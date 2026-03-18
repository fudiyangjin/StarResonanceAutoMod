# Star Resonance Auto Module Filter

[中文文档](README.md)

An intelligent module filtering tool for the game “Star Resonance”. It captures module data via network packets and computes optimal combinations using multi-strategy algorithms with a C++ core. English CLI inputs and English output/logs are supported via `--lang en`.

## 🌟 Core Features

- Real-time module capture via packet sniffing
- Multi-strategy optimizer (C++ core; CUDA/OpenCL acceleration when available)
- Precise filtering by attributes and module categories
- Unified combat power scoring for each recommended combination
- Automated workflow: one command to complete the full process

## 📦 Two Ways to Use

### 🎮 Way 1: Executable (recommended for end users)

Suitable for non-developers, ready to use out of the box.

#### 📋 System Requirements

Basic (for both CPU and GPU versions):

- Windows 10/11 (64-bit)
- Install Npcap or WinPcap (Npcap recommended)
- Star Resonance game client

GPU-specific:

- NVIDIA GPU, driver version ≥ 571.96 (CUDA path)
- AMD / Intel GPU with OpenCL-capable drivers (OpenCL path)

> ⚠️ Important: .exe users must install Npcap to capture packets. If you've used DPS counter tools before, you may already have it installed.
> ⚠️ Important: Try GPU version first; if unavailable it will fall back to CPU automatically. If GPU is slower, use CPU version directly.

#### 🔽 Get the Tool

