#include <cuda_runtime.h>
#include <device_launch_parameters.h>
#include <cstdio>
#include <utility>
#include <cub/cub.cuh>
#include <cub/device/device_topk.cuh>
#include <cub/version.cuh>
#include <cuda/__execution/determinism.h>
#include <cuda/__execution/output_ordering.h>
#include <cuda/__execution/require.h>
#include <cuda/std/__cccl/version.h>
#include <cuda/stream>

#if !defined(CUB_VERSION) || CUB_VERSION < 300300
#error "module_optimizer_cuda.cu requires CUB 3.3.0 or newer from vendored CCCL."
#endif

#if !defined(CCCL_VERSION) || CCCL_VERSION < 3003000
#error "module_optimizer_cuda.cu requires CCCL 3.3.0 or newer from vendored libcudacxx."
#endif

/// @brief GPU配置信息结构体
struct GpuConfig
{
    int max_threads_per_block;    // 每个block最大线程数
    int max_blocks_per_sm;        // 每个SM最大block数
    int multiprocessor_count;     // SM数量
    int max_grid_size;            // 最大grid大小
    size_t global_memory;         // 全局内存大小
    int compute_capability_major; // 计算能力主版本
    int compute_capability_minor; // 计算能力次版本

    // 计算得出的优化参数
    int optimal_block_size;       // 优化的block大小
    int optimal_grid_size;        // 优化的grid大小
    long long optimal_batch_size; // 优化的batch大小
};

template <typename T>
class DeviceBuffer
{
public:
    DeviceBuffer() = default;
    ~DeviceBuffer()
    {
        Reset();
    }

    DeviceBuffer(const DeviceBuffer &) = delete;
    DeviceBuffer &operator=(const DeviceBuffer &) = delete;

    DeviceBuffer(DeviceBuffer &&other) noexcept
        : ptr_(std::exchange(other.ptr_, nullptr))
    {
    }

    DeviceBuffer &operator=(DeviceBuffer &&other) noexcept
    {
        if (this != &other)
        {
            Reset();
            ptr_ = std::exchange(other.ptr_, nullptr);
        }
        return *this;
    }

    cudaError_t Allocate(size_t count)
    {
        Reset();
        if (count == 0)
            return cudaSuccess;
        return cudaMalloc(reinterpret_cast<void **>(&ptr_), count * sizeof(T));
    }

    void Reset()
    {
        if (ptr_)
        {
            cudaFree(ptr_);
            ptr_ = nullptr;
        }
    }

    T *get()
    {
        return ptr_;
    }

    const T *get() const
    {
        return ptr_;
    }

private:
    T *ptr_ = nullptr;
};

class CudaEventHandle
{
public:
    CudaEventHandle() = default;
    ~CudaEventHandle()
    {
        Reset();
    }

    CudaEventHandle(const CudaEventHandle &) = delete;
    CudaEventHandle &operator=(const CudaEventHandle &) = delete;

    CudaEventHandle(CudaEventHandle &&other) noexcept
        : event_(std::exchange(other.event_, nullptr))
    {
    }

    CudaEventHandle &operator=(CudaEventHandle &&other) noexcept
    {
        if (this != &other)
        {
            Reset();
            event_ = std::exchange(other.event_, nullptr);
        }
        return *this;
    }

    cudaError_t Create()
    {
        Reset();
        return cudaEventCreate(&event_);
    }

    void Reset()
    {
        if (event_)
        {
            cudaEventDestroy(event_);
            event_ = nullptr;
        }
    }

    cudaEvent_t get() const
    {
        return event_;
    }

private:
    cudaEvent_t event_ = nullptr;
};

