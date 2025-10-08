#include "module_optimizer.h"

#ifdef USE_OPENCL
#ifndef CL_TARGET_OPENCL_VERSION
#define CL_TARGET_OPENCL_VERSION 300
#endif
#include <CL/cl.h>
#include <cstdio>
#include <vector>
#include <string>
#include <algorithm>
#include <cstring>
#include <queue>
#include <limits>

struct GpuConfigOpenCL {
    size_t max_work_group_size;        // 最大工作组大小
    cl_uint compute_units;              // 计算单元数量
    cl_ulong global_memory;             // 全局内存大小
    size_t max_work_item_sizes[3];     // 最大工作项大小
    
    // 计算得出的优化参数
    size_t optimal_local_size;         // 优化的本地大小（block_size）
    size_t optimal_global_size;        // 优化的全局大小（grid_size * block_size）
    unsigned long long optimal_batch_size;  // 优化的批处理大小
};

static bool SelectDiscreteGpu(cl_platform_id &out_platform,
                              cl_device_id &out_device) {
    cl_uint num_platforms = 0;
    if (clGetPlatformIDs(0, nullptr, &num_platforms) != CL_SUCCESS || num_platforms == 0) {
        return false;
    }
    std::vector<cl_platform_id> platforms(num_platforms);
    if (clGetPlatformIDs(num_platforms, platforms.data(), nullptr) != CL_SUCCESS) {
        return false;
    }

    for (auto platform : platforms) {
        cl_uint num_devices = 0;
        if (clGetDeviceIDs(platform, CL_DEVICE_TYPE_GPU, 0, nullptr, &num_devices) != CL_SUCCESS || num_devices == 0) {
            continue;
        }
        std::vector<cl_device_id> devices(num_devices);
        if (clGetDeviceIDs(platform, CL_DEVICE_TYPE_GPU, num_devices, devices.data(), nullptr) != CL_SUCCESS) {
            continue;
        }
        for (auto dev : devices) {
            cl_bool unified = CL_FALSE;
            clGetDeviceInfo(dev, CL_DEVICE_HOST_UNIFIED_MEMORY, sizeof(unified), &unified, nullptr);
            // 仅选择独显
            if (unified == CL_FALSE) {
                out_platform = platform;
                out_device = dev;
                return true;
            }
        }
    }
    return false;
}

extern "C" int TestOpenCL() {
    cl_platform_id platform = nullptr;
    cl_device_id device = nullptr;
    return SelectDiscreteGpu(platform, device) ? 1 : 0;
}

static bool GetGpuConfigOpenCL(cl_device_id device, GpuConfigOpenCL* config) {
    if (!device || !config) return false;
    
    cl_int err = CL_SUCCESS;
    
    // 查询最大工作组大小
    err = clGetDeviceInfo(device, CL_DEVICE_MAX_WORK_GROUP_SIZE, 
                          sizeof(size_t), &config->max_work_group_size, nullptr);
    if (err != CL_SUCCESS) return false;
    
    // 查询计算单元数量
    err = clGetDeviceInfo(device, CL_DEVICE_MAX_COMPUTE_UNITS, 
                          sizeof(cl_uint), &config->compute_units, nullptr);
    if (err != CL_SUCCESS) return false;
    
    // 查询全局内存大小
    err = clGetDeviceInfo(device, CL_DEVICE_GLOBAL_MEM_SIZE, 
                          sizeof(cl_ulong), &config->global_memory, nullptr);
    if (err != CL_SUCCESS) return false;
    
    // 查询最大工作项大小
    err = clGetDeviceInfo(device, CL_DEVICE_MAX_WORK_ITEM_SIZES, 
                          sizeof(size_t) * 3, config->max_work_item_sizes, nullptr);
    if (err != CL_SUCCESS) return false;
    
    return true;
}

static void CalculateOptimalParamsOpenCL(GpuConfigOpenCL* config, unsigned long long total_combinations) {
    if (!config) return;
    
    // 设置优化的本地工作组大小
    config->optimal_local_size = 512;
    
    // 确保不超过硬件限制
    if (config->optimal_local_size > config->max_work_group_size) {
        config->optimal_local_size = config->max_work_group_size;
    }
    
    // 计算优化的全局工作大小
    size_t estimated_max_work_groups = config->compute_units * 16;
    size_t max_global_threads = estimated_max_work_groups * 2 * config->optimal_local_size;
    
    // 基于实际工作负载调整
    if (total_combinations < max_global_threads) {
        config->optimal_global_size = ((total_combinations + config->optimal_local_size - 1) 
                                       / config->optimal_local_size) * config->optimal_local_size;
    } else {
        config->optimal_global_size = max_global_threads;
    }
    
    // 计算优化的批处理大小
    size_t available_memory = (size_t)(config->global_memory * 0.5);
    unsigned long long memory_limited_batch = available_memory / (sizeof(int) + sizeof(unsigned long long));
    
    // 基于计算能力的批处理大小
    unsigned long long compute_limited_batch = max_global_threads * 3000ULL;
    
    // 取较小值，但至少 10 万，最大 2250 万
    config->optimal_batch_size = memory_limited_batch < compute_limited_batch ? 
                                  memory_limited_batch : compute_limited_batch;
    if (config->optimal_batch_size < 100000ULL) {
        config->optimal_batch_size = 100000ULL;
    }
    if (config->optimal_batch_size > 22500000ULL) {
        config->optimal_batch_size = 22500000ULL;
    }
}

