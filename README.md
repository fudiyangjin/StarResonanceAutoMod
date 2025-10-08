# 星痕共鸣模组全自动筛选工具

[![License: AGPL v3](https://img.shields.io/badge/License-AGPL%20v3-brightgreen.svg)](https://www.gnu.org/licenses/agpl-3.0.txt)

一款专为《星痕共鸣》游戏开发的星痕共鸣模组智能筛选工具，通过网络数据包分析自动获取游戏中的模组数据，并使用 C++优化的算法计算最优模组搭配方案。

## 🌟 核心功能

- **🔍 实时模组抓取**: 通过网络抓包实时获取游戏中的模组数据
- **⚡ 智能筛选算法**: 基于 C++优化的多策略并行算法，快速计算最优搭配
- **🎯 精准属性筛选**: 支持按属性词条、模组类型进行精确筛选
- **📊 综合评分系统**: 为每个推荐搭配计算游戏中的战斗力评分
- **🚀 自动化流程**: 一键启动，自动完成整个筛选过程

## 📦 两种使用方式

### 🎮 方式一：可执行文件（推荐普通用户）

适合不熟悉编程的用户，开箱即用。

#### 📋 系统要求

**基础要求（CPU 版本和 GPU 版本都需要）：**

- Windows 10/11 (64 位)
- 下载并安装 [Npcap](https://nmap.org/npcap/) 或 [WinPcap](https://www.winpcap.org/)（推荐 Npcap）
- 《星痕共鸣》游戏客户端

**GPU 版本额外要求：**

- NVIDIA 显卡, 且驱动版本 ≥ 571.96（CUDA 路径）
- AMD / Intel 显卡, 支持 OpenCL 的显卡驱动（应该没有不支持的, OPENCL 路径)

> ⚠️ **重要**: .exe 用户必须安装 Npcap 才能正常使用抓包功能！如果您之前使用过 dps 检测工具, 不需要安装
> ⚠️ **重要**: 首先尝试 GPU 版本, 若不支持会自动转为 CPU 运算. 若 GPU 性能不如 CPU 可直接使用 CPU 版本

#### 🔽 获取工具

**下载步骤：**

1. 从 [Release 页面](https://github.com/fudiyangjin/StarResonanceAutoMod/releases) 下载对应版本：
   - `StarResonanceAutoMod_CPU.exe` - CPU 版本
   - `StarResonanceAutoMod_CUDA.exe` - GPU 版本
2. 将文件放置在任意文件夹中

#### 🚀 快速开始

**基础使用流程：**

1. 运行命令提示符(cmd, 终端)
2. 导航到 exe 文件所在目录
3. 执行基础命令：
   ```bash
   .\StarResonanceAutoMod.exe -a
   ```
4. 启动游戏，重新登录并选择角色
5. 等待程序自动完成模组分析(程序会自动退出)

#### 💡 常用命令示例

```bash
# 查看所有可用网络接口
.\StarResonanceAutoMod.exe --list

# 自动选择网络接口（推荐）
.\StarResonanceAutoMod.exe -a

# 筛选包含特定属性的模组, 携带的属性会优先筛选(推荐)
.\StarResonanceAutoMod.exe -a -attr 智力加持 暴击专注 特攻伤害

# 排除不需要的属性, -exattr 后携带的属性筛选优先级会降低
.\StarResonanceAutoMod.exe -a -attr 智力加持 暴击专注 -exattr 施法专注 攻速专注

# 要求包含多个指定属性(谨慎使用, 严格的筛选规则)
.\StarResonanceAutoMod.exe -a -attr 智力加持 特攻伤害 暴击专注 -mc 2

# 启用枚举模式直接运算
.\StarResonanceAutoMod.exe -a -enum

# 启用枚举模式+指定携带属性(推荐, 可以得到指定属性更全的组合)
.\StarResonanceAutoMod.exe -a -enum -attr 智力加持 暴击专注 特攻伤害

# 最终属性值筛选, 和枚举模式并用, 最终组合特攻伤害 >= 20, 智力加持 >= 16
.\StarResonanceAutoMod.exe -a -enum -attr 智力加持 暴击专注 特攻伤害 -mas 特攻伤害 20 -mas 智力加持 16

# 启用调试模式查看详细信息
.\StarResonanceAutoMod.exe -a --debug
```

## ⚡ 枚举模式性能参考

### 🖥️ 测试环境

**CPU 版本测试环境：**

- **CPU**: AMD Ryzen 9 5900X 12-Core Processor (3.70 GHz)
- **内存**: 32GB DDR4
- **系统**: Windows 10/11

**GPU 版本测试环境：**

- **GPU**: NVIDIA RTX 3090 (Compute Capability 8.6, 82 SM)
- **显存**: 24GB GDDR6X
- **系统**: Windows 10/11

### 📊 性能测试结果

| 模组数量 | 组合总数       | CPU 版本耗时 | CPU 每秒处理 | CUDA 版本耗时 | CUDA 每秒处理 | CUDA 提升 | OpenCL 版本耗时 | OpenCL 每秒处理 | OpenCL 提升 |
| -------- | -------------- | ------------ | ------------ | ------------- | ------------- | --------- | ---------------- | ---------------- | ----------- |
| 500      | 2,573,031,125  | 13.630s      | ~188.7M      | 1.2837s       | ~2004.0M      | 10.62x    | 3.6628s          | ~702.5M          | 3.72x       |
| 600      | 5,346,164,850  | 27.348s      | ~195.5M      | 2.6983s       | ~1981.9M      | 10.14x    | 7.3117s          | ~731.5M          | 3.74x       |
| 700      | 9,918,641,075  | 50.245s      | ~197.4M      | 5.0685s       | ~1957.9M      | 9.91x     | 13.0472s         | ~760.3M          | 3.85x       |
| 800      | 16,938,959,800 | 87.154s      | ~194.3M      | 8.1274s       | ~2084.8M      | 10.72x    | 22.3830s         | ~757.0M          | 3.90x       |
| 900      | 27,155,621,025 | -            | -            | 14.1992s      | ~1912.5M      | 9.84x     | 37.3179s         | ~727.9M          | 3.75x       |
| 1000     | 41,417,124,375 | -            | -            | 23.2125s      | ~1784.5M      | 9.18x     | 57.2074s         | ~723.9M          | 3.73x       |

### 🚀 性能优化建议

1. **关闭游戏**: 枚举模式运行时建议退出游戏，释放 CPU, GPU 资源
2. **避免其他重负载程序**: 确保 CPU, GPU 专注于模组计算
3. **合理选择模组数量**: 根据实际需求选择筛选范围，避免过度计算

> ⚠️ **枚举模式限制**: 枚举模式最多支持 800 个模组的运算，如果模组数量超过 800 个，程序会自动筛选至 800 个进行计算(GPU 模式为 1000)

#### 📝 命令参数说明

| 参数                   | 短参数    | 类型   | 说明                                        | 可选值/示例                                 |
| ---------------------- | --------- | ------ | ------------------------------------------- | ------------------------------------------- |
| `--list`               | `-l`      | 开关   | 列出所有网络接口                            | `-l`                                        |
| `--auto`               | `-a`      | 开关   | 自动选择网络接口(推荐)                      | `-a`                                        |
| `--interface`          | `-i`      | 整数   | 手动指定网络接口索引                        | `-i 2`                                      |
| `--category`           | `-c`      | 字符串 | 筛选模组类型                                | `攻击` / `守护` / `辅助` / `全部`(默认全部) |
| `--attributes`         | `-attr`   | 多值   | 指定要包含的属性词条, 优先筛选              | `-attr 智力加持 暴击专注`                   |
| `--exclude-attributes` | `-exattr` | 多值   | 指定要排除的属性词条, 筛选优先级变低        | `-exattr 施法专注`                          |
| `--match-count`        | `-mc`     | 整数   | 模组需包含的指定属性数量                    | `-mc 2` (默认:1)                            |
| `--enumeration-mode`   | `-enum`   | 开关   | 启用枚举模式直接运算(推荐和 -attr 一起使用) | `-enum`                                     |
| `--debug`              | `-d`      | 开关   | 启用调试模式输出详细日志                    | `-d`                                        |
| `--min-attr-sum`     | `-mas`    | 多值   | 指定求解后包含的属性词条总和的最小值        | `-mas 智力加持 20 -mas 暴击专注 20`         |

#### ⚠️ 使用注意事项

1. **先安装 Npcap**: 如果没有安装 Npcap，程序无法进行网络抓包
2. **网络接口选择**: 建议优先使用 `-a` 参数自动选择网络接口
3. **使用枚举模式**: 开启枚举模式计算开始后最好退出游戏保证 CPU 的利用率
4. **枚举模式限制**: 枚举模式最多支持 800 个模组的运算，超过后会自动筛选至 800 个(GPU 模式为 1000)
5. **`-attr` 参数选择**: `-attr`, `-exattr` , `-mas` 后跟的参数必须在文档最后 **支持的属性词条** 中

---

### 💻 方式二：源码运行（开发者）

适合开发者或需要自定义功能的高级用户。

#### 🛠️ 开发环境要求

**基础要求（CPU 版本和 GPU 版本都需要）：**

- Python 3.8+ (推荐 3.10+) ⚠️
- **Visual Studio Build Tools 2019/2022** 或 Visual Studio
- Git
- Windows SDK

**GPU 版本额外要求：**

- **CUDA Toolkit 12.8**（CUDA 路径）
- **OpenCL SDK/CUDA Toolkit** (OpenCL 路径)
- 支持 CUDA 的 NVIDIA 显卡（RTX 20XX 及以上）

> ⚠️ **编译器一致性问题**:
>
> - **CPython**: 从 python.org 下载的 Python 解释器，用 MSVC 编译
> - **C++扩展**: 必须用相同版本的 MSVC 编译，否则会出现：
>   - Segmentation Fault (段错误)
>   - 默认参数调用失败
>   - 内存访问异常
> - **绝对不能混用**: MSVC 编译的 Python + MinGW 编译的扩展 = 💥 崩溃

#### 📥 环境搭建

**1. 克隆项目**

```bash
git clone https://github.com/fudiyangjin/StarResonanceAutoMod.git
cd StarResonanceAutoMod
```

**2. 安装依赖**

```bash
pip install -r requirements.txt
```

> 📝 **依赖说明**:
>
> - `scapy>=2.5.0`: 网络抓包库
> - `zstandard>=0.21.0`: 数据压缩库
> - `protobuf>=4.21.0`: 协议缓冲区库
> - `pybind11>=2.10.0`: C++扩展绑定库

**3. 编译 C++扩展**

**CPU 版本编译：**

```bash
cd cpp_extension
python setup.py build_ext --inplace
cd ..
```

**GPU 版本编译：**

```bash
cd cpp_extension
python setup.py build_ext --inplace
cd ..
```

> 📝 **GPU 版本编译说明**:
>
> - 编译脚本会自动检测 CUDA 环境
> - 如果检测到 CUDA，会自动编译 GPU 加速版本
> - 如果 CUDA 编译失败，会自动回退到 CPU 版本
> - 支持的显卡架构：GTX 1000 系列、RTX 2000/3000/4000/5000 系列

#### 🚀 运行程序

```bash
# 基础运行
python star_railway_monitor.py -a

# 带参数运行
python star_railway_monitor.py -a -attr 智力加持 暴击专注 -d

# 查看所有参数
python star_railway_monitor.py --help
```

#### 🔧 开发者功能

**日志系统**

- 日志文件位于 `logs/` 目录
- 支持调试模式：`python star_railway_monitor.py -d`

**C++性能优化**

- 核心算法使用 C++实现
- 源码位于 `cpp_extension/src/`
- 支持 CUDA GPU 加速
- 支持 OpenCL GPU 加速

**GPU 加速功能**

- 自动检测 CUDA 环境
- 支持多种 NVIDIA 显卡架构
- 编译时自动优化 GPU 代码
- 支持 OpenCL（AMD/Intel），自动检测平台与设备
- 优先 CUDA，其次 OpenCL，均不可用则回退 CPU
- 运行时自动回退到 CPU 计算（当 GPU 不可用或初始化失败时）

#### 📁 项目结构

```
StarResonanceAutoMod/
├── star_railway_monitor.py    # 主程序入口
├── module_parser.py           # 模组数据解析器
├── module_optimizer.py        # 模组优化算法
├── module_types.py           # 数据类型定义
├── packet_capture.py         # 网络抓包模块
├── network_interface_util.py # 网络接口工具
├── logging_config.py         # 日志配置
├── BlueProtobuf_pb2.py       # 协议buffer定义
├── requirements.txt          # Python依赖
├── cpp_extension/            # C++性能扩展
│   ├── setup.py             # 编译脚本
│   └── src/                 # C++源码
│       ├── module_optimizer.cpp      # CPU优化算法
│       ├── module_optimizer_cuda.cu  # CUDA GPU加速算法
|       |── module_optimizer_opencl.cpp # OpenCL GPU加速算法
│       └── pybind11_wrapper.cpp      # Python绑定
├── logs/                    # 日志文件目录
└── README.md                # 说明文档
```

---

## 🎯 支持的属性词条

### 基础属性

- 力量加持、敏捷加持、智力加持
- 特攻伤害、精英打击
- 特攻治疗加持、专精治疗加持
- 施法专注、攻速专注、暴击专注、幸运专注
- 抵御魔法、抵御物理

### 特殊属性

- 极-绝境守护、极-伤害叠加、极-灵活身法
- 极-生命凝聚、极-急救措施、极-生命波动
- 极-生命汲取、极-全队幸暴

## 🛠️ 问题排查

### 获取帮助

- 查看详细日志：使用 `--debug` 参数
- 提交 Issue：[GitHub Issues](https://github.com/fudiyangjin/StarResonanceAutoMod/issues)
- 查看日志文件：`logs/` 目录下的最新日志

## 🙏 鸣谢

本项目关键数据抓取与分析部分基于 [StarResonanceDamageCounter](https://github.com/dmlgzs/StarResonanceDamageCounter) 项目移植而来，感谢原作者对于本项目的帮助。

## ⚖️ 免责声明

本工具仅用于游戏数据分析学习目的，不得用于任何违反游戏服务条款的行为。使用者需自行承担相关风险。项目开发者不对任何使用本工具产生的后果负责。请在使用前确保遵守游戏社区的相关规定和道德标准。
