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

1. Download from the Release page:
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

# Enforce final attribute sums with enumeration
.\StarResonanceAutoMod.exe -a -enum -attr "Intellect Boost" "Crit Focus" "Special Attack" -mas "Special Attack" 20 -mas "Intellect Boost" 16 -lang en

# Enable debug mode for detailed logs in English
.\StarResonanceAutoMod.exe -a --debug -lang en
```

#### 🗂️ Offline Run (No Re-capture Needed)

Once modules are captured, the program automatically saves and overwrites `modules.vdata` in the exe directory (always keeping the latest one).

To compute directly from the latest offline data without recapturing or relogging:

```bash
.\StarResonanceAutoMod.exe -lv -lang en
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
| 500     | 2,573,031,125    | 13.630s   | ~188.7M    | 1.2837s   | ~2004.0M   | 10.62x    | 3.6628s     | ~702.5M    | 3.72x       |
| 600     | 5,346,164,850    | 27.348s   | ~195.5M    | 2.6983s   | ~1981.9M   | 10.14x    | 7.3117s     | ~731.5M    | 3.74x       |
| 700     | 9,918,641,075    | 50.245s   | ~197.4M    | 5.0685s   | ~1957.9M   | 9.91x     | 13.0472s    | ~760.3M    | 3.85x       |
| 800     | 16,938,959,800   | 87.154s   | ~194.3M    | 8.1274s   | ~2084.8M   | 10.72x    | 22.3830s    | ~757.0M    | 3.90x       |
| 900     | 27,155,621,025   | -         | -          | 14.1992s  | ~1912.5M   | 9.84x     | 37.3179s    | ~727.9M    | 3.75x       |
| 1000    | 41,417,124,375   | -         | -          | 23.2125s  | ~1784.5M   | 9.18x     | 57.2074s    | ~723.9M    | 3.73x       |

### 🚀 Performance Tips

1. Close the game during enumeration to free CPU/GPU resources
2. Avoid other heavy workloads when running the optimizer
3. Choose a reasonable number of modules to avoid excessive computation

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
| `--enumeration-mode`    | `-enum`  | flag    | Enable enumeration mode (recommended with -attr)                     | `-enum` |
| `--debug`               | `-d`     | flag    | Enable debug logs                                                    | `-d` |
| `--min-attr-sum`        | `-mas`   | multi   | Min total value for an attribute in final 4-piece set                | `-mas "Crit Focus" 8 -mas "Intellect Boost" 16` |
| `--lang`                | `-lang`  | string  | Output language, `zh` or `en` (default: `zh`)                        | `-lang en` |
| `--load-vdata`          | `-lv`    | flag    | Load `modules.vdata` from exe dir and compute offline                | `-lv` |

#### ⚠️ Notes

1. Install Npcap first; otherwise packet capture will not work
2. Prefer `-a` to auto-select the network interface
3. For best CPU utilization during enumeration, exit the game while computing
4. Enumeration mode supports up to 800 (CPU/OpenCL) or 1000 (CUDA) modules
5. `-attr`, `-exattr`, `-mas` require supported attribute names (see below)

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
> - Supported GPU families: RTX 2000/3000/4000/5000 series

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


