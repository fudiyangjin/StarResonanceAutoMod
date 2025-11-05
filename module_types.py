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
    EXCELLENT_ATTACK = 5500103
    EXCELLENT_ATTACK_PREFERRED = 5500104
    BASIC_HEALING = 5500201
    HIGH_PERFORMANCE_HEALING = 5500202
    EXCELLENT_HEALING = 5500203
    EXCELLENT_HEALING_PREFERRED = 5500204
    BASIC_PROTECTION = 5500301
    HIGH_PERFORMANCE_PROTECTION = 5500302
    EXCELLENT_PROTECTION = 5500303
    EXCELLENT_PROTECTION_PREFERRED = 5500304


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
    EXTREME_DAMAGE_STACK = 2104
    EXTREME_FLEXIBLE_MOVEMENT = 2105
    EXTREME_LIFE_CONVERGENCE = 2204
    EXTREME_EMERGENCY_MEASURES = 2205
    EXTREME_LIFE_FLUCTUATION = 2404
    EXTREME_LIFE_DRAIN = 2405
    EXTREME_TEAM_CRIT = 2406
    EXTREME_DESPERATE_GUARDIAN = 2304


class ModuleCategory(Enum):
    """模组类型分类"""
    ATTACK = "攻击"
    GUARDIAN = "守护" 
    SUPPORT = "辅助"
    ALL = "全部"


# 模组名称映射
MODULE_NAMES = {
    ModuleType.BASIC_ATTACK.value: "基础攻击",
    ModuleType.HIGH_PERFORMANCE_ATTACK.value: "高性能攻击",
    ModuleType.EXCELLENT_ATTACK.value: "卓越攻击",
    ModuleType.EXCELLENT_ATTACK_PREFERRED.value: "卓越攻击-优选",
    ModuleType.BASIC_HEALING.value: "基础治疗",
    ModuleType.HIGH_PERFORMANCE_HEALING.value: "高性能治疗",
    ModuleType.EXCELLENT_HEALING.value: "卓越辅助",
    ModuleType.EXCELLENT_HEALING_PREFERRED.value: "卓越辅助-优选",
    ModuleType.BASIC_PROTECTION.value: "基础防护",
    ModuleType.HIGH_PERFORMANCE_PROTECTION.value: "高性能守护",
    ModuleType.EXCELLENT_PROTECTION.value: "卓越守护",
    ModuleType.EXCELLENT_PROTECTION_PREFERRED.value: "卓越守护-优选",
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
    ModuleAttrType.EXTREME_DAMAGE_STACK.value: "极-伤害叠加",
    ModuleAttrType.EXTREME_FLEXIBLE_MOVEMENT.value: "极-灵活身法",
    ModuleAttrType.EXTREME_LIFE_CONVERGENCE.value: "极-生命凝聚",
    ModuleAttrType.EXTREME_EMERGENCY_MEASURES.value: "极-急救措施",
    ModuleAttrType.EXTREME_LIFE_FLUCTUATION.value: "极-生命波动",
    ModuleAttrType.EXTREME_LIFE_DRAIN.value: "极-生命汲取",
    ModuleAttrType.EXTREME_TEAM_CRIT.value: "极-全队幸暴",
    ModuleAttrType.EXTREME_DESPERATE_GUARDIAN.value: "极-绝境守护",
}

# 英文显示名称映射
MODULE_NAMES_EN: Dict[int, str] = {
    ModuleType.BASIC_ATTACK.value: "Basic Attack Module",
    ModuleType.HIGH_PERFORMANCE_ATTACK.value: "Advanced Attack Module",
    ModuleType.EXCELLENT_ATTACK.value: "Excellent Attack Module",
    ModuleType.EXCELLENT_ATTACK_PREFERRED.value: "Excellent Attack Module Preferred",
    ModuleType.BASIC_HEALING.value: "Basic Support Module",
    ModuleType.HIGH_PERFORMANCE_HEALING.value: "Advanced Support Module",
    ModuleType.EXCELLENT_HEALING.value: "Excellent Support Module",
    ModuleType.EXCELLENT_HEALING_PREFERRED.value: "Excellent Support Module Preferred",
    ModuleType.BASIC_PROTECTION.value: "Basic Guard Module",
    ModuleType.HIGH_PERFORMANCE_PROTECTION.value: "Advanced Guard Module",
    ModuleType.EXCELLENT_PROTECTION.value: "Excellent Guard Module",
    ModuleType.EXCELLENT_PROTECTION_PREFERRED.value: "Excellent Guard Module Preferred",
}