/// @brief CUDA 槽位数值到分值查表
__constant__ int D_SLOT_VALUE_POWER[24 * 21];
/// @brief CUDA 槽位最小属性需求表
__constant__ int D_MIN_ATTR_REQUIREMENTS[24];
/// @brief 总属性战斗力映射表
/// @details 从0到120的属性总值对应的战斗力映射
__constant__ int D_TOTAL_ATTR_POWER_VALUES[121] = {
    0, 5, 11, 17, 23, 29, 34, 40, 46, 52, 58, 64, 69, 75, 81, 87, 93, 99, 104, 110, 116,
    122, 128, 133, 139, 145, 151, 157, 163, 168, 174, 180, 186, 192, 198, 203, 209, 215, 221, 227, 233,
    238, 244, 250, 256, 262, 267, 273, 279, 285, 291, 297, 302, 308, 314, 320, 326, 332, 337, 343, 349,
    355, 361, 366, 372, 378, 384, 390, 396, 401, 407, 413, 419, 425, 431, 436, 442, 448, 454, 460, 466,
    471, 477, 483, 489, 495, 500, 506, 512, 518, 524, 530, 535, 541, 547, 553, 559, 565, 570, 576, 582,
    588, 594, 599, 605, 611, 617, 623, 629, 634, 640, 646, 652, 658, 664, 669, 675, 681, 687, 693, 699};

/// @brief 用于判断是否支持CUDA加速
/// @param data 数据数组指针
/// @param size 数据数组大小
__global__ void TestKernel(int *data, int size)
{
    int idx = blockIdx.x * blockDim.x + threadIdx.x;
    if (idx < size)
    {
        data[idx] = idx * 2;
    }
}

/// @brief 计算组合数
/// @param n 总元素数量
/// @param r 选择元素数量
/// @return 组合数
__device__ long long GpuCombinationCount(int n, int r)
{
    if (r > n || r < 0)
        return 0;
    if (r == 0 || r == n)
        return 1;
    if (r > n - r)
        r = n - r;

    long long result = 1;
    for (int i = 0; i < r; ++i)
    {
        result = result * (n - i) / (i + 1);
    }
    return result;
}

/// @brief 根据索引生成第 k 个组合
/// @param n 总元素数量
/// @param r 选择元素数量
/// @param index 组合索引
/// @param combination 组合结果
__device__ void GpuGetCombinationByIndex(int n, int r, long long index, int *combination)
{
    long long remaining = index;

    for (int i = 0; i < r; ++i)
    {
        const int start = (i == 0) ? 0 : combination[i - 1] + 1;
        const int choose_count = r - i;
        const int max_candidate = n - choose_count;
        const long long base_prefix = GpuCombinationCount(n - start, choose_count);

        int lo = start;
        int hi = max_candidate + 1;
        while (lo + 1 < hi)
        {
            const int mid = lo + ((hi - lo) >> 1);
            const long long skipped_before_mid =
                base_prefix - GpuCombinationCount(n - mid, choose_count);

            if (skipped_before_mid <= remaining)
            {
                lo = mid;
            }
            else
            {
                hi = mid;
            }
        }

        const long long skipped_before_lo =
            base_prefix - GpuCombinationCount(n - lo, choose_count);
        combination[i] = lo;
        remaining -= skipped_before_lo;
    }
}

template <int R>
__device__ inline int GpuNextCombinationLevel(int n, int *comb)
{
    for (int pos = R - 1; pos >= 0; --pos)
    {
        const int limit = n - (R - pos);
        if (comb[pos] < limit)
        {
            ++comb[pos];
            for (int k = pos + 1; k < R; ++k)
            {
                comb[k] = comb[k - 1] + 1;
            }
            return (R - 1) - pos;
        }
    }
    return -1;
}

constexpr int CUDA_ATTR_DIM = 24;
constexpr int CUDA_ATTR_CHUNKS = CUDA_ATTR_DIM / 4;

__device__ inline void ComputePrefixSum3(
    const int4 *__restrict__ row0,
    const int4 *__restrict__ row1,
    const int4 *__restrict__ row2,
    int4 *sum_abc)
{
    for (int chunk = 0; chunk < CUDA_ATTR_CHUNKS; ++chunk)
    {
        const int4 a = row0[chunk];
        const int4 b = row1[chunk];
        const int4 c = row2[chunk];
        sum_abc[chunk] = make_int4(
            a.x + b.x + c.x,
            a.y + b.y + c.y,
            a.z + b.z + c.z,
            a.w + b.w + c.w);
    }
}

