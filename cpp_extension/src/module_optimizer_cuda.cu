#include <cuda_runtime.h>
#include <device_launch_parameters.h>
#include <cstdio>
#include <thrust/device_vector.h>
#include <thrust/sort.h>
#include <thrust/copy.h>
#include <thrust/tuple.h>
#include <thrust/iterator/zip_iterator.h>

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

/// @brief 属性阈值常量数组
__constant__ int D_ATTR_THRESHOLDS[6] = {1, 4, 8, 12, 16, 20};
/// @brief 基础属性战斗力常量数组
__constant__ int D_BASIC_POWER_VALUES[6] = {7, 14, 29, 44, 167, 254};
/// @brief 特殊属性战斗力常量数组
__constant__ int D_SPECIAL_POWER_VALUES[6] = {14, 29, 59, 89, 298, 448};
/// @brief 特殊属性ID常量数组
__constant__ int D_SPECIAL_ATTRS[8] = {2104, 2105, 2204, 2205, 2404, 2405, 2406, 2304};
/// @brief 总属性战斗力映射表
/// @details 从0到120的属性总值对应的战斗力映射
__constant__ int D_TOTAL_ATTR_POWER_VALUES[121] = {
    0, 5, 11, 17, 23, 29, 34, 40, 46, 52, 58, 64, 69, 75, 81, 87, 93, 99, 104, 110, 116,
    122, 128, 133, 139, 145, 151, 157, 163, 168, 174, 180, 186, 192, 198, 203, 209, 215, 221, 227, 233,
    238, 244, 250, 256, 262, 267, 273, 279, 285, 291, 297, 302, 308, 314, 320, 326, 332, 337, 343, 349,
    355, 361, 366, 372, 378, 384, 390, 396, 401, 407, 413, 419, 425, 431, 436, 442, 448, 454, 460, 466,
    471, 477, 483, 489, 495, 500, 506, 512, 518, 524, 530, 535, 541, 547, 553, 559, 565, 570, 576, 582,
    588, 594, 599, 605, 611, 617, 623, 629, 634, 640, 646, 652, 658, 664, 669, 675, 681, 687, 693, 699};

/// @brief 计算模组组合的战斗力
/// @param combo 模组组合索引数组
/// @param attr_ids 所有模组的属性ID数组
/// @param attr_values 所有模组的属性值数组
/// @param attr_counts 每个模组的属性数量数组
/// @param offsets 每个模组在属性数组中的偏移量
/// @param target_attrs 目标属性ID数组
/// @param target_count 目标属性数量
/// @param exclude_attrs 排除属性ID数组
/// @param exclude_count 排除属性数量
/// @return 战力
__device__ int CalculatePowerGpu(
    const int *combo,
    const int *attr_ids,
    const int *attr_values,
    const int *attr_counts,
    const int *offsets,
    const int *target_attrs,
    int target_count,
    const int *exclude_attrs,
    int exclude_count)
{
    int aggregated_ids[20];
    int aggregated_values[20];
    int agg_count = 0;
    int total_attr_value = 0;

    for (int m = 0; m < 4; ++m)
    {
        int module_idx = combo[m];
        int start_offset = offsets[module_idx];
        int attr_cnt = attr_counts[module_idx];

        for (int i = 0; i < attr_cnt; ++i)
        {
            int attr_id = attr_ids[start_offset + i];
            int attr_value = attr_values[start_offset + i];

            total_attr_value += attr_value;

            int found_idx = -1;
            for (int j = 0; j < agg_count; ++j)
            {
                if (aggregated_ids[j] == attr_id)
                {
                    found_idx = j;
                    break;
                }
            }

            if (found_idx >= 0)
            {
                aggregated_values[found_idx] += attr_value;
            }
            else
            {
                aggregated_ids[agg_count] = attr_id;
                aggregated_values[agg_count] = attr_value;
                agg_count++;
            }
        }
    }

    int threshold_power = 0;

    for (int i = 0; i < agg_count; ++i)
    {
        int attr_id = aggregated_ids[i];
        int attr_value = aggregated_values[i];

        int max_level = 0;
        for (int j = 0; j < 6; ++j)
        {
            if (attr_value >= D_ATTR_THRESHOLDS[j])
            {
                max_level = j + 1;
            }
        }

        if (max_level > 0)
        {
            bool is_special = false;
            for (int j = 0; j < 8; ++j)
            {
                if (attr_id == D_SPECIAL_ATTRS[j])
                {
                    is_special = true;
                    break;
                }
            }

            int base_power = is_special ? D_SPECIAL_POWER_VALUES[max_level - 1] : D_BASIC_POWER_VALUES[max_level - 1];
            int power_multiplier = 1;

            for (int j = 0; j < target_count; ++j)
            {
                if (attr_id == target_attrs[j])
                {
                    power_multiplier = 2;
                    break;
                }
            }

            if (power_multiplier != 2)
            {
                for (int j = 0; j < exclude_count; ++j)
                {
                    if (attr_id == exclude_attrs[j])
                    {
                        power_multiplier = 0;
                        break;
                    }
                }
            }

            threshold_power += base_power * power_multiplier;
        }
    }

    int total_attr_power = D_TOTAL_ATTR_POWER_VALUES[total_attr_value];

    return threshold_power + total_attr_power;
}

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
        int start = (i == 0) ? 0 : combination[i - 1] + 1;
        for (int j = start; j < n; ++j)
        {
            long long combinations_after = GpuCombinationCount(n - j - 1, r - i - 1);
            if (remaining < combinations_after)
            {
                combination[i] = j;
                break;
            }
            remaining -= combinations_after;
        }
    }
}