extern "C" int GpuStrategyEnumerationOpenCL(
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
    long long *result_indices) {
    cl_platform_id platform = nullptr;
    cl_device_id device = nullptr;
    if (!SelectDiscreteGpu(platform, device)) {
        return 0;
    }

    cl_int err = CL_SUCCESS;
    cl_context ctx = clCreateContext(nullptr, 1, &device, nullptr, nullptr, &err);
    if (!ctx || err != CL_SUCCESS) return 0;
    cl_command_queue q = clCreateCommandQueueWithProperties(ctx, device, nullptr, &err);
    if (!q || err != CL_SUCCESS) { clReleaseContext(ctx); return 0; }

    // 获取 GPU 配置
    GpuConfigOpenCL gpu_config;
    if (!GetGpuConfigOpenCL(device, &gpu_config)) {
        clReleaseCommandQueue(q);
        clReleaseContext(ctx);
        return 0;
    }

    auto comb_count = [](unsigned long long n, unsigned long long r) -> unsigned long long {
        if (r > n) return 0ULL; 
        if (r == 0ULL || r == n) return 1ULL; 
        if (r > n - r) r = n - r;
        unsigned long long res = 1ULL; 
        for (unsigned long long i = 0; i < r; ++i) res = (res * (n - i)) / (i + 1ULL); 
        return res;
    };
    unsigned long long total_combinations = comb_count((unsigned long long)module_count, 4ULL);
    CalculateOptimalParamsOpenCL(&gpu_config, total_combinations);

    // 打印配置信息
    printf("OpenCL GPU Configuration:\n");
    printf("  Compute Units: %u\n", gpu_config.compute_units);
    printf("  Max Work Group Size: %zu\n", gpu_config.max_work_group_size);
    printf("  Global Memory: %.1f MB\n", (double)gpu_config.global_memory / (1024 * 1024));
    printf("Optimal Parameters:\n");
    printf("  Local Size: %zu\n", gpu_config.optimal_local_size);
    printf("  Global Size: %zu\n", gpu_config.optimal_global_size);
    printf("  Batch Size: %llu\n", gpu_config.optimal_batch_size);

    const char *kernel_src = R"CLC(
#define RADIX_BINS 256
__constant int ATTR_THRESHOLDS[6] = {1,4,8,12,16,20};
__constant int BASIC_POWER_VALUES[6] = {7,14,29,44,167,254};
__constant int SPECIAL_POWER_VALUES[6] = {14,29,59,89,298,448};
__constant int SPECIAL_ATTRS[8] = {2104,2105,2204,2205,2404,2405,2406,2304};
__constant int TOTAL_ATTR_POWER_VALUES[121] = {
    0,5,11,17,23,29,34,40,46,52,58,64,69,75,81,87,93,99,104,110,116,
    122,128,133,139,145,151,157,163,168,174,180,186,192,198,203,209,215,221,227,233,
    238,244,250,256,262,267,273,279,285,291,297,302,308,314,320,326,332,337,343,349,
    355,361,366,372,378,384,390,396,401,407,413,419,425,431,436,442,448,454,460,466,
    471,477,483,489,495,500,506,512,518,524,530,535,541,547,553,559,565,570,576,582,
    588,594,599,605,611,617,623,629,634,640,646,652,658,664,669,675,681,687,693,699
};

inline int is_special_id(int id) {
    #pragma unroll
    for (int i = 0; i < 8; ++i) if (SPECIAL_ATTRS[i] == id) return 1; return 0;
}
inline int in_set(__global const int * restrict arr, int n, int v) {
    for (int i = 0; i < n; ++i) if (arr[i] == v) return 1; return 0;
}

ulong comb_count(ulong n, ulong r) {
    if (r > n) return 0UL; if (r == 0UL || r == n) return 1UL; if (r > n - r) r = n - r;
    ulong res = 1UL; for (ulong i = 0; i < r; ++i) { res = (res * (n - i)) / (i + 1UL); } return res;
}

void get_combination_by_index(uint n, uint r, ulong idx, uint comb_out[4]) {
    ulong remaining = idx;
    for (uint i = 0; i < r; ++i) {
        uint start = (i == 0U) ? 0U : (comb_out[i-1] + 1U);
        for (uint j = start; j < n; ++j) {
            ulong after = comb_count((ulong)(n - j - 1U), (ulong)(r - i - 1U));
            if (remaining < after) { comb_out[i] = j; break; }
            remaining -= after;
        }
    }
}

int next_combination(uint n, uint r, uint comb[4]) {
    for (int pos = (int)r - 1; pos >= 0; --pos) {
        uint limit = n - r + (uint)pos;
        if (comb[pos] < limit) {
            ++comb[pos];
            for (uint k = (uint)pos + 1U; k < r; ++k) {
                comb[k] = comb[k - 1U] + 1U;
            }
            return 1;
        }
    }
    return 0;
}

__kernel void score_range(
    __global const int * restrict module_attr_ids,
    __global const int * restrict module_attr_values,
    __global const int * restrict module_attr_counts,
    __global const int * restrict module_offsets,
    int module_count,
    int total_attrs,
    __global const int * restrict target_attrs,
    int target_count,
    __global const int * restrict exclude_attrs,
    int exclude_count,
    __global const int * restrict min_attr_ids,
    __global const int * restrict min_attr_values,
    int min_attr_count,
    ulong range_start,
    ulong range_len,
    __global int * restrict out_scores,
    __global ulong * restrict out_indices) {
    ulong gid = (ulong)get_global_id(0);
    ulong total_threads = (ulong)get_global_size(0);
    
    ulong total_work = range_len;
    if (total_work == 0) return;
    
    ulong work_per_thread = (total_work + total_threads - 1) / total_threads;
    ulong seg_start = range_start + gid * work_per_thread;
    if (seg_start >= range_start + range_len) return;
    ulong seg_end = seg_start + work_per_thread;
    if (seg_end > range_start + range_len) seg_end = range_start + range_len;
    
    uint comb[4];
    get_combination_by_index((uint)module_count, 4U, seg_start, comb);
    
    for (ulong combo_idx = seg_start; combo_idx < seg_end; ++combo_idx) {
        ulong gid_local = combo_idx - range_start;

        int attr_ids[20];
        int attr_vals[20];
        int attr_cnt = 0;
        int total_attr_value = 0;
        
        #pragma unroll
        for (int t = 0; t < 4; ++t) {
            int mi = (int)comb[t];
            int off = module_offsets[mi];
            int cnt = module_attr_counts[mi];
            
            #pragma unroll
            for (int k = 0; k < 3; ++k) {
                if (k < cnt) {
                    int aid = module_attr_ids[off + k];
                    int aval = module_attr_values[off + k];
                    total_attr_value += aval;
                    int found = -1;
                    #pragma unroll
                    for (int u = 0; u < 12; ++u) { 
                        if (u < attr_cnt && attr_ids[u] == aid) { found = u; break; } 
                    }
                    if (found >= 0) { attr_vals[found] += aval; }
                    else if (attr_cnt < 12) { attr_ids[attr_cnt] = aid; attr_vals[attr_cnt] = aval; attr_cnt++; }
                }
            }
        }

        int pass_min_filter = 1;
        for (int m = 0; m < min_attr_count; ++m) {
            int req_id = min_attr_ids[m];
            int req_v = min_attr_values[m];
            int sum_v = 0, ok = 0;
            #pragma unroll
            for (int u = 0; u < 12; ++u) {
                if (u < attr_cnt && attr_ids[u] == req_id) { sum_v = attr_vals[u]; ok = 1; break; }
            }
            if (!ok || sum_v < req_v) {
                pass_min_filter = 0;
                break;
            }
        }
        
        if (!pass_min_filter) {
            out_scores[gid_local] = 0;
            out_indices[gid_local] = 0UL;
            if (!next_combination((uint)module_count, 4U, comb)) {
                break;
            }
            continue;
        }

        int threshold_power = 0;
        #pragma unroll
        for (int i = 0; i < 12; ++i) {
            if (i < attr_cnt) {
                int aval = attr_vals[i];
                int aid = attr_ids[i];
                int lvl = 0;
                #pragma unroll
                for (int L = 0; L < 6; ++L) { if (aval >= ATTR_THRESHOLDS[L]) lvl = L + 1; else break; }
                if (lvl > 0) {
                    int base = is_special_id(aid) ? SPECIAL_POWER_VALUES[lvl - 1] : BASIC_POWER_VALUES[lvl - 1];
                    if (target_count > 0 && in_set(target_attrs, target_count, aid)) threshold_power += base * 2;
                    else if (exclude_count > 0 && in_set(exclude_attrs, exclude_count, aid)) threshold_power += 0;
                    else threshold_power += base;
                }
            }
        }
        int idx_total = total_attr_value > 120 ? 120 : total_attr_value;
        int total_power = threshold_power + TOTAL_ATTR_POWER_VALUES[idx_total];
        out_scores[gid_local] = total_power;
        out_indices[gid_local] = ((ulong)comb[0]) | ((ulong)comb[1] << 16) | ((ulong)comb[2] << 32) | ((ulong)comb[3] << 48);
        
        if (!next_combination((uint)module_count, 4U, comb)) {
            break;
        }
    }
}

__kernel void histogram_byte_radix(
    __global const int * restrict scores,
    ulong n,
    uint prefix_mask,
    uint prefix_value,
    int byte_idx,
    __global uint * restrict g_hist,
    __local uint *s_hist) {
    size_t lid = get_local_id(0);
    size_t lsz = get_local_size(0);
    size_t gid = get_global_id(0);
    size_t gsz = get_global_size(0);
    
    for (uint i = lid; i < RADIX_BINS; i += lsz) {
        s_hist[i] = 0U;
    }
    barrier(CLK_LOCAL_MEM_FENCE);
    
    int shift = byte_idx * 8;
    for (ulong idx = gid; idx < n; idx += gsz) {
        uint s = (uint)scores[idx];
        if ((s & prefix_mask) == prefix_value) {
            uint bucket = (s >> shift) & 0xFFU;
            atomic_inc((volatile __local uint *)&s_hist[bucket]);
        }
    }
    barrier(CLK_LOCAL_MEM_FENCE);
    
    for (uint i = lid; i < RADIX_BINS; i += lsz) {
        if (s_hist[i] > 0) {
            atomic_add((volatile __global uint *)&g_hist[i], s_hist[i]);
        }
    }
}

__kernel void flag_scores_by_threshold(
    __global const int * restrict scores,
    ulong n,
    int threshold,
    __global uchar * restrict flags) {
    size_t gid = get_global_id(0);
    size_t gsz = get_global_size(0);
    for (ulong i = gid; i < n; i += gsz) {
        int s = scores[i];
        flags[i] = (uchar)((s >= threshold) ? 1 : 0);
    }
}

__kernel void compact_selected(
    __global const int * restrict scores,
    __global const ulong * restrict indices,
    __global const uchar * restrict flags,
    ulong n,
    __global int * restrict out_scores,
    __global ulong * restrict out_indices,
    __global uint * restrict out_count) {
    size_t gid = get_global_id(0);
    size_t gsz = get_global_size(0);
    for (ulong i = gid; i < n; i += gsz) {
        if (flags[i]) {
            uint pos = (uint)atomic_inc((volatile __global int *)out_count);
            out_scores[pos] = scores[i];
            out_indices[pos] = indices[i];
        }
    }
}
    )CLC";

    const char *srcs[] = { kernel_src };
    size_t lens[] = { std::strlen(kernel_src) };
    cl_program prog = clCreateProgramWithSource(ctx, 1, srcs, lens, &err);
    if (!prog || err != CL_SUCCESS) { 
        clReleaseCommandQueue(q); 
        clReleaseContext(ctx); 
        return 0; 
    }
    err = clBuildProgram(prog, 1, &device, "-cl-std=CL3.0 -cl-mad-enable -cl-fast-relaxed-math -cl-finite-math-only", nullptr, nullptr);
    if (err != CL_SUCCESS) {
        size_t log_size = 0; clGetProgramBuildInfo(prog, device, CL_PROGRAM_BUILD_LOG, 0, nullptr, &log_size);
        std::vector<char> log(log_size + 1, 0);
        clGetProgramBuildInfo(prog, device, CL_PROGRAM_BUILD_LOG, log_size, log.data(), nullptr);
        clReleaseProgram(prog); 
        clReleaseCommandQueue(q); 
        clReleaseContext(ctx);
        return 0;
    }
    cl_kernel kernel = clCreateKernel(prog, "score_range", &err);
    if (!kernel || err != CL_SUCCESS) { 
        clReleaseProgram(prog); 
        clReleaseCommandQueue(q); 
        clReleaseContext(ctx); 
        return 0; 
    }
    cl_kernel k_hist_radix = clCreateKernel(prog, "histogram_byte_radix", &err);
    if (!k_hist_radix || err != CL_SUCCESS) { 
        clReleaseKernel(kernel); 
        clReleaseProgram(prog); 
        clReleaseCommandQueue(q); 
        clReleaseContext(ctx); 
        return 0; 
    }
    cl_kernel k_flag = clCreateKernel(prog, "flag_scores_by_threshold", &err);
    if (!k_flag || err != CL_SUCCESS) { 
        clReleaseKernel(k_hist_radix); 
        clReleaseKernel(kernel); 
        clReleaseProgram(prog); 
        clReleaseCommandQueue(q); 
        clReleaseContext(ctx); 
        return 0; 
    }
    cl_kernel k_compact = clCreateKernel(prog, "compact_selected", &err);
    if (!k_compact || err != CL_SUCCESS) { 
        clReleaseKernel(k_flag); 
        clReleaseKernel(k_hist_radix); 
        clReleaseKernel(kernel); 
        clReleaseProgram(prog); 
        clReleaseCommandQueue(q); 
        clReleaseContext(ctx); 
        return 0; 
    }

    cl_mem d_attr_ids = clCreateBuffer(ctx, CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR, sizeof(int) * total_attrs, (void*)module_attr_ids, &err);
    cl_mem d_attr_vals = clCreateBuffer(ctx, CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR, sizeof(int) * total_attrs, (void*)module_attr_values, &err);
    cl_mem d_attr_counts = clCreateBuffer(ctx, CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR, sizeof(int) * module_count, (void*)module_attr_counts, &err);
    cl_mem d_offsets = clCreateBuffer(ctx, CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR, sizeof(int) * module_count, (void*)module_offsets, &err);
    cl_mem d_targets = target_count > 0 ? clCreateBuffer(ctx, CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR, sizeof(int) * target_count, (void*)target_attrs, &err) : nullptr;
    cl_mem d_excludes = exclude_count > 0 ? clCreateBuffer(ctx, CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR, sizeof(int) * exclude_count, (void*)exclude_attrs, &err) : nullptr;
    cl_mem d_min_ids = min_attr_count > 0 ? clCreateBuffer(ctx, CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR, sizeof(int) * min_attr_count, (void*)min_attr_ids, &err) : nullptr;
    cl_mem d_min_vals = min_attr_count > 0 ? clCreateBuffer(ctx, CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR, sizeof(int) * min_attr_count, (void*)min_attr_values, &err) : nullptr;

    if (total_combinations == 0ULL) {
        if (d_min_vals) clReleaseMemObject(d_min_vals);
        if (d_min_ids) clReleaseMemObject(d_min_ids);
        if (d_excludes) clReleaseMemObject(d_excludes);
        if (d_targets) clReleaseMemObject(d_targets);
        clReleaseMemObject(d_offsets); 
        clReleaseMemObject(d_attr_counts);
        clReleaseMemObject(d_attr_vals); 
        clReleaseMemObject(d_attr_ids);
        clReleaseKernel(kernel); 
        clReleaseProgram(prog); 
        clReleaseCommandQueue(q); 
        clReleaseContext(ctx);
        return 0;
    }

    struct Item { int score; unsigned long long idx; bool operator<(const Item& o) const { return score > o.score; } };
    std::priority_queue<Item> topk;

    unsigned long long processed = 0ULL;
    while (processed < total_combinations) {
        unsigned long long batch = std::min(gpu_config.optimal_batch_size, total_combinations - processed);
        size_t outN = (size_t)batch;

        cl_mem d_scores = clCreateBuffer(ctx, CL_MEM_READ_WRITE, sizeof(int) * outN, nullptr, &err);
        cl_mem d_indices = clCreateBuffer(ctx, CL_MEM_READ_WRITE, sizeof(unsigned long long) * outN, nullptr, &err);

        int arg = 0;
        clSetKernelArg(kernel, arg++, sizeof(cl_mem), &d_attr_ids);
        clSetKernelArg(kernel, arg++, sizeof(cl_mem), &d_attr_vals);
        clSetKernelArg(kernel, arg++, sizeof(cl_mem), &d_attr_counts);
        clSetKernelArg(kernel, arg++, sizeof(cl_mem), &d_offsets);
        clSetKernelArg(kernel, arg++, sizeof(int), &module_count);
        clSetKernelArg(kernel, arg++, sizeof(int), &total_attrs);
        clSetKernelArg(kernel, arg++, sizeof(cl_mem), &d_targets);
        clSetKernelArg(kernel, arg++, sizeof(int), &target_count);
        clSetKernelArg(kernel, arg++, sizeof(cl_mem), &d_excludes);
        clSetKernelArg(kernel, arg++, sizeof(int), &exclude_count);
        clSetKernelArg(kernel, arg++, sizeof(cl_mem), &d_min_ids);
        clSetKernelArg(kernel, arg++, sizeof(cl_mem), &d_min_vals);
        clSetKernelArg(kernel, arg++, sizeof(int), &min_attr_count);
        unsigned long long range_start = processed;
        clSetKernelArg(kernel, arg++, sizeof(unsigned long long), &range_start);
        cl_ulong range_len = (cl_ulong)batch;
        clSetKernelArg(kernel, arg++, sizeof(unsigned long long), &range_len);
        clSetKernelArg(kernel, arg++, sizeof(cl_mem), &d_scores);
        clSetKernelArg(kernel, arg++, sizeof(cl_mem), &d_indices);

        size_t lsz = gpu_config.optimal_local_size;
        size_t target_threads = gpu_config.optimal_global_size;
        if (target_threads > (size_t)batch) {
            target_threads = ((size_t)batch + lsz - 1) / lsz * lsz;
        }
        size_t gsz = target_threads;
        
        err = clEnqueueNDRangeKernel(q, kernel, 1, nullptr, &gsz, &lsz, 0, nullptr, nullptr);
        if (err != CL_SUCCESS) { 
            clReleaseMemObject(d_indices); 
            clReleaseMemObject(d_scores); 
            break; 
        }
        clFinish(q);

        cl_ulong n64 = (cl_ulong)outN;
        cl_mem d_hist = clCreateBuffer(ctx, CL_MEM_READ_WRITE, sizeof(cl_uint) * 256, nullptr, &err);
        if (!d_hist || err != CL_SUCCESS) { 
            clReleaseMemObject(d_indices); 
            clReleaseMemObject(d_scores); 
            break; 
        }
        
        cl_uint prefix_mask = 0U;
        cl_uint prefix_value = 0U;
        int k_needed = max_solutions;
        
        for (int byte_idx = 3; byte_idx >= 0; --byte_idx) {
            cl_uint zero_u = 0U;
            clEnqueueFillBuffer(q, d_hist, &zero_u, sizeof(cl_uint), 0, sizeof(cl_uint) * 256, 0, nullptr, nullptr);
            
            int harg = 0;
            clSetKernelArg(k_hist_radix, harg++, sizeof(cl_mem), &d_scores);
            clSetKernelArg(k_hist_radix, harg++, sizeof(cl_ulong), &n64);
            clSetKernelArg(k_hist_radix, harg++, sizeof(cl_uint), &prefix_mask);
            clSetKernelArg(k_hist_radix, harg++, sizeof(cl_uint), &prefix_value);
            clSetKernelArg(k_hist_radix, harg++, sizeof(int), &byte_idx);
            clSetKernelArg(k_hist_radix, harg++, sizeof(cl_mem), &d_hist);
            clSetKernelArg(k_hist_radix, harg++, sizeof(cl_uint) * 256, nullptr);
            
            err = clEnqueueNDRangeKernel(q, k_hist_radix, 1, nullptr, &gsz, &lsz, 0, nullptr, nullptr);
            if (err != CL_SUCCESS) { 
                clReleaseMemObject(d_hist); 
                clReleaseMemObject(d_indices); 
                clReleaseMemObject(d_scores); 
                break; 
            }
            clFinish(q);
            
            cl_uint h_hist[256];
            clEnqueueReadBuffer(q, d_hist, CL_TRUE, 0, sizeof(cl_uint) * 256, h_hist, 0, nullptr, nullptr);
            clFinish(q);
            
            cl_uint acc = 0U;
            int chosen_bucket = 0;
            for (int b = 255; b >= 0; --b) {
                acc += h_hist[b];
                if (acc >= (cl_uint)k_needed) {
                    chosen_bucket = b;
                    break;
                }
            }
            
            cl_uint bigger_acc = acc - h_hist[chosen_bucket];
            k_needed -= (int)bigger_acc;
            
            cl_uint mask_byte = 0xFFU << (byte_idx * 8);
            prefix_mask |= mask_byte;
            prefix_value |= ((cl_uint)chosen_bucket << (byte_idx * 8));
        }
        
        int threshold_value = (int)prefix_value;
        clReleaseMemObject(d_hist);

        cl_mem d_flags = clCreateBuffer(ctx, CL_MEM_READ_WRITE, sizeof(unsigned char) * outN, nullptr, &err);
        if (!d_flags || err != CL_SUCCESS) { 
            clReleaseMemObject(d_indices); 
            clReleaseMemObject(d_scores);
            break; 
        }
        int farg = 0;
        clSetKernelArg(k_flag, farg++, sizeof(cl_mem), &d_scores);
        clSetKernelArg(k_flag, farg++, sizeof(cl_ulong), &n64);
        clSetKernelArg(k_flag, farg++, sizeof(int), &threshold_value);
        clSetKernelArg(k_flag, farg++, sizeof(cl_mem), &d_flags);
        err = clEnqueueNDRangeKernel(q, k_flag, 1, nullptr, &gsz, &lsz, 0, nullptr, nullptr);
        if (err != CL_SUCCESS) { 
            clReleaseMemObject(d_flags); 
            clReleaseMemObject(d_indices); 
            clReleaseMemObject(d_scores); 
            break; 
        }
        clFinish(q);

        cl_mem d_selected_count = clCreateBuffer(ctx, CL_MEM_READ_WRITE, sizeof(cl_uint), nullptr, &err);
        cl_uint zero_u = 0;
        clEnqueueWriteBuffer(q, d_selected_count, CL_TRUE, 0, sizeof(cl_uint), &zero_u, 0, nullptr, nullptr);
        cl_mem d_comp_scores = clCreateBuffer(ctx, CL_MEM_READ_WRITE, sizeof(int) * outN, nullptr, &err);
        cl_mem d_comp_indices = clCreateBuffer(ctx, CL_MEM_READ_WRITE, sizeof(unsigned long long) * outN, nullptr, &err);
        if (!d_comp_scores || !d_comp_indices || err != CL_SUCCESS) {
            if (d_comp_indices) clReleaseMemObject(d_comp_indices);
            if (d_comp_scores) clReleaseMemObject(d_comp_scores);
            clReleaseMemObject(d_selected_count);
            clReleaseMemObject(d_flags);
            clReleaseMemObject(d_indices);
            clReleaseMemObject(d_scores);
            break;
        }
        int carg = 0;
        clSetKernelArg(k_compact, carg++, sizeof(cl_mem), &d_scores);
        clSetKernelArg(k_compact, carg++, sizeof(cl_mem), &d_indices);
        clSetKernelArg(k_compact, carg++, sizeof(cl_mem), &d_flags);
        clSetKernelArg(k_compact, carg++, sizeof(cl_ulong), &n64);
        clSetKernelArg(k_compact, carg++, sizeof(cl_mem), &d_comp_scores);
        clSetKernelArg(k_compact, carg++, sizeof(cl_mem), &d_comp_indices);
        clSetKernelArg(k_compact, carg++, sizeof(cl_mem), &d_selected_count);
        err = clEnqueueNDRangeKernel(q, k_compact, 1, nullptr, &gsz, &lsz, 0, nullptr, nullptr);
        if (err != CL_SUCCESS) {
            clReleaseMemObject(d_comp_indices);
            clReleaseMemObject(d_comp_scores);
            clReleaseMemObject(d_selected_count);
            clReleaseMemObject(d_flags);
            clReleaseMemObject(d_indices);
            clReleaseMemObject(d_scores);
            break;
        }
        clFinish(q);

        cl_uint h_selected = 0;
        clEnqueueReadBuffer(q, d_selected_count, CL_TRUE, 0, sizeof(cl_uint), &h_selected, 0, nullptr, nullptr);
        clFinish(q);

        if (h_selected > 0) {
            size_t selN = (size_t)h_selected;
            std::vector<int> h_scores_sel(selN);
            std::vector<unsigned long long> h_indices_sel(selN);
            clEnqueueReadBuffer(q, d_comp_scores, CL_TRUE, 0, sizeof(int) * selN, h_scores_sel.data(), 0, nullptr, nullptr);
            clEnqueueReadBuffer(q, d_comp_indices, CL_TRUE, 0, sizeof(unsigned long long) * selN, h_indices_sel.data(), 0, nullptr, nullptr);
            clFinish(q);

            for (size_t i = 0; i < selN; ++i) {
                int sc = h_scores_sel[i];
                if (sc < 0) continue;
                if (topk.size() < (size_t)max_solutions) { 
                    topk.push(Item{sc, h_indices_sel[i]}); 
                }
                else if (sc > topk.top().score) { 
                    topk.pop(); 
                    topk.push(Item{sc, h_indices_sel[i]});
                }
            }
        }

        clReleaseMemObject(d_comp_indices);
        clReleaseMemObject(d_comp_scores);
        clReleaseMemObject(d_selected_count);
        clReleaseMemObject(d_flags);
        clReleaseMemObject(d_indices);
        clReleaseMemObject(d_scores);

        processed += batch;
    }

    std::vector<Item> items; items.reserve(topk.size());
    while (!topk.empty()) { items.push_back(topk.top()); topk.pop(); }
    std::sort(items.begin(), items.end(), [](const Item& a, const Item& b){ return a.score > b.score; });
    int out_count = (int)std::min(items.size(), (size_t)max_solutions);
    for (int i = 0; i < out_count; ++i) { 
        result_scores[i] = items[i].score; 
        result_indices[i] = (long long)items[i].idx; 
    }

    if (d_min_vals) clReleaseMemObject(d_min_vals);
    if (d_min_ids) clReleaseMemObject(d_min_ids);
    if (d_excludes) clReleaseMemObject(d_excludes);
    if (d_targets) clReleaseMemObject(d_targets);
    clReleaseMemObject(d_offsets); 
    clReleaseMemObject(d_attr_counts);
    clReleaseMemObject(d_attr_vals); 
    clReleaseMemObject(d_attr_ids);
    clReleaseKernel(k_compact); 
    clReleaseKernel(k_flag); 
    clReleaseKernel(k_hist_radix); 
    clReleaseKernel(kernel);
    clReleaseProgram(prog); 
    clReleaseCommandQueue(q); 
    clReleaseContext(ctx);
    return out_count;
}