__device__ inline void ComputePrefixSum4(
    const int4 *__restrict__ row0,
    const int4 *__restrict__ row1,
    const int4 *__restrict__ row2,
    const int4 *__restrict__ row3,
    int4 *sum_abcd)
{
    for (int chunk = 0; chunk < CUDA_ATTR_CHUNKS; ++chunk)
    {
        const int4 a = row0[chunk];
        const int4 b = row1[chunk];
        const int4 c = row2[chunk];
        const int4 d = row3[chunk];
        sum_abcd[chunk] = make_int4(
            a.x + b.x + c.x + d.x,
            a.y + b.y + c.y + d.y,
            a.z + b.z + c.z + d.z,
            a.w + b.w + c.w + d.w);
    }
}

/// @brief CUDA 枚举算子
/// @param module_matrix 模组稠密属性矩阵
/// @param module_count 模组总数
/// @param start_combination 起始组合索引
/// @param end_combination 结束组合索引
/// @param scores 输出参数: 计算得到的战斗力数组
/// @param indices 输出参数: 打包的模组索引数组
template <int R>
__global__ void GpuEnumerationKernel(
    const int *__restrict__ module_matrix,
    int module_count,
    long long start_combination,
    long long end_combination,
    int *scores,
    long long *indices)
{
    const int4 *module_matrix_vec = reinterpret_cast<const int4 *>(module_matrix);
    const int4 *module_data_vec = module_matrix_vec;

    long long tid = blockIdx.x * blockDim.x + threadIdx.x;
    long long total_threads = gridDim.x * blockDim.x;

    long long range_start = start_combination;
    long long range_end = end_combination;
    long long range_size = range_end - range_start;
    if (range_size <= 0)
        return;

    long long L = (range_size + total_threads - 1) / total_threads;
    long long seg_start = range_start + tid * L;
    if (seg_start >= range_end)
        return;
    long long seg_end = min(seg_start + L, range_end);
    long long active_threads = (range_size + L - 1) / L;
    long long last_segment_length = range_size - (active_threads - 1) * L;

    int combo[R];
    GpuGetCombinationByIndex(module_count, R, seg_start, combo);
    int4 sum_prefix[CUDA_ATTR_CHUNKS];
    if constexpr (R == 5)
    {
        ComputePrefixSum4(
            module_data_vec + combo[0] * CUDA_ATTR_CHUNKS,
            module_data_vec + combo[1] * CUDA_ATTR_CHUNKS,
            module_data_vec + combo[2] * CUDA_ATTR_CHUNKS,
            module_data_vec + combo[3] * CUDA_ATTR_CHUNKS,
            sum_prefix);
    }
    else
    {
        ComputePrefixSum3(
            module_data_vec + combo[0] * CUDA_ATTR_CHUNKS,
            module_data_vec + combo[1] * CUDA_ATTR_CHUNKS,
            module_data_vec + combo[2] * CUDA_ATTR_CHUNKS,
            sum_prefix);
    }

    long long local_offset = 0;
    for (long long combo_idx = seg_start; combo_idx < seg_end; ++combo_idx, ++local_offset)
    {
        long long output_idx = 0;
        if (local_offset < last_segment_length)
        {
            output_idx = local_offset * active_threads + tid;
        }
        else
        {
            output_idx = last_segment_length * active_threads +
                         (local_offset - last_segment_length) * (active_threads - 1) + tid;
        }

        const int4 *row_d_vec = module_data_vec + combo[R - 1] * CUDA_ATTR_CHUNKS;

        int total_attr_value = 0;
        int threshold_power = 0;
        int valid_mask = 1;

        for (int chunk = 0; chunk < CUDA_ATTR_CHUNKS; ++chunk)
        {
            const int4 sabc = sum_prefix[chunk];
            const int4 vd = row_d_vec[chunk];

            const int attr_idx = chunk * 4;

            int attr_value = sabc.x + vd.x;
            total_attr_value += attr_value;
            valid_mask &= (attr_value >= D_MIN_ATTR_REQUIREMENTS[attr_idx]);
            threshold_power += D_SLOT_VALUE_POWER[attr_idx * 21 + min(attr_value, 20)];

            attr_value = sabc.y + vd.y;
            total_attr_value += attr_value;
            valid_mask &= (attr_value >= D_MIN_ATTR_REQUIREMENTS[attr_idx + 1]);
            threshold_power += D_SLOT_VALUE_POWER[(attr_idx + 1) * 21 + min(attr_value, 20)];

            attr_value = sabc.z + vd.z;
            total_attr_value += attr_value;
            valid_mask &= (attr_value >= D_MIN_ATTR_REQUIREMENTS[attr_idx + 2]);
            threshold_power += D_SLOT_VALUE_POWER[(attr_idx + 2) * 21 + min(attr_value, 20)];

            attr_value = sabc.w + vd.w;
            total_attr_value += attr_value;
            valid_mask &= (attr_value >= D_MIN_ATTR_REQUIREMENTS[attr_idx + 3]);
            threshold_power += D_SLOT_VALUE_POWER[(attr_idx + 3) * 21 + min(attr_value, 20)];
        }

        int total_attr_power = D_TOTAL_ATTR_POWER_VALUES[min(total_attr_value, 120)];
        int combat_power = threshold_power + total_attr_power;

        long long packed = 0;
        for (int i = 0; i < R; ++i)
        {
            packed |= ((long long)(combo[i] & 0x0FFF) << (i * 12));
        }

        scores[output_idx] = combat_power * valid_mask;
        indices[output_idx] = packed * static_cast<long long>(valid_mask);

        const int combination_level = GpuNextCombinationLevel<R>(module_count, combo);
        if (combination_level < 0)
        {
            break;
        }

        if (combination_level >= 1)
        {
            if constexpr (R == 5)
            {
                ComputePrefixSum4(
                    module_data_vec + combo[0] * CUDA_ATTR_CHUNKS,
                    module_data_vec + combo[1] * CUDA_ATTR_CHUNKS,
                    module_data_vec + combo[2] * CUDA_ATTR_CHUNKS,
                    module_data_vec + combo[3] * CUDA_ATTR_CHUNKS,
                    sum_prefix);
            }
            else
            {
                ComputePrefixSum3(
                    module_data_vec + combo[0] * CUDA_ATTR_CHUNKS,
                    module_data_vec + combo[1] * CUDA_ATTR_CHUNKS,
                    module_data_vec + combo[2] * CUDA_ATTR_CHUNKS,
                    sum_prefix);
            }
        }
    }
}

