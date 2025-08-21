"""
模组定义
"""

from typing import Dict, List
from dataclasses import dataclass
from enum import Enum


class ModuleType(Enum):
    """模组类型枚举"""
    BASIC_ATTACK = 5500101
    HIGH_PERFORMANCE_ATTACK = 5500102
    BASIC_HEALING = 5500201
    HIGH_PERFORMANCE_HEALING = 5500202
    BASIC_PROTECTION = 5500301
    HIGH_PERFORMANCE_PROTECTION = 5500302


class ModuleAttrType(Enum):
    """模组属性类型枚举"""
    STRENGTH_BOOST = 1110
    AGILITY_BOOST = 1111
    INTELLIGENCE_BOOST = 1112
    SPECIAL_ATTACK_DAMAGE = 1113
    ELITE_STRIKE = 1114
    SPECIAL_HEALING_BOOST = 1205
    EXPERT_HEALING_BOOST = 1206
    CASTING_FOCUS = 1407
    ATTACK_SPEED_FOCUS = 1408
    CRITICAL_FOCUS = 1409
    LUCK_FOCUS = 1410
    MAGIC_RESISTANCE = 1307
    PHYSICAL_RESISTANCE = 1308


class ModuleCategory(Enum):
    """模组类型分类"""
    ATTACK = "攻击"
    GUARDIAN = "守护" 
    SUPPORT = "辅助"


# 模组名称映射
MODULE_NAMES = {
    ModuleType.BASIC_ATTACK.value: "基础攻击",
    ModuleType.HIGH_PERFORMANCE_ATTACK.value: "高性能攻击",
    ModuleType.BASIC_HEALING.value: "基础治疗",
    ModuleType.HIGH_PERFORMANCE_HEALING.value: "高性能治疗",
    ModuleType.BASIC_PROTECTION.value: "基础防护",
    ModuleType.HIGH_PERFORMANCE_PROTECTION.value: "高性能守护",
}

# 模组属性名称映射
MODULE_ATTR_NAMES = {
    ModuleAttrType.STRENGTH_BOOST.value: "力量加持",
    ModuleAttrType.AGILITY_BOOST.value: "敏捷加持",
    ModuleAttrType.INTELLIGENCE_BOOST.value: "智力加持",
    ModuleAttrType.SPECIAL_ATTACK_DAMAGE.value: "特攻伤害",
    ModuleAttrType.ELITE_STRIKE.value: "精英打击",
    ModuleAttrType.SPECIAL_HEALING_BOOST.value: "特攻治疗加持",
    ModuleAttrType.EXPERT_HEALING_BOOST.value: "专精治疗加持",
    ModuleAttrType.CASTING_FOCUS.value: "施法专注",
    ModuleAttrType.ATTACK_SPEED_FOCUS.value: "攻速专注",
    ModuleAttrType.CRITICAL_FOCUS.value: "暴击专注",
    ModuleAttrType.LUCK_FOCUS.value: "幸运专注",
    ModuleAttrType.MAGIC_RESISTANCE.value: "抵御魔法",
    ModuleAttrType.PHYSICAL_RESISTANCE.value: "抵御物理",
}

# 模组类型到分类的映射
MODULE_CATEGORY_MAP = {
    ModuleType.BASIC_ATTACK.value: ModuleCategory.ATTACK,
    ModuleType.HIGH_PERFORMANCE_ATTACK.value: ModuleCategory.ATTACK,
    ModuleType.BASIC_PROTECTION.value: ModuleCategory.GUARDIAN,
    ModuleType.HIGH_PERFORMANCE_PROTECTION.value: ModuleCategory.GUARDIAN,
    ModuleType.BASIC_HEALING.value: ModuleCategory.SUPPORT,
    ModuleType.HIGH_PERFORMANCE_HEALING.value: ModuleCategory.SUPPORT,
}

# 属性阈值和效果等级
ATTR_THRESHOLDS = [1, 4, 8, 12, 16, 20]


@dataclass
class ModulePart:
    """模组部件信息"""
    id: int
    name: str
    value: int


@dataclass
class ModuleInfo:
    """模组信息"""
    name: str
    config_id: int
    uuid: str
    quality: int
    parts: List[ModulePart] 