MODULE_ATTR_NAMES_EN: Dict[int, str] = {
    ModuleAttrType.STRENGTH_BOOST.value: "Strength Boost",
    ModuleAttrType.AGILITY_BOOST.value: "Agility Boost",
    ModuleAttrType.INTELLIGENCE_BOOST.value: "Intellect Boost",
    ModuleAttrType.SPECIAL_ATTACK_DAMAGE.value: "Special Attack",
    ModuleAttrType.ELITE_STRIKE.value: "Elite Strike",
    ModuleAttrType.SPECIAL_HEALING_BOOST.value: "Healing Boost",
    ModuleAttrType.EXPERT_HEALING_BOOST.value: "Healing Enhance",
    ModuleAttrType.CASTING_FOCUS.value: "Cast Focus",
    ModuleAttrType.ATTACK_SPEED_FOCUS.value: "Attack SPD",
    ModuleAttrType.CRITICAL_FOCUS.value: "Crit Focus",
    ModuleAttrType.LUCK_FOCUS.value: "Luck Focus",
    ModuleAttrType.MAGIC_RESISTANCE.value: "Resistance",
    ModuleAttrType.PHYSICAL_RESISTANCE.value: "Armor",
    ModuleAttrType.EXTREME_DAMAGE_STACK.value: "DMG Stack",
    ModuleAttrType.EXTREME_FLEXIBLE_MOVEMENT.value: "Agile",
    ModuleAttrType.EXTREME_LIFE_CONVERGENCE.value: "Life Condense",
    ModuleAttrType.EXTREME_EMERGENCY_MEASURES.value: "First Aid",
    ModuleAttrType.EXTREME_LIFE_FLUCTUATION.value: "Life Wave",
    ModuleAttrType.EXTREME_LIFE_DRAIN.value: "Life Steal",
    ModuleAttrType.EXTREME_TEAM_CRIT.value: "Team Luck&Crit",
    ModuleAttrType.EXTREME_DESPERATE_GUARDIAN.value: "Final Protection",
}

# 模组属性id名称映射
MODULE_ATTR_IDS = {name: attr_id for attr_id, name in MODULE_ATTR_NAMES.items()}

# 模组类型到分类的映射
MODULE_CATEGORY_MAP = {
    ModuleType.BASIC_ATTACK.value: ModuleCategory.ATTACK,
    ModuleType.HIGH_PERFORMANCE_ATTACK.value: ModuleCategory.ATTACK,
    ModuleType.EXCELLENT_ATTACK.value: ModuleCategory.ATTACK, 
    ModuleType.EXCELLENT_ATTACK_PREFERRED.value: ModuleCategory.ATTACK,
    ModuleType.BASIC_PROTECTION.value: ModuleCategory.GUARDIAN,
    ModuleType.HIGH_PERFORMANCE_PROTECTION.value: ModuleCategory.GUARDIAN,
    ModuleType.EXCELLENT_PROTECTION.value: ModuleCategory.GUARDIAN,
    ModuleType.EXCELLENT_PROTECTION_PREFERRED.value: ModuleCategory.GUARDIAN,
    ModuleType.BASIC_HEALING.value: ModuleCategory.SUPPORT,
    ModuleType.HIGH_PERFORMANCE_HEALING.value: ModuleCategory.SUPPORT,
    ModuleType.EXCELLENT_HEALING.value: ModuleCategory.SUPPORT,
    ModuleType.EXCELLENT_HEALING_PREFERRED.value: ModuleCategory.SUPPORT,
}

# 分类中英映射与属性名中英互转
CATEGORY_EN_TO_CN: Dict[str, str] = {
    "attack": ModuleCategory.ATTACK.value,
    "guardian": ModuleCategory.GUARDIAN.value,
    "support": ModuleCategory.SUPPORT.value,
    "all": ModuleCategory.ALL.value,
}

CATEGORY_CN_TO_EN: Dict[str, str] = {
    ModuleCategory.ATTACK.value: "Attack",
    ModuleCategory.GUARDIAN.value: "Guardian",
    ModuleCategory.SUPPORT.value: "Support",
    ModuleCategory.ALL.value: "All",
}

ATTR_CN_TO_EN: Dict[str, str] = {cn: MODULE_ATTR_NAMES_EN[attr_id] for attr_id, cn in MODULE_ATTR_NAMES.items()}
ATTR_EN_TO_CN: Dict[str, str] = {v.lower(): k for k, v in ATTR_CN_TO_EN.items()}

def normalize_attribute_name(name: str) -> str:
    """将英文/中文属性名归一化为中文；未知则原样返回"""
    if not name:
        return name
    if name in MODULE_ATTR_IDS:
        return name
    key = name.strip().lower()
    return ATTR_EN_TO_CN.get(key, name)

def normalize_attribute_list(names: List[str] | None) -> List[str] | None:
    if names is None:
        return None
    return [normalize_attribute_name(n) for n in names]

def normalize_category(name: str) -> str:
    """将英文/中文分类归一化为中文，无法识别则返回 '全部'"""
    if not name:
        return ModuleCategory.ALL.value
    if name in CATEGORY_CN_TO_EN:
        return name
    key = name.strip().lower()
    return CATEGORY_EN_TO_CN.get(key, ModuleCategory.ALL.value)

def to_english_attr(name_cn: str) -> str:
    """中文属性名转英文显示名；未知则返回原文"""
    return ATTR_CN_TO_EN.get(name_cn, name_cn)

def to_english_module(config_id: int, fallback_cn: str) -> str:
    """根据 config_id 返回英文模组名；未知则返回中文回退"""
    return MODULE_NAMES_EN.get(config_id, fallback_cn)