static cudaError_t SelectBatchTopKPairs(
    void *d_temp_storage,
    size_t temp_storage_bytes,
    int *d_scores_in,
    int *d_scores_topk,
    long long *d_indices_in,
    long long *d_indices_topk,
    int num_items,
    int k)
{
    if (num_items <= 0 || k <= 0)
    {
        return cudaSuccess;
    }

    auto requirements = cuda::execution::require(
        cuda::execution::determinism::not_guaranteed,
        cuda::execution::output_ordering::unsorted);
    cuda::stream_ref stream_ref{cudaStream_t{}};
    auto env = cuda::std::execution::env{stream_ref, requirements};

    cudaError_t err = cub::DeviceTopK::MaxPairs(
        d_temp_storage,
        temp_storage_bytes,
        d_scores_in,
        d_scores_topk,
        d_indices_in,
        d_indices_topk,
        num_items,
        k,
        env);
    return err;
}

/// @brief 获取GPU配置信息
/// @param config 输出的GPU配置信息
/// @return 1表示成功，0表示失败
int GetGpuConfig(GpuConfig *config)
{
    cudaError_t err;
    cudaDeviceProp prop;

    err = cudaGetDeviceProperties(&prop, 0);
    if (err != cudaSuccess)
    {
        return 0;
    }

    config->max_threads_per_block = prop.maxThreadsPerBlock;
    config->max_blocks_per_sm = prop.maxBlocksPerMultiProcessor;
    config->multiprocessor_count = prop.multiProcessorCount;
    config->max_grid_size = prop.maxGridSize[0];
    config->global_memory = prop.totalGlobalMem;
    config->compute_capability_major = prop.major;
    config->compute_capability_minor = prop.minor;

    return 1;
}