1. Download from the [Release page](https://github.com/fudiyangjin/StarResonanceAutoMod/releases):
   - `StarResonanceAutoMod_CPU.exe` – CPU version
   - `StarResonanceAutoMod_CUDA.exe` – GPU version
2. Place the file in any directory

#### 🚀 Quick Start

1. Open Command Prompt (cmd, Terminal)
2. Navigate to the directory where the exe is located
3. Run:
   ```bash
   .\StarResonanceAutoMod.exe -a
   ```
4. Launch the game, relogin and select your character
5. Wait for the program to finish analysis (it will exit automatically)

#### 💡 Common Commands

```bash
# List all network interfaces
.\StarResonanceAutoMod.exe --list

# Auto-select network interface (recommended)
.\StarResonanceAutoMod.exe -a -lang en

# Prefer modules that contain specified attributes (recommended)
.\StarResonanceAutoMod.exe -a -attr "Intellect Boost" "Crit Focus" "Special Attack" -lang en

# Exclude attributes (excluded attributes get lower priority)
.\StarResonanceAutoMod.exe -a -attr "Intellect Boost" "Crit Focus" -exattr "Cast Focus" "Attack SPD" -lang en

# Require multiple specified attributes (strict)
.\StarResonanceAutoMod.exe -a -attr "Intellect Boost" "Special Attack" "Crit Focus" -mc 2 -lang en

# Enable enumeration mode
.\StarResonanceAutoMod.exe -a -enum -lang en

# Enumeration + target attributes (recommended)
.\StarResonanceAutoMod.exe -a -enum -attr "Intellect Boost" "Crit Focus" "Special Attack" -lang en

# Enable 5-module computation
.\StarResonanceAutoMod.exe -a -cs 5 -lang en

# Enable 5-module with preferred attributes
.\StarResonanceAutoMod.exe -a -cs 5 -attr "Intellect Boost" "Crit Focus" "Special Attack" -lang en

# Enable 5-module with final combination filter
.\StarResonanceAutoMod.exe -a -cs 5 -mas "Special Attack" 20 -mas "Intellect Boost" 16 -lang en

# Enforce final attribute sums with enumeration
.\StarResonanceAutoMod.exe -a -enum -attr "Intellect Boost" "Crit Focus" "Special Attack" -mas "Special Attack" 20 -mas "Intellect Boost" 16 -lang en

# Enable debug mode for detailed logs in English
.\StarResonanceAutoMod.exe -a --debug -lang en
```

#### 🗂️ Offline Run (No Re-capture Needed)

Once modules are captured, the program automatically saves and overwrites `modules.vdata` in the exe directory (always keeping the latest one).

To compute directly from the latest offline data without recapturing or relogging:

```bash
.\StarResonanceAutoMod.exe -enum -lv -lang en
```

For 5-module offline computation:

```bash
.\StarResonanceAutoMod.exe -lv -cs 5 -lang en
```

## ⚡ Enumeration Mode Performance Reference

### 🖥️ Test Environments

CPU version:

- CPU: AMD Ryzen 9 5900X 12-Core Processor (3.70 GHz)
- Memory: 32GB DDR4
- OS: Windows 10/11

GPU version:

- GPU: NVIDIA RTX 3090 (Compute Capability 8.6, 82 SM)
- VRAM: 24GB GDDR6X
- OS: Windows 10/11

### 📊 Performance Results

| Modules | Combinations     | CPU Time  | CPU/s      | CUDA Time | CUDA/s     | CUDA Gain | OpenCL Time | OpenCL/s   | OpenCL Gain |
| ------- | ---------------- | --------- | ---------- | --------- | ---------- | --------- | ----------- | ---------- | ----------- |
| 500     | 2,573,031,125    | 6.8124s   | ~377.7M    | 0.2756s   | ~9336.1M   | 24.72x    | 3.6628s     | ~702.5M    | 3.72x       |
| 600     | 5,346,164,850    | 14.3606s  | ~372.3M    | 0.5258s   | ~10167.7M  | 27.31x    | 7.3117s     | ~731.5M    | 3.74x       |
| 700     | 9,918,641,075    | 26.4913s  | ~374.4M    | 1.0677s   | ~9289.7M   | 24.81x    | 13.0472s    | ~760.3M    | 3.85x       |
| 800     | 16,938,959,800   | 43.7750s  | ~387.0M    | 1.4090s   | ~12022.0M  | 31.07x    | 22.3830s    | ~757.0M    | 3.90x       |
| 900     | 27,155,621,025   | -         | -          | 2.5902s   | ~10484.0M  | -         | 37.3179s    | ~727.9M    | 3.75x       |
| 1000    | 41,417,124,375   | -         | -          | 3.6148s   | ~11457.7M  | -         | 57.2074s    | ~723.9M    | 3.73x       |

> **Note:**
>
> - CPU and CUDA test results have been updated
> - OpenCL column retained as historical data; not re-tested in this round
> - 900/1000 modules: only CUDA main path (Dense LUT) data added; CPU and gain factor left blank

### 🚀 Performance Tips

1. Close the game during enumeration to free CPU/GPU resources
2. Avoid other heavy workloads when running the optimizer
3. Choose a reasonable number of modules to avoid excessive computation

### 🧩 5-Module CUDA Test Results

5-module mode is enabled via `--combination-size 5` or `-cs 5`. When CUDA is available, the program uses **parallel strategy enumeration + beam search**; `-enum` is automatically disabled and falls back to regular optimization in 5-module mode.

| Modules | Combinations      | CUDA Time  |
| ------- | ----------------- | ---------- |
| 500     | 255,244,687,600   | 24.3293s   |
| 600     | 637,262,850,120   | 59.6303s   |

> **Note:** 5-module combination space is much larger than 4-module.

> ⚠️ Enumeration Limit: Up to 800 modules in CPU/OpenCL mode and 1000 in CUDA; if exceeded, prefiltering will reduce to the limit automatically.

## 📝 CLI Options

| Option                  | Short    | Type    | Description                                                          | Examples |
| ----------------------- | -------- | ------- | -------------------------------------------------------------------- | -------- |
| `--list`                | `-l`     | flag    | List all network interfaces                                          | `-l`     |
| `--auto`                | `-a`     | flag    | Auto select network interface (recommended)                          | `-a`     |
| `--interface`           | `-i`     | int     | Use the specified interface index                                    | `-i 2`   |
| `--category`            | `-c`     | string  | Module category                                                      | `attack` / `guardian` / `support` / `all` (default: all)|
| `--attributes`          | `-attr`  | multi   | Include attributes (prioritized)                                     | `-attr "Intellect Boost" "Crit Focus"` |
| `--exclude-attributes`  | `-exattr`| multi   | Exclude attributes (lower priority)                                  | `-exattr "Cast Focus"` |
| `--match-count`         | `-mc`    | int     | Number of required target attributes                                 | `-mc 2` (default: 1) |
| `--combination-size`    | `-cs`    | int     | Number of modules per combination, `4` or `5`                        | `-cs 5` |
| `--enumeration-mode`    | `-enum`  | flag    | Enable enumeration mode (recommended with -attr, 4-module only)      | `-enum` |
| `--debug`               | `-d`     | flag    | Enable debug logs                                                    | `-d` |
| `--min-attr-sum`        | `-mas`   | multi   | Min total value for an attribute in final 4-piece set                | `-mas "Crit Focus" 8 -mas "Intellect Boost" 16` |
| `--lang`                | `-lang`  | string  | Output language, `zh` or `en` (default: `zh`)                        | `-lang en` |
| `--load-vdata`          | `-lv`    | flag    | Load `modules.vdata` from exe dir and compute offline                | `-lv` |

#### ⚠️ Notes

1. Install Npcap first; otherwise packet capture will not work
2. Prefer `-a` to auto-select the network interface
3. For best CPU utilization during enumeration, exit the game while computing
4. Enumeration mode supports up to 800 (CPU/OpenCL) or 1000 (CUDA) modules; if exceeded, auto-prefilter to the limit
5. **5-module mode**: Use `-cs 5` for 5-module computation; with CUDA, enables parallel strategy enumeration + beam search; without CUDA, beam search only
6. **5-module and enumeration**: In 5-module mode, `-enum` is automatically disabled and falls back to regular optimization
7. `-attr`, `-exattr`, `-mas` require supported attribute names (see below)

---

### 💻 Way 2: Run from Source (for developers)

Suitable for developers or advanced users who need customization.

#### 🛠️ Development Requirements

Basic (for both CPU and GPU):

- Python 3.8+ (recommended 3.10+)
- Visual Studio Build Tools 2019/2022 or Visual Studio
- Git
- Windows SDK

GPU-specific:

- CUDA Toolkit 12.8 (for CUDA path)
- OpenCL SDK/CUDA Toolkit (for OpenCL path)
- NVIDIA GPU supporting CUDA (RTX 20XX or later)

> ⚠️ Compiler Consistency:
>
> - CPython: Python from python.org is built with MSVC
> - C++ extensions: must be built with the same MSVC version
> - Otherwise you may encounter segmentation faults, default argument call failures, memory access violations
> - Never mix MSVC-built Python with MinGW-built extensions

#### 📥 Setup

1) Clone the project

