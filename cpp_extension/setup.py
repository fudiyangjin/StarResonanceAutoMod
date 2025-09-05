import os
import sys
import subprocess
from setuptools import setup, Extension
from pybind11.setup_helpers import Pybind11Extension
import pybind11

def find_cuda():
    """查找CUDA安装路径"""
    # 查找nvcc编译器
    try:
        result = subprocess.run(['nvcc', '--version'], capture_output=True, text=True)
        if result.returncode == 0:
            print("✅ 找到CUDA编译器")
            
            # 获取CUDA路径
            cuda_paths = [
                os.environ.get('CUDA_HOME'),
                os.environ.get('CUDA_PATH'),
                r'C:\Program Files\NVIDIA GPU Computing Toolkit\CUDA\v12.6',
                r'C:\Program Files\NVIDIA GPU Computing Toolkit\CUDA\v11.6'
            ]
            
            cuda_home = None
            for path in cuda_paths:
                if path and os.path.exists(path):
                    cuda_home = path
                    break
            
            if os.path.exists(cuda_home):
                print(f"✅ CUDA路径: {cuda_home}")
                return cuda_home
            else:
                print("❌ 未找到CUDA安装路径")
                return None
                
    except FileNotFoundError:
        print("❌ 未找到nvcc编译器")
        return None

def compile_cuda_code(cuda_home):
    """编译CUDA代码为目标文件"""
    try:
        # 查找Visual Studio环境
        vs_vars_paths = [
            r"C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat",
            r"C:\Program Files (x86)\Microsoft Visual Studio\2019\Community\VC\Auxiliary\Build\vcvars64.bat",
            r"C:\Program Files\Microsoft Visual Studio\2019\Community\VC\Auxiliary\Build\vcvars64.bat"
        ]
        
        vs_vars = None
        for path in vs_vars_paths:
            if os.path.exists(path):
                vs_vars = path
                break
        
        if vs_vars is None:
            print("❌ 未找到Visual Studio环境脚本")
            return False

        cuda_files = [
            ("src/module_optimizer_cuda.cu", "src/module_optimizer_cuda.obj")
        ]
        
        all_compiled = True
        for src_file, obj_file in cuda_files:
            # 支持的架构包括：
            # - sm_60: GTX 1000系列 (GTX 1060, 1070, 1080等)  
            # - sm_70: GTX 1080 Ti, Titan Xp等
            # - sm_75: RTX 2000系列 (RTX 2060, 2070, 2080等)
            # - sm_80: RTX 3000系列 (RTX 3060, 3070, 3080等)
            # - sm_86: RTX 3090, 4090等
            # - sm_89: RTX 4060, 4070, 4080等
            cuda_cmd = f'''"{vs_vars}" && nvcc -c {src_file} -o {obj_file} -std=c++17 --compiler-options "/O2,/std:c++17,/EHsc,/wd4819,/MD" --use_fast_math -I"{cuda_home}\\include" -I"{pybind11.get_include()}" -Isrc -gencode=arch=compute_60,code=sm_60 -gencode=arch=compute_70,code=sm_70 -gencode=arch=compute_75,code=sm_75 -gencode=arch=compute_80,code=sm_80 -gencode=arch=compute_86,code=sm_86 -gencode=arch=compute_89,code=sm_89'''
            
            print(f"🔧 编译 {src_file} ...")
            print(f"📋 编译命令: {cuda_cmd}")
            result = subprocess.run(cuda_cmd, shell=True, capture_output=True, text=True)
            
            if result.returncode != 0:
                print(f"❌ {src_file} 编译失败:")
                print(f"stdout: {result.stdout}")
                print(f"stderr: {result.stderr}")
                all_compiled = False
                break
            else:
                print(f"✅ {src_file} 编译成功")
        
        if not all_compiled:
            return False
        
        print("✅ 所有CUDA文件编译成功")
        return True
            
    except Exception as e:
        print(f"❌ CUDA编译出错: {e}")
        return False

# 检查CUDA支持
cuda_home = find_cuda()
use_cuda = cuda_home is not None

# 编译参数
is_windows = os.name == 'nt'
if is_windows:
    extra_compile_args = ["/O2", "/std:c++17", "/utf-8", "/EHsc", "/bigobj"]
    extra_link_args = []
else:
    extra_compile_args = ["-O3", "-march=native", "-std=c++17"]
    extra_link_args = []

# 预先添加CUDA宏
if use_cuda:
    extra_compile_args.append("/DUSE_CUDA" if is_windows else "-DUSE_CUDA")

# 源文件列表
source_files = [
    "src/pybind11_wrapper.cpp",
    "src/module_optimizer.cpp"
]

# 库和包含目录
libraries = []
library_dirs = []
include_dirs = [pybind11.get_include()]

if use_cuda:
    print("🚀 启用CUDA支持")
    
    # 编译CUDA代码
    if compile_cuda_code(cuda_home):
        # 添加CUDA相关配置
        include_dirs.extend([
            f"{cuda_home}\\include",
            "src"
        ])
        
        libraries.extend(['cudart_static', 'cuda'])
        library_dirs.extend([
            f"{cuda_home}\\lib\\x64"
        ])
        
        # 添加编译好的CUDA目标文件
        extra_link_args.append("src/module_optimizer_cuda.obj")
        
        print("✅ CUDA配置完成")
    else:
        print("⚠️ CUDA编译失败, 回退到CPU版本")
        use_cuda = False
else:
    print("⚠️ 未检测到CUDA, 使用CPU版本")

# 定义扩展模块
ext_modules = [
    Pybind11Extension(
        "module_optimizer_cpp",
        source_files,
        include_dirs=include_dirs,
        libraries=libraries,
        library_dirs=library_dirs,
        extra_compile_args=extra_compile_args,
        extra_link_args=extra_link_args,
        language='c++'
    ),
]

# 设置信息
setup(
    name="module_optimizer_cpp",
    version="1.4.0",
    author="StarResonanceAutoMod",
    description="C++ implementation with CUDA GPU acceleration for module optimizer",
    long_description="High-performance C++ extension with CUDA GPU acceleration for module optimization algorithms",
    ext_modules=ext_modules,
    zip_safe=False,
    python_requires=">=3.8",
    install_requires=[
        "pybind11>=2.10.0",
    ],
    classifiers=[
        "Development Status :: 4 - Beta",
        "Intended Audience :: Developers",
        "License :: OSI Approved :: MIT License",
        "Programming Language :: Python :: 3",
        "Programming Language :: Python :: 3.8",
        "Programming Language :: Python :: 3.9",
        "Programming Language :: Python :: 3.10",
        "Programming Language :: Python :: 3.11",
        "Programming Language :: Python :: 3.12",
        "Programming Language :: Python :: 3.13",
        "Programming Language :: C++",
        "Topic :: Scientific/Engineering :: Artificial Intelligence",
    ],
)