/// @brief 计算优化的GPU执行参数
/// @param config GPU配置信息
/// @param total_combinations 总组合数
void CalculateOptimalParams(GpuConfig *config, long long total_combinations)
{

    config->optimal_block_size = 384;

    // 确保不超过硬件限制
    config->optimal_block_size = min(config->optimal_block_size, config->max_threads_per_block);

    // 计算优化的grid大小
    int total_cores = config->multiprocessor_count * config->max_blocks_per_sm;
    config->optimal_grid_size = min(total_cores * 2, config->max_grid_size);

    // 基于实际工作负载调整
    long long max_concurrent_threads = (long long)config->optimal_grid_size * config->optimal_block_size;
    if (total_combinations < max_concurrent_threads)
    {
        config->optimal_grid_size = (int)((total_combinations + config->optimal_block_size - 1) / config->optimal_block_size);
    }

    // 计算优化的batch大小
    size_t available_memory = config->global_memory * 0.5;
    long long memory_limited_batch = available_memory / (sizeof(int) + sizeof(long long));

    // 基于计算能力的batch大小
    long long compute_limited_batch = max_concurrent_threads * 3000;

    // 取较小值, 但至少10万, 最大500万
    config->optimal_batch_size = max(100000LL, min(memory_limited_batch, compute_limited_batch));
    config->optimal_batch_size = min(config->optimal_batch_size, 100000000LL);
}

/// @brief 用于判断是否支持CUDA加速
/// @return 1表示CUDA可用，0表示CUDA不可用
extern "C" int TestCuda()
{
    int device_count = 0;
    cudaError_t err = cudaGetDeviceCount(&device_count);

    if (err != cudaSuccess || device_count == 0)
    {
        return 0;
    }

    int *d_data;
    const int size = 1024;
    err = cudaMalloc(&d_data, size * sizeof(int));
    if (err != cudaSuccess)
    {
        return 0;
    }

    dim3 block(256);
    dim3 grid((size + block.x - 1) / block.x);
    TestKernel<<<grid, block>>>(d_data, size);

    err = cudaDeviceSynchronize();
    cudaFree(d_data);

    return (err == cudaSuccess) ? 1 : 0;
}

/// @brief 计算组合数
/// @param n 总元素数量
/// @param r 选择元素数量
/// @return 组合数
long long CpuCombinationCount(int n, int r)
{
    if (r > n || r < 0)
        return 0;
    if (r == 0 || r == n)
        return 1;
    if (r > n - r)
        r = n - r;

    long long result = 1;
    for (int i = 0; i < r; ++i)
    {
        result = result * (n - i) / (i + 1);
    }
    return result;
}

