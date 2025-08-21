# 星痕共鸣模组全自动筛选工具

[![License: AGPL v3](https://img.shields.io/badge/License-AGPL%20v3-brightgreen.svg)](https://www.gnu.org/licenses/agpl-3.0.txt)

本项目关键数据抓取与分析部分基于 [StarResonanceDamageCounter](https://github.com/dmlgzs/StarResonanceDamageCounter) 项目移植而来，感谢原作者对于本项目的帮助。

## 快速开始

### 基本用法

1. 命令行运行 `StarResonanceAutoMod.exe` + 对应参数
2. 选择网络接口(先带 `-a` 参数尝试自动选择)
3. 重新登录游戏, 选择角色
4. 等待程序自动解析模组并显示最优搭配

### 命令行参数

- `--list` 或 `-l`: 列出所有网络接口
- `--interface <索引>` 或 `-i <索引>`: 指定网络接口索引
- `--auto` 或 `-a`: 自动检测默认网络接口
- `--debug` 或 `-d`: 启用调试模式
- `--category <类型>` 或 `-c <类型>`: 指定模组类型 (攻击/守护/辅助)
- `--attributes <属性1> <属性2> ...` 或 `-attr <属性1> <属性2> ...`: 指定要筛选的属性词条

### 使用示例

```bash
# 列出网络接口
.\StarResonanceAutoMod.exe --list

# 筛选攻击类型模组
.\StarResonanceAutoMod.exe --category 攻击

# 筛选包含特定属性的攻击模组
.\StarResonanceAutoMod.exe --category 攻击 --attributes 精英打击 特攻伤害 智力加持

# 自动检测网络接口并筛选守护模组
.\StarResonanceAutoMod.exe --auto --category 守护

# 指定网络接口并启用调试模式
.\StarResonanceAutoMod.exe --interface 0 --debug
```

python 主程序接口为 `star_railway_monitor.py`

```python
python star_railway_monitor.py -a
```

### 注意事项

1. 打开程序后重新登录
2. 程序会自动退出，无需手动关闭
3. 如果遇到问题，可以尝试使用 `--debug` 参数查看详细日志

### 支持的属性词条

- 力量加持
- 敏捷加持
- 智力加持
- 特攻伤害
- 精英打击
- 特攻治疗加持
- 专精治疗加持
- 施法专注
- 攻速专注
- 暴击专注
- 幸运专注
- 抵御魔法
- 抵御物理

---

**免责声明**：本工具仅用于游戏数据分析学习目的，不得用于任何违反游戏服务条款的行为。使用者需自行承担相关风险。项目开发者不对任何他人使用本工具的恶意战力歧视行为负责。请在使用前确保遵守游戏社区的相关规定和道德标准。