#endif

std::vector<ModuleSolution> ModuleOptimizerCpp::StrategyEnumerationOpenCL(
    const std::vector<ModuleInfo>& modules,
    const std::unordered_set<int>& target_attributes,
    const std::unordered_set<int>& exclude_attributes,
    const std::unordered_map<int, int>& min_attr_sum_requirements,
    int max_solutions,
    int max_workers) {
#ifdef USE_OPENCL
    if (!TestOpenCL()) {
        printf("OpenCL not available, using CPU optimized version\n");
        return StrategyEnumeration(modules, target_attributes, exclude_attributes,
                                   min_attr_sum_requirements, max_solutions, max_workers);
    }

    printf("OpenCL GPU acceleration enabled - all calculations performed on GPU\n");

    std::vector<int> all_attr_ids;
    std::vector<int> all_attr_values;
    std::vector<int> module_attr_counts;
    std::vector<int> module_offsets;

    size_t current_offset = 0;
    for (const auto& module : modules) {
        module_offsets.push_back(static_cast<int>(current_offset));
        module_attr_counts.push_back(static_cast<int>(module.parts.size()));
        for (const auto& part : module.parts) {
            all_attr_ids.push_back(part.id);
            all_attr_values.push_back(part.value);
        }
        current_offset += module.parts.size();
    }

    std::vector<int> target_attrs_vec(target_attributes.begin(), target_attributes.end());
    std::vector<int> exclude_attrs_vec(exclude_attributes.begin(), exclude_attributes.end());
    std::vector<int> min_attr_ids;
    std::vector<int> min_attr_values;
    for (const auto& kv : min_attr_sum_requirements) {
        min_attr_ids.push_back(kv.first);
        min_attr_values.push_back(kv.second);
    }

    std::vector<int> gpu_scores(max_solutions);
    std::vector<long long> gpu_indices(max_solutions);

    int gpu_result_count = 0;
#ifdef USE_OPENCL
    gpu_result_count = GpuStrategyEnumerationOpenCL(
        all_attr_ids.data(),
        all_attr_values.data(),
        module_attr_counts.data(),
        module_offsets.data(),
        static_cast<int>(modules.size()),
        static_cast<int>(all_attr_ids.size()),
        target_attrs_vec.empty() ? nullptr : target_attrs_vec.data(),
        static_cast<int>(target_attrs_vec.size()),
        exclude_attrs_vec.empty() ? nullptr : exclude_attrs_vec.data(),
        static_cast<int>(exclude_attrs_vec.size()),
        min_attr_ids.empty() ? nullptr : min_attr_ids.data(),
        min_attr_values.empty() ? nullptr : min_attr_values.data(),
        static_cast<int>(min_attr_ids.size()),
        max_solutions,
        gpu_scores.data(),
        gpu_indices.data());
#endif

    std::vector<ModuleSolution> final_solutions;
    final_solutions.reserve(static_cast<size_t>(gpu_result_count));
    for (int i = 0; i < gpu_result_count; ++i) {
        long long packed = gpu_indices[i];
        std::vector<ModuleInfo> solution_modules;
        solution_modules.reserve(4);
        for (int j = 0; j < 4; ++j) {
            size_t module_idx = static_cast<size_t>((packed >> (j * 16)) & 0xFFFF);
            if (module_idx < modules.size()) {
                solution_modules.push_back(modules[module_idx]);
            }
        }
        auto result = CalculateCombatPower(solution_modules);
        final_solutions.emplace_back(solution_modules, gpu_scores[i], result.second);
    }
    return final_solutions;
#else
    (void)modules; (void)target_attributes; (void)exclude_attributes; (void)min_attr_sum_requirements; (void)max_solutions; (void)max_workers;
    return StrategyEnumeration(modules, target_attributes, exclude_attributes,
                               min_attr_sum_requirements, max_solutions, max_workers);
#endif
}


