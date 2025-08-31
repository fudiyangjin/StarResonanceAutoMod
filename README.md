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

- Windows 10/11 (64 位)
- 下载并安装 [Npcap](https://nmap.org/npcap/) 或 [WinPcap](https://www.winpcap.org/)（推荐 Npcap）
- 《星痕共鸣》游戏客户端

> ⚠️ **重要**: .exe 用户必须安装 Npcap 才能正常使用抓包功能！如果您之前使用过 dps 检测工具, 不需要安装

#### 🔽 获取工具

1. 从 [Release 页面](https://github.com/fudiyangjin/StarResonanceAutoMod/releases) 下载最新版本的 `StarResonanceAutoMod.exe`
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

# 启用调试模式查看详细信息
.\StarResonanceAutoMod.exe -a --debug
```

## ⚡ 枚举模式性能参考

### 🖥️ 测试环境

- **CPU**: AMD Ryzen 9 5900X 12-Core Processor (3.70 GHz)
- **内存**: 32GB DDR4
- **系统**: Windows 10/11

### 📊 性能测试结果

| 模组数量 | 组合总数      | 计算耗时 | 每秒处理组合数 |
| -------- | ------------- | -------- | -------------- |
| 100      | 3,921,225     | 0.192s   | ~20.4M         |
| 200      | 64,684,950    | 3.772s   | ~17.1M         |
| 300      | 330,791,175   | 19.546s  | ~16.9M         |
| 400      | 1,050,739,900 | 74.437s  | ~14.1M         |
| 500      | 2,573,031,125 | 222.316s | ~11.6M         |

### 🚀 性能优化建议

1. **关闭游戏**: 枚举模式运行时建议退出游戏，释放 CPU 资源
2. **避免其他重负载程序**: 确保 CPU 专注于模组计算
3. **合理选择模组数量**: 根据实际需求选择筛选范围，避免过度计算

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

#### ⚠️ 使用注意事项

1. **先安装 Npcap**: 如果没有安装 Npcap，程序无法进行网络抓包
2. **网络接口选择**: 建议优先使用 `-a` 参数自动选择网络接口
3. **使用枚举模式**: 开启枚举模式计算开始后最好退出游戏保证 CPU 的利用率
4. **`-attr` 参数选择**: `-attr`, `-exattr` 后跟的参数必须在文档最后 **支持的属性词条** 中
---

### 💻 方式二：源码运行（开发者）

适合开发者或需要自定义功能的高级用户。

#### 🛠️ 开发环境要求

- Python 3.8+ (推荐 3.10+) ⚠️
- **Visual Studio Build Tools 2019/2022** 或 Visual Studio
- Git
- Windows SDK

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

**4. 编译 C++扩展**

```bash
cd cpp_extension
python setup.py build_ext --inplace
cd ..
```

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
