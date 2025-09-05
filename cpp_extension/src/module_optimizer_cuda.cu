#include <cuda_runtime.h>
#include <device_launch_parameters.h>
#include <cstdio>
#include <thrust/device_vector.h>
#include <thrust/sort.h>
#include <thrust/copy.h>

// 用于判断是否支持CUDA加速
__global__ void test_kernel(int *data, int size)
{
    int idx = blockIdx.x * blockDim.x + threadIdx.x;
    if (idx < size)
    {
        data[idx] = idx * 2;
    }
}

// 用于判断是否支持CUDA加速
extern "C" int test_cuda_()
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
    test_kernel<<<grid, block>>>(d_data, size);

    err = cudaDeviceSynchronize();
    cudaFree(d_data);

    return (err == cudaSuccess) ? 1 : 0;
}