__device__ inline bool GpuNextCombination(int n, int r, int *comb)
{
    for (int pos = r - 1; pos >= 0; --pos)
    {
        int limit = n - r + pos;
        if (comb[pos] < limit)
        {
            ++comb[pos];
            for (int k = pos + 1; k < r; ++k)
            {
                comb[k] = comb[k - 1] + 1;
            }
            return true;
        }
    }
    return false;
}

/// @brief CUDA枚举算子
/// @param attr_ids 所有模组的属性ID数组
/// @param attr_values 所有模组的属性值数组
/// @param attr_counts 每个模组的属性数量数组
/// @param offsets 每个模组在属性数组中的偏移量
/// @param module_count 模组总数
/// @param start_combination 起始组合索引
/// @param end_combination 结束组合索引
/// @param target_attrs 目标属性ID数组
/// @param target_count 目标属性数量
/// @param exclude_attrs 排除属性ID数组
/// @param exclude_count 排除属性数量
/// @param min_attr_ids 最小属性需求ID数组
/// @param min_attr_values 最小属性需求值数组
/// @param min_attr_count 最小属性需求数量
/// @param scores 输出参数: 计算得到的战斗力数组
/// @param indices 输出参数: 打包的模组索引数组
__global__ void GpuEnumerationKernel(
    const int *attr_ids,
    const int *attr_values,
    const int *attr_counts,
    const int *offsets,
    int module_count,
    long long start_combination,
    long long end_combination,
    const int *target_attrs,
    int target_count,
    const int *exclude_attrs,
    int exclude_count,
    const int *min_attr_ids,
    const int *min_attr_values,
    int min_attr_count,
    int *scores,
    long long *indices)
{
    long long tid = blockIdx.x * blockDim.x + threadIdx.x;
    long long total_threads = gridDim.x * blockDim.x;

    long long S = start_combination;
    long long E = end_combination;
    long long R = E - S;
    if (R <= 0)
        return;

    long long L = (R + total_threads - 1) / total_threads;
    long long seg_start = S + tid * L;
    if (seg_start >= E)
        return;
    long long seg_end = min(seg_start + L, E);

    int combo[4];
    GpuGetCombinationByIndex(module_count, 4, seg_start, combo);

    long long local_offset = 0;
    for (long long combo_idx = seg_start; combo_idx < seg_end; ++combo_idx, ++local_offset)
    {
        if (min_attr_count > 0)
        {
            bool valid = true;
            for (int req_idx = 0; req_idx < min_attr_count; ++req_idx)
            {
                int required_attr_id = min_attr_ids[req_idx];
                int required_min_value = min_attr_values[req_idx];
                int actual_sum = 0;

                for (int m = 0; m < 4; ++m)
                {
                    int module_idx = combo[m];
                    int start_offset = offsets[module_idx];
                    int attr_cnt = attr_counts[module_idx];

                    for (int i = 0; i < attr_cnt; ++i)
                    {
                        int attr_id = attr_ids[start_offset + i];
                        if (attr_id == required_attr_id)
                        {
                            actual_sum += attr_values[start_offset + i];
                        }
                    }
                }

                if (actual_sum < required_min_value)
                {
                    valid = false;
                    break;
                }
            }
            if (!valid)
            {
                if (!GpuNextCombination(module_count, 4, combo))
                    break;
                continue;
            }
        }

        int combat_power = CalculatePowerGpu(
            combo, attr_ids, attr_values, attr_counts, offsets,
            target_attrs, target_count, exclude_attrs, exclude_count);

        long long packed = 0;
        for (int i = 0; i < 4; ++i)
        {
            packed |= ((long long)combo[i] << (i * 16));
        }

        long long output_idx = (seg_start - S) + local_offset;
        scores[output_idx] = combat_power;
        indices[output_idx] = packed;

        if (!GpuNextCombination(module_count, 4, combo))
            break;
    }
}