extern "C" int GpuStrategyEnumeration(
    const int *module_matrix,
    int module_count,
    const int *slot_value_power,
    const int *min_attr_requirements,
    int max_solutions,
    int *result_scores,
    long long *result_indices,
    int combination_size)
{
    long long total_combinations = CpuCombinationCount(module_count, combination_size);

    GpuConfig gpu_config;
    if (!GetGpuConfig(&gpu_config))
    {
        printf("Failed to get GPU configuration\n");
        return 0;
    }

    CalculateOptimalParams(&gpu_config, total_combinations);

    printf("GPU Configuration:\n");
    printf("  Compute Capability: %d.%d\n", gpu_config.compute_capability_major, gpu_config.compute_capability_minor);
    printf("  Multiprocessors: %d\n", gpu_config.multiprocessor_count);
    printf("  Global Memory: %.1f MB\n", (double)gpu_config.global_memory / (1024 * 1024));
    printf("Optimal Parameters:\n");
    printf("  Block Size: %d\n", gpu_config.optimal_block_size);
    printf("  Grid Size: %d\n", gpu_config.optimal_grid_size);
    printf("  Batch Size: %lld\n", gpu_config.optimal_batch_size);

    long long batch_size = gpu_config.optimal_batch_size;

    std::vector<int> global_best_scores(max_solutions, 0);
    std::vector<long long> global_best_indices(max_solutions, 0);

    DeviceBuffer<int> d_module_matrix;
    DeviceBuffer<int> d_scores;
    DeviceBuffer<int> d_scores_sorted;
    DeviceBuffer<long long> d_indices;
    DeviceBuffer<long long> d_indices_sorted;
    DeviceBuffer<unsigned char> d_temp_storage;

    cudaError_t err = cudaMemcpyToSymbol(
        D_SLOT_VALUE_POWER,
        slot_value_power,
        CUDA_ATTR_DIM * 21 * sizeof(int));
    if (err != cudaSuccess)
    {
        printf("ERROR: CUDA memcpy to constant failed(slot_value_power): %s\n", cudaGetErrorString(err));
        return 0;
    }

    err = cudaMemcpyToSymbol(
        D_MIN_ATTR_REQUIREMENTS,
        min_attr_requirements,
        CUDA_ATTR_DIM * sizeof(int));
    if (err != cudaSuccess)
    {
        printf("ERROR: CUDA memcpy to constant failed(min_attr_requirements): %s\n", cudaGetErrorString(err));
        return 0;
    }

    err = d_module_matrix.Allocate(module_count * CUDA_ATTR_DIM);
    if (err != cudaSuccess)
    {
        printf("ERROR: CUDA malloc failed(module_matrix): %s\n", cudaGetErrorString(err));
        return 0;
    }

    err = cudaMemcpy(
        d_module_matrix.get(),
        module_matrix,
        module_count * CUDA_ATTR_DIM * (int)sizeof(int),
        cudaMemcpyHostToDevice);
    if (err != cudaSuccess)
    {
        printf("ERROR: CUDA memcpy failed(module_matrix): %s\n", cudaGetErrorString(err));
        return 0;
    }

    err = d_scores.Allocate(batch_size);
    if (err != cudaSuccess)
    {
        printf("ERROR: CUDA malloc failed(scores): %s\n", cudaGetErrorString(err));
        return 0;
    }

    err = d_indices.Allocate(batch_size);
    if (err != cudaSuccess)
    {
        printf("ERROR: CUDA malloc failed(indices): %s\n", cudaGetErrorString(err));
        return 0;
    }

    err = d_scores_sorted.Allocate(max_solutions);
    if (err != cudaSuccess)
    {
        printf("ERROR: CUDA malloc failed(scores_sorted): %s\n", cudaGetErrorString(err));
        return 0;
    }

    err = d_indices_sorted.Allocate(max_solutions);
    if (err != cudaSuccess)
    {
        printf("ERROR: CUDA malloc failed(indices_sorted): %s\n", cudaGetErrorString(err));
        return 0;
    }

    size_t temp_storage_bytes_topk = 0;
    auto topk_requirements = cuda::execution::require(
        cuda::execution::determinism::not_guaranteed,
        cuda::execution::output_ordering::unsorted);
    cuda::stream_ref topk_stream_ref{cudaStream_t{}};
    auto topk_env = cuda::std::execution::env{topk_stream_ref, topk_requirements};

    cub::DeviceTopK::MaxPairs(
        d_temp_storage.get(), temp_storage_bytes_topk,
        d_scores.get(), d_scores_sorted.get(),
        d_indices.get(), d_indices_sorted.get(),
        (int)batch_size, max_solutions, topk_env);
    size_t temp_storage_bytes = temp_storage_bytes_topk;

    err = d_temp_storage.Allocate(temp_storage_bytes);
    if (err != cudaSuccess)
    {
        printf("ERROR: CUDA malloc failed(temp_storage): %s\n", cudaGetErrorString(err));
        return 0;
    }

    for (long long batch_start = 0; batch_start < total_combinations; batch_start += batch_size)
    {
        long long current_batch_size = min(batch_size, total_combinations - batch_start);

        dim3 block(gpu_config.optimal_block_size);
        int grid_size = min(gpu_config.optimal_grid_size, (int)((current_batch_size + block.x - 1) / block.x));
        dim3 grid(grid_size);

        if (combination_size == 5)
        {
            GpuEnumerationKernel<5><<<grid, block>>>(
                d_module_matrix.get(),
                module_count,
                batch_start,
                batch_start + current_batch_size,
                d_scores.get(),
                d_indices.get());
        }
        else
        {
            GpuEnumerationKernel<4><<<grid, block>>>(
                d_module_matrix.get(),
                module_count,
                batch_start,
                batch_start + current_batch_size,
                d_scores.get(),
                d_indices.get());
        }

        err = cudaGetLastError();
        if (err != cudaSuccess)
        {
            printf("ERROR: CUDA kernel launch failed: %s\n", cudaGetErrorString(err));
            return 0;
        }
        err = cudaDeviceSynchronize();
        if (err != cudaSuccess)
        {
            printf("ERROR: CUDA kernel synchronize failed: %s\n", cudaGetErrorString(err));
            return 0;
        }

        int batch_topk_count = min(max_solutions, (int)current_batch_size);
        err = SelectBatchTopKPairs(
            d_temp_storage.get(),
            temp_storage_bytes,
            d_scores.get(),
            d_scores_sorted.get(),
            d_indices.get(),
            d_indices_sorted.get(),
            (int)current_batch_size,
            batch_topk_count);
        if (err != cudaSuccess)
        {
            printf("ERROR: DeviceTopK selection failed: %s\n", cudaGetErrorString(err));
            return 0;
        }
        err = cudaDeviceSynchronize();
        if (err != cudaSuccess)
        {
            printf("ERROR: CUDA topk synchronize failed: %s\n", cudaGetErrorString(err));
            return 0;
        }

        int results_to_transfer = min(max_solutions, (int)current_batch_size);
        std::vector<int> batch_scores(results_to_transfer);
        std::vector<long long> batch_indices(results_to_transfer);

        err = cudaMemcpy(batch_scores.data(), d_scores_sorted.get(), results_to_transfer * sizeof(int), cudaMemcpyDeviceToHost);
        if (err != cudaSuccess)
        {
            printf("ERROR: CUDA result transfer failed(batch_scores): %s\n", cudaGetErrorString(err));
            return 0;
        }
        err = cudaMemcpy(batch_indices.data(), d_indices_sorted.get(), results_to_transfer * sizeof(long long), cudaMemcpyDeviceToHost);
        if (err != cudaSuccess)
        {
            printf("ERROR: CUDA result transfer failed(batch_indices): %s\n", cudaGetErrorString(err));
            return 0;
        }

        for (int i = 0; i < results_to_transfer; ++i)
        {
            bool should_insert = false;
            int insert_pos = max_solutions;

            for (int j = 0; j < max_solutions; ++j)
            {
                if (global_best_scores[j] == 0 || batch_scores[i] > global_best_scores[j])
                {
                    insert_pos = j;
                    should_insert = true;
                    break;
                }
            }

            if (should_insert && insert_pos < max_solutions)
            {
                for (int j = max_solutions - 1; j > insert_pos; --j)
                {
                    global_best_scores[j] = global_best_scores[j - 1];
                    global_best_indices[j] = global_best_indices[j - 1];
                }
                global_best_scores[insert_pos] = batch_scores[i];
                global_best_indices[insert_pos] = batch_indices[i];
            }
        }
    }

    for (int i = 0; i < max_solutions; ++i)
    {
        result_scores[i] = global_best_scores[i];
        result_indices[i] = global_best_indices[i];
    }

    return max_solutions;
}