# 属性阈值和效果等级
ATTR_THRESHOLDS = [1, 4, 8, 12, 16, 20]

# 基础词条战力映射
BASIC_ATTR_POWER_MAP = {
    1: 7,
    2: 14,
    3: 29,
    4: 44,
    5: 167,
    6: 254
}

# 特殊词条战力映射
SPECIAL_ATTR_POWER_MAP = {
    1: 14,
    2: 29,
    3: 59,
    4: 89,
    5: 298,
    6: 448
}

# 模组总属性值战力映射
TOTAL_ATTR_POWER_MAP = {
    0: 0, 1: 5, 2: 11, 3: 17, 4: 23, 5: 29, 6: 34, 7: 40, 8: 46,
    18: 104, 19: 110, 20: 116, 21: 122, 22: 128, 23: 133, 24: 139, 25: 145,
    26: 151, 27: 157, 28: 163, 29: 168, 30: 174, 31: 180, 32: 186, 33: 192,
    34: 198, 35: 203, 36: 209, 37: 215, 38: 221, 39: 227, 40: 233, 41: 238,
    42: 244, 43: 250, 44: 256, 45: 262, 46: 267, 47: 273, 48: 279, 49: 285,
    50: 291, 51: 297, 52: 302, 53: 308, 54: 314, 55: 320, 56: 326, 57: 332,
    58: 337, 59: 343, 60: 349, 61: 355, 62: 361, 63: 366, 64: 372, 65: 378,
    66: 384, 67: 390, 68: 396, 69: 401, 70: 407, 71: 413, 72: 419, 73: 425,
    74: 431, 75: 436, 76: 442, 77: 448, 78: 454, 79: 460, 80: 466, 81: 471,
    82: 477, 83: 483, 84: 489, 85: 495, 86: 500, 87: 506, 88: 512, 89: 518,
    90: 524, 91: 530, 92: 535, 93: 541, 94: 547, 95: 553, 96: 559, 97: 565,
    98: 570, 99: 576, 100: 582, 101: 588, 102: 594, 103: 599, 104: 605, 105: 611,
    106: 617, 113: 658, 114: 664, 115: 669, 116: 675, 117: 681, 118: 687, 119: 693, 120: 699
}

# 基础词条ID列表
BASIC_ATTR_IDS = {
    ModuleAttrType.STRENGTH_BOOST.value,
    ModuleAttrType.AGILITY_BOOST.value,
    ModuleAttrType.INTELLIGENCE_BOOST.value,
    ModuleAttrType.SPECIAL_ATTACK_DAMAGE.value,
    ModuleAttrType.ELITE_STRIKE.value,
    ModuleAttrType.SPECIAL_HEALING_BOOST.value,
    ModuleAttrType.EXPERT_HEALING_BOOST.value,
    ModuleAttrType.CASTING_FOCUS.value,
    ModuleAttrType.ATTACK_SPEED_FOCUS.value,
    ModuleAttrType.CRITICAL_FOCUS.value,
    ModuleAttrType.LUCK_FOCUS.value,
    ModuleAttrType.MAGIC_RESISTANCE.value,
    ModuleAttrType.PHYSICAL_RESISTANCE.value
}

# 特殊词条ID列表
SPECIAL_ATTR_IDS = {
    ModuleAttrType.EXTREME_DAMAGE_STACK.value,
    ModuleAttrType.EXTREME_FLEXIBLE_MOVEMENT.value,
    ModuleAttrType.EXTREME_LIFE_CONVERGENCE.value,
    ModuleAttrType.EXTREME_EMERGENCY_MEASURES.value,
    ModuleAttrType.EXTREME_LIFE_FLUCTUATION.value,
    ModuleAttrType.EXTREME_LIFE_DRAIN.value,
    ModuleAttrType.EXTREME_TEAM_CRIT.value,
    ModuleAttrType.EXTREME_DESPERATE_GUARDIAN.value
}

# 属性名称到类型的映射
ATTR_NAME_TYPE_MAP = {
    "力量加持": "basic",
    "敏捷加持": "basic", 
    "智力加持": "basic",
    "特攻伤害": "basic",
    "精英打击": "basic",
    "特攻治疗加持": "basic",
    "专精治疗加持": "basic",
    "施法专注": "basic",
    "攻速专注": "basic",
    "暴击专注": "basic",
    "幸运专注": "basic",
    "抵御魔法": "basic",
    "抵御物理": "basic",
    "极-伤害叠加": "special",
    "极-灵活身法": "special",
    "极-生命凝聚": "special",
    "极-急救措施": "special",
    "极-生命波动": "special",
    "极-生命汲取": "special",
    "极-全队幸暴": "special",
    "极-绝境守护": "special",
}


@dataclass
class ModulePart:
    """模组部件信息"""
    id: int
    name: str
    value: int


@dataclass(eq=True)
class ModuleInfo:
    """模组信息"""
    name: str
    config_id: int
    uuid: int
    quality: int
    parts: List[ModulePart]
    
    def __hash__(self):
        return hash(self.uuid)
    
    def __lt__(self, other):
        if not isinstance(other, ModuleInfo):
            return NotImplemented
        return self.uuid < other.uuid 