```bash
git clone https://github.com/fudiyangjin/StarResonanceAutoMod.git
cd StarResonanceAutoMod
```

2) Install dependencies

```bash
pip install -r requirements.txt
```

> Dependencies:
>
> - `scapy>=2.5.0`: packet capture
> - `zstandard>=0.21.0`: compression
> - `protobuf>=4.21.0`: protocol buffers
> - `pybind11>=2.10.0`: C++ binding
> - `cpp_extension/third_party/cccl-3.3.0/`: cccl >= 3.3

3) Build C++ extensions

CPU build:

```bash
cd cpp_extension
python setup.py build_ext --inplace
cd ..
```

GPU build:

```bash
cd cpp_extension
python setup.py build_ext --inplace
cd ..
```

> GPU Build Notes:
>
> - The build script auto-detects CUDA
> - If CUDA is detected, the GPU-accelerated version is built
> - If CUDA build fails, it falls back to CPU
> - `cpp_extension/third_party/` contains additional header dependencies for compilation
> - Supported GPU families: GTX 1000, RTX 2000/3000/4000/5000 series

#### 🚀 Run

```bash
# Basic run
python star_railway_monitor.py -a

# With parameters (English mode)
python star_railway_monitor.py -a -attr "Intellect Boost" "Crit Focus" -d -lang en

# Show help
python star_railway_monitor.py --help
```

#### 🔧 Developer Features

Logging:

- Logs under `logs/`
- Debug mode: `python star_railway_monitor.py -d`

C++ Optimization:

- Core algorithms implemented in C++
- Source under `cpp_extension/src/`
- CUDA GPU acceleration
- OpenCL GPU acceleration

GPU Acceleration Behavior:

- Auto-detect CUDA environment
- Prefer CUDA, then OpenCL; fall back to CPU if neither available
- Runtime fallback to CPU if GPU unavailable or initialization fails

#### 📁 Project Structure

```text
StarResonanceAutoMod/
├── star_railway_monitor.py    # Entry point
├── module_parser.py           # Module data parser
├── module_optimizer.py        # Module optimizer
├── module_types.py            # Data types and mappings
├── packet_capture.py          # Packet capture
├── network_interface_util.py  # Network interface utilities
├── logging_config.py          # Logging setup
├── BlueProtobuf_pb2.py        # Protobuf definitions
├── requirements.txt           # Python dependencies
├── cpp_extension/             # C++ performance extensions
│   ├── setup.py               # Build script
│   ├── third_party/           # CUDA build dependencies (including CCCL headers)
│   └── src/                   # C++ sources
│       ├── module_optimizer.cpp       # CPU optimizer
│       ├── module_optimizer_cuda.cu   # CUDA GPU optimizer
│       ├── module_optimizer_opencl.cpp# OpenCL GPU optimizer
│       └── pybind11_wrapper.cpp       # Python bindings
├── logs/                      # Logs
└── README.md                  # Docs (Chinese); README_EN.md (English)
```

---

## 🎯 Supported Attributes

### Basic

- Strength Boost, Intellect Boost, Agility Boost
- Special Attack, Elite Strike
- Healing Boost, Healing Enhance
- Cast Focus, Attack SPD, Crit Focus, Luck Focus
- Resistance, Armor

### Special

- Final Protection, DMG Stack, Agile
- Life Condense, First Aid, Life Wave
- Life Steal, Team Luck&Crit

> Internally, attributes are mapped to Chinese canonical names for stable calculation. English inputs are accepted and normalized.

## 🛠️ Troubleshooting

### Get Help

- Use `--debug` for detailed logs
- Submit issues: GitHub Issues
- Check latest logs under `logs/`

## 🙏 Acknowledgements

Core packet parsing and analysis were adapted from StarResonanceDamageCounter. Thanks to the original author.

## ⚖️ Disclaimer

This tool is for analysis and learning purposes only. Do not use it in ways that violate the game’s Terms of Service. Users assume all risks. The developers are not responsible for any consequences arising from the use of this tool. Please comply with community rules and ethical standards.


