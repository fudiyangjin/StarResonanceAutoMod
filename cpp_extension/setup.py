import os
import sys
from setuptools import setup, Extension
from pybind11.setup_helpers import Pybind11Extension
import pybind11

# 检查操作系统
is_windows = os.name == 'nt'

# 编译参数
if is_windows:
    extra_compile_args = ["/O2", "/std:c++17", "/utf-8"]
    extra_link_args = []
else:
    extra_compile_args = ["-O3", "-march=native", "-std=c++17"]
    extra_link_args = []

# 定义扩展模块
ext_modules = [
    Pybind11Extension(
        "module_optimizer_cpp",
        [
            "src/pybind11_wrapper.cpp",
            "src/module_optimizer.cpp"
        ],
        include_dirs=[pybind11.get_include()],
        extra_compile_args=extra_compile_args,
        extra_link_args=extra_link_args,
        language='c++'
    ),
]

# 设置信息
setup(
    name="module_optimizer_cpp",
    version="1.0.0",
    author="StarResonanceAutoMod",
    description="C++ implementation of module optimizer for performance optimization",
    long_description="High-performance C++ extension for module optimization algorithms",
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
        "Programming Language :: C++",
    ],
)