/// @brief 获取前TOP解
/// @param d_scores 分数数组
/// @param d_indices 索引数组
/// @param total_count 总结果数量
/// @param top_count 需要的TOP结果
void GpuSortTopSolutions(int *d_scores, long long *d_indices, int total_count, int top_count)
{
    thrust::device_ptr<int> scores_ptr(d_scores);
    thrust::device_ptr<long long> indices_ptr(d_indices);
    thrust::sort_by_key(scores_ptr, scores_ptr + total_count, indices_ptr, thrust::greater<int>());
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
    // 计算优化的block大小
    if (config->compute_capability_major >= 7)
    {
        // 较新的GPU
        config->optimal_block_size = 512;
    }
    else if (config->compute_capability_major >= 6)
    {
        // Pascal架构
        config->optimal_block_size = 256;
    }
    else
    {
        // 较老的GPU
        config->optimal_block_size = 192;
    }

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
    long long compute_limited_batch = max_concurrent_threads * 1000;

    // 取较小值, 但至少10万, 最大500万
    config->optimal_batch_size = max(100000LL, min(memory_limited_batch, compute_limited_batch));
    config->optimal_batch_size = min(config->optimal_batch_size, 5000000LL);
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

/// @brief 完整的CUDA策略枚举函数
/// @param module_attr_ids 所有模组的属性ID数组
/// @param module_attr_values 所有模组的属性值数组
/// @param module_attr_counts 每个模组的属性数量数组
/// @param module_offsets 每个模组在属性数组中的偏移量
/// @param module_count 模组总数
/// @param total_attrs 总属性数量
/// @param target_attrs 目标属性ID数组
/// @param target_count 目标属性数量
/// @param exclude_attrs 排除属性ID数组
/// @param exclude_count 排除属性数量
/// @param min_attr_ids 最小属性需求ID数组
/// @param min_attr_values 最小属性需求值数组
/// @param min_attr_count 最小属性需求数量
/// @param max_solutions 最大解决方案数量
/// @param result_scores 输出参数：结果分数数组
/// @param result_indices 输出参数：结果模组索引数组
/// @return 成功处理的解决方案数量，0表示失败
extern "C" int GpuStrategyEnumeration(
    const int *module_attr_ids,
    const int *module_attr_values,
    const int *module_attr_counts,
    const int *module_offsets,
    int module_count,
    int total_attrs,
    const int *target_attrs,
    int target_count,
    const int *exclude_attrs,
    int exclude_count,
    const int *min_attr_ids,
    const int *min_attr_values,
    int min_attr_count,
    int max_solutions,
    int *result_scores,
    long long *result_indices)
{
    // 计算要处理的组合数
    long long total_combinations = CpuCombinationCount(module_count, 4);

    // 获取GPU配置并计算优化参数
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

    // 使用优化的批处理大小
    long long batch_size = gpu_config.optimal_batch_size;

    std::vector<int> global_best_scores(max_solutions, 0);
    std::vector<long long> global_best_indices(max_solutions, 0);
    long long processed = 0;

    // 分配GPU内存
    int *d_attr_ids = nullptr;
    int *d_attr_values = nullptr;
    int *d_attr_counts = nullptr;
    int *d_offsets = nullptr;
    int *d_target_attrs = nullptr;
    int *d_exclude_attrs = nullptr;
    int *d_min_attr_ids = nullptr;
    int *d_min_attr_values = nullptr;
    int *d_scores = nullptr;
    long long *d_indices = nullptr;

    cudaError_t err;

    err = cudaMalloc(&d_attr_ids, total_attrs * sizeof(int));
    if (err != cudaSuccess)
    {
        printf("ERROR: CUDA malloc failed (attr_ids): %s\n", cudaGetErrorString(err));
        return 0;
    }

    err = cudaMalloc(&d_attr_values, total_attrs * sizeof(int));
    if (err != cudaSuccess)
    {
        printf("ERROR: CUDA malloc failed (attr_values): %s\n", cudaGetErrorString(err));
        cudaFree(d_attr_ids);
        return 0;
    }

    err = cudaMalloc(&d_attr_counts, module_count * sizeof(int));
    if (err != cudaSuccess)
    {
        printf("ERROR: CUDA malloc failed(attr_counts): %s\n", cudaGetErrorString(err));
        cudaFree(d_attr_ids);
        cudaFree(d_attr_values);
        return 0;
    }

    err = cudaMalloc(&d_offsets, module_count * sizeof(int));
    if (err != cudaSuccess)
    {
        printf("ERROR: CUDA malloc failed(offsets): %s\n", cudaGetErrorString(err));
        cudaFree(d_attr_ids);
        cudaFree(d_attr_values);
        cudaFree(d_attr_counts);
        return 0;
    }

    if (target_count > 0)
    {
        err = cudaMalloc(&d_target_attrs, target_count * sizeof(int));
        if (err != cudaSuccess)
        {
            printf("ERROR: CUDA malloc failed(target_attrs): %s\n", cudaGetErrorString(err));
            goto cleanup;
        }
        err = cudaMemcpy(d_target_attrs, target_attrs, target_count * sizeof(int), cudaMemcpyHostToDevice);
        if (err != cudaSuccess)
        {
            printf("ERROR: CUDA memcpy failed(target_attrs): %s\n", cudaGetErrorString(err));
            goto cleanup;
        }
    }

    if (exclude_count > 0)
    {
        err = cudaMalloc(&d_exclude_attrs, exclude_count * sizeof(int));
        if (err != cudaSuccess)
        {
            printf("ERROR: CUDA malloc failed(exclude_attrs): %s\n", cudaGetErrorString(err));
            goto cleanup;
        }
        err = cudaMemcpy(d_exclude_attrs, exclude_attrs, exclude_count * sizeof(int), cudaMemcpyHostToDevice);
        if (err != cudaSuccess)
        {
            printf("ERROR: CUDA memcpy failed(exclude_attrs): %s\n", cudaGetErrorString(err));
            goto cleanup;
        }
    }

    if (min_attr_count > 0)
    {
        err = cudaMalloc(&d_min_attr_ids, min_attr_count * sizeof(int));
        if (err != cudaSuccess)
        {
            printf("ERROR: CUDA malloc failed(min_attr_ids): %s\n", cudaGetErrorString(err));
            goto cleanup;
        }
        err = cudaMemcpy(d_min_attr_ids, min_attr_ids, min_attr_count * sizeof(int), cudaMemcpyHostToDevice);
        if (err != cudaSuccess)
        {
            printf("ERROR: CUDA memcpy failed(min_attr_ids): %s\n", cudaGetErrorString(err));
            goto cleanup;
        }

        err = cudaMalloc(&d_min_attr_values, min_attr_count * sizeof(int));
        if (err != cudaSuccess)
        {
            printf("ERROR: CUDA malloc failed(min_attr_values): %s\n", cudaGetErrorString(err));
            goto cleanup;
        }
        err = cudaMemcpy(d_min_attr_values, min_attr_values, min_attr_count * sizeof(int), cudaMemcpyHostToDevice);
        if (err != cudaSuccess)
        {
            printf("ERROR: CUDA memcpy failed(min_attr_values): %s\n", cudaGetErrorString(err));
            goto cleanup;
        }
    }

    err = cudaMalloc(&d_scores, batch_size * sizeof(int));
    if (err != cudaSuccess)
    {
        printf("ERROR: CUDA malloc failed(scores): %s\n", cudaGetErrorString(err));
        goto cleanup;
    }

    err = cudaMalloc(&d_indices, batch_size * sizeof(long long));
    if (err != cudaSuccess)
    {
        printf("ERROR: CUDA malloc failed(indices): %s\n", cudaGetErrorString(err));
        goto cleanup;
    }

    err = cudaMemcpy(d_attr_ids, module_attr_ids, total_attrs * sizeof(int), cudaMemcpyHostToDevice);
    if (err != cudaSuccess)
    {
        printf("ERROR: CUDA memcpy failed(attr_ids): %s\n", cudaGetErrorString(err));
        goto cleanup;
    }

    err = cudaMemcpy(d_attr_values, module_attr_values, total_attrs * sizeof(int), cudaMemcpyHostToDevice);
    if (err != cudaSuccess)
    {
        printf("ERROR: CUDA memcpy failed(attr_values): %s\n", cudaGetErrorString(err));
        goto cleanup;
    }

    err = cudaMemcpy(d_attr_counts, module_attr_counts, module_count * sizeof(int), cudaMemcpyHostToDevice);
    if (err != cudaSuccess)
    {
        printf("ERROR: CUDA memcpy failed(attr_counts): %s\n", cudaGetErrorString(err));
        goto cleanup;
    }

    err = cudaMemcpy(d_offsets, module_offsets, module_count * sizeof(int), cudaMemcpyHostToDevice);
    if (err != cudaSuccess)
    {
        printf("ERROR: CUDA memcpy failed(offsets): %s\n", cudaGetErrorString(err));
        goto cleanup;
    }

    // 开始处理所有组合
    for (long long batch_start = 0; batch_start < total_combinations; batch_start += batch_size)
    {
        long long current_batch_size = min(batch_size, total_combinations - batch_start);

        // 创建当前结果内存
        err = cudaMemset(d_scores, 0, current_batch_size * sizeof(int));
        if (err != cudaSuccess)
        {
            printf("ERROR: CUDA memset failed(scores): %s\n", cudaGetErrorString(err));
            goto cleanup;
        }

        err = cudaMemset(d_indices, 0, current_batch_size * sizeof(long long));
        if (err != cudaSuccess)
        {
            printf("ERROR: CUDA memset failed(indices): %s\n", cudaGetErrorString(err));
            goto cleanup;
        }

        // 执行kernel
        {
            dim3 block(gpu_config.optimal_block_size);
            int grid_size = min(gpu_config.optimal_grid_size, (int)((current_batch_size + block.x - 1) / block.x));
            dim3 grid(grid_size);

            GpuEnumerationKernel<<<grid, block>>>(
                d_attr_ids, d_attr_values, d_attr_counts, d_offsets,
                module_count, batch_start, batch_start + current_batch_size,
                d_target_attrs, target_count,
                d_exclude_attrs, exclude_count,
                d_min_attr_ids, d_min_attr_values, min_attr_count,
                d_scores, d_indices);

            err = cudaGetLastError();
            if (err != cudaSuccess)
            {
                printf("ERROR: CUDA kernel launch failed: %s\n", cudaGetErrorString(err));
                goto cleanup;
            }

            err = cudaDeviceSynchronize();
            if (err != cudaSuccess)
            {
                printf("ERROR: CUDA kernel execution failed: %s\n", cudaGetErrorString(err));
                goto cleanup;
            }

            // 排序
            if (current_batch_size >= max_solutions)
            {
                GpuSortTopSolutions(d_scores, d_indices, (int)current_batch_size, max_solutions);
            }
        }

        // 获取当前批次Top解
        int results_to_transfer = min((long long)max_solutions, current_batch_size);

        std::vector<int> batch_scores(results_to_transfer);
        std::vector<long long> batch_indices(results_to_transfer);

        err = cudaMemcpy(batch_scores.data(), d_scores, results_to_transfer * sizeof(int), cudaMemcpyDeviceToHost);
        if (err != cudaSuccess)
        {
            printf("ERROR: CUDA result transfer failed(batch_scores): %s\n", cudaGetErrorString(err));
            printf("Debug: d_scores=%p, batch_scores.data()=%p, size=%d bytes\n",
                   d_scores, batch_scores.data(), results_to_transfer * (int)sizeof(int));
            goto cleanup;
        }

        err = cudaMemcpy(batch_indices.data(), d_indices, results_to_transfer * sizeof(long long), cudaMemcpyDeviceToHost);
        if (err != cudaSuccess)
        {
            printf("ERROR: CUDA result transfer failed(batch_indices): %s\n", cudaGetErrorString(err));
            goto cleanup;
        }

        // 合并当前批次结果到全局TOP
        for (int i = 0; i < results_to_transfer; ++i)
        {
            bool should_insert = false;
            int insert_pos = max_solutions;

            // 查找插入位置
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

        processed += current_batch_size;
    }

    for (int i = 0; i < max_solutions; ++i)
    {
        result_scores[i] = global_best_scores[i];
        result_indices[i] = global_best_indices[i];
    }

    // 清理GPU内存
cleanup:
    if (d_attr_ids)
        cudaFree(d_attr_ids);
    if (d_attr_values)
        cudaFree(d_attr_values);
    if (d_attr_counts)
        cudaFree(d_attr_counts);
    if (d_offsets)
        cudaFree(d_offsets);
    if (d_target_attrs)
        cudaFree(d_target_attrs);
    if (d_exclude_attrs)
        cudaFree(d_exclude_attrs);
    if (d_min_attr_ids)
        cudaFree(d_min_attr_ids);
    if (d_min_attr_values)
        cudaFree(d_min_attr_values);
    if (d_scores)
        cudaFree(d_scores);
    if (d_indices)
        cudaFree(d_indices);

    return (err == cudaSuccess) ? max_solutions : 0;
}
