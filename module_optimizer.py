"""
模组搭配优化器
"""

import logging
import os
from typing import Dict, List, Optional, Tuple, Set
from dataclasses import dataclass
from itertools import combinations
from logging_config import get_logger
from module_types import (
    ModuleInfo, ModuleType, ModuleAttrType, ModuleCategory,
    MODULE_CATEGORY_MAP, ATTR_THRESHOLDS
)

# 获取日志器
logger = get_logger(__name__)


@dataclass
class ModuleCombination:
    """模组搭配组合
    
    Attributes:
        modules: 模组列表
        total_attr_value: 总属性值
        attr_breakdown: 属性分布字典，键为属性名称，值为属性数值
        threshold_level: 达到的属性(0-5)
        score: 综合评分（数值越大越好）
    """
    modules: List[ModuleInfo]
    total_attr_value: int
    attr_breakdown: Dict[str, int]  # 属性名称 -> 数值
    threshold_level: int  # 达到的属性
    score: float  # 综合评分


class ModuleOptimizer:
    """模组搭配优化器"""
    
    def __init__(self):
        self.logger = logger
        self._result_log_file = None
    
    def _get_current_log_file(self) -> Optional[str]:
        """获取当前日志文件路径
        
        Returns:
            当前日志文件路径, 如果无法获取则返回None
        """
        try:
            root_logger = logging.getLogger()
            for handler in root_logger.handlers:
                if isinstance(handler, logging.FileHandler):
                    return handler.baseFilename
            return None
        except Exception as e:
            self.logger.warning(f"无法获取日志文件路径: {e}")
            return None
    
    def _log_result(self, message: str):
        """记录筛选结果到日志文件, 不带前缀
        
        Args:
            message: 要记录的消息
        """
        try:
            if self._result_log_file is None:
                self._result_log_file = self._get_current_log_file()
            
            if self._result_log_file and os.path.exists(self._result_log_file):
                with open(self._result_log_file, 'a', encoding='utf-8') as f:
                    f.write(message + '\n')
        except Exception as e:
            self.logger.warning(f"记录筛选结果失败: {e}")
    
    def get_module_category(self, module: ModuleInfo) -> ModuleCategory:
        """获取模组类型分类
        
        Args:
            module: 模组信息对象
            
        Returns:
            模组类型分类（攻击/守护/辅助）
        """
        return MODULE_CATEGORY_MAP.get(module.config_id, ModuleCategory.ATTACK)
    
    def calculate_total_attr_value(self, modules: List[ModuleInfo]) -> Tuple[int, Dict[str, int]]:
        """计算模组组合的总属性值
        
        Args:
            modules: 模组列表
            
        Returns:
            (总属性值, 属性分布字典)
        """
        attr_breakdown = {}
        
        for module in modules:
            for part in module.parts:
                attr_name = part.name
                if attr_name in attr_breakdown:
                    attr_breakdown[attr_name] += part.value
                else:
                    attr_breakdown[attr_name] = part.value
        
        # 按照阈值计算总属性值
        total_threshold_value = 0
        for attr_name, attr_value in attr_breakdown.items():
            # 找到该属性值能达到的最高阈值
            threshold_value = 0
            for threshold in ATTR_THRESHOLDS:
                if attr_value >= threshold:
                    threshold_value = threshold
                else:
                    break
            total_threshold_value += threshold_value
        
        return total_threshold_value, attr_breakdown
    
    def calculate_multi_attr_threshold_score(self, attr_breakdown: Dict[str, int]) -> Tuple[int, Dict[str, int]]:
        """计算多属性属性评分
        
        Args:
            attr_breakdown: 属性分布字典，键为属性名称，值为属性数值
            
        Returns:
            (总属性分数, 各属性属性等级字典)
        """
        attr_threshold_levels = {}
        total_threshold_score = 0
        
        # 计算每个属性达到的属性等级
        for attr_name, attr_value in attr_breakdown.items():
            threshold_level = 0
            for i, threshold in enumerate(ATTR_THRESHOLDS):
                if attr_value >= threshold:
                    threshold_level = i
                else:
                    break
            
            attr_threshold_levels[attr_name] = threshold_level
            
            # 属性等级越高，分数越高
            threshold_score = 10 ** threshold_level if threshold_level > 0 else 0
            total_threshold_score += threshold_score
        
        return total_threshold_score, attr_threshold_levels

    def get_effective_threshold_level(self, attr_breakdown: Dict[str, int]) -> int:
        """获取有效属性等级
        
        Args:
            attr_breakdown: 属性分布字典，键为属性名称，值为属性数值
            
        Returns:
            有效属性等级(0-5)
        """
        threshold_score, _ = self.calculate_multi_attr_threshold_score(attr_breakdown)
        
        # 将属性分数转换为等级
        # 100000分 = 等级5, 10000分 = 等级4, 1000分 = 等级3, 100分 = 等级2, 10分 = 等级1
        if threshold_score >= 100000:
            return 5
        elif threshold_score >= 10000:
            return 4
        elif threshold_score >= 1000:
            return 3
        elif threshold_score >= 100:
            return 2
        elif threshold_score >= 10:
            return 1
        else:
            return 0

    def calculate_combination_score(self, combination: ModuleCombination) -> float:
        """计算组合的综合评分
        
        Args:
            combination: 模组组合对象
            
        Returns:
            综合评分
        """

        # 计算多属性评分
        threshold_score, attr_threshold_levels = self.calculate_multi_attr_threshold_score(combination.attr_breakdown)
        
        # 计算达到高属性的属性数量（属性等级>=4的属性）
        high_threshold_attrs = sum(1 for level in attr_threshold_levels.values() if level >= 4)
        
        # 多属性属性分数
        multi_attr_score = threshold_score * 1000
        
        # 高属性属性数量奖励（鼓励多属性达到高属性）
        high_threshold_bonus = high_threshold_attrs * 50000
        
        # 总属性值分数
        total_attr_score = combination.total_attr_value
        
        # 综合评分
        total_score = multi_attr_score + high_threshold_bonus + total_attr_score
        
        return total_score
   
    def find_optimal_combinations_greedy(self, modules: List[ModuleInfo], category: ModuleCategory, top_n: int = 20) -> List[ModuleCombination]:
        """贪心筛选
        
        Args:
            modules: 所有模组列表
            category: 目标模组类型（攻击/守护/辅助）
            top_n: 返回前N个最优组合, 默认20
            
        Returns:
            最优模组组合列表，按评分排序
        """
        self.logger.info(f"开始计算{category.value}类型模组的最优搭配")
        
        # 过滤指定类型的模组
        if category == ModuleCategory.ALL:
            filtered_modules = modules
            self.logger.info(f"使用全部模组，共{len(filtered_modules)}个")
        else:
            filtered_modules = [
                module for module in modules 
                if self.get_module_category(module) == category
            ]
            self.logger.info(f"找到{len(filtered_modules)}个{category.value}类型模组")
        
        if len(filtered_modules) < 4:
            self.logger.warning(f"{category.value}类型模组数量不足4个, 无法形成完整搭配")
            return []
        
        # 按模组总属性值排序，选择前20个最好的模组
        module_scores = []
        for module in filtered_modules:
            # 计算单个模组的总属性值
            total_value = sum(part.value for part in module.parts)
            module_scores.append((module, total_value))
        
        # 按属性值排序，取前20个
        module_scores.sort(key=lambda x: x[1], reverse=True)
        top_modules = [ms[0] for ms in module_scores[:20]]
        
        self.logger.info(f"选择前20个最佳模组进行组合计算")
        
        # 只对前20个模组生成组合
        combinations_list = list(combinations(top_modules, 4))
        self.logger.info(f"生成{len(combinations_list)}个4模组组合")
        
        # 计算每个组合的属性值和评分
        module_combinations = []
        
        for combo in combinations_list:
            total_value, attr_breakdown = self.calculate_total_attr_value(list(combo))
            effective_threshold_level = self.get_effective_threshold_level(attr_breakdown)
            
            combination = ModuleCombination(
                modules=list(combo),
                total_attr_value=total_value,
                attr_breakdown=attr_breakdown,
                threshold_level=effective_threshold_level,
                score=0.0
            )
            
            combination.score = self.calculate_combination_score(combination)
            module_combinations.append(combination)
        
        # 按多级排序：先按属性等级，再按评分，最后按总属性值
        module_combinations.sort(key=lambda x: (x.threshold_level, x.score, x.total_attr_value), reverse=True)
        return module_combinations[:top_n]

    def print_combination_details(self, combination: ModuleCombination, rank: int):
        """打印组合详细信息
        
        Args:
            combination: 模组组合对象
            rank: 排名（第几名）
        """
        print(f"\n=== 第{rank}名搭配 ===")
        self._log_result(f"\n=== 第{rank}名搭配 ===")
        
        print(f"总属性值: {combination.total_attr_value}")
        self._log_result(f"总属性值: {combination.total_attr_value}")
        
        print(f"达到属性等级等级: {combination.threshold_level} ({ATTR_THRESHOLDS[combination.threshold_level]}点)")
        self._log_result(f"达到属性等级等级: {combination.threshold_level} ({ATTR_THRESHOLDS[combination.threshold_level]}点)")
        
        print(f"综合评分: {combination.score:.1f}")
        self._log_result(f"综合评分: {combination.score:.1f}")
        
        print("\n模组列表:")
        self._log_result("\n模组列表:")
        for i, module in enumerate(combination.modules, 1):
            parts_str = ", ".join([f"{p.name}+{p.value}" for p in module.parts])
            print(f"  {i}. {module.name} (品质{module.quality}) - {parts_str}")
            self._log_result(f"  {i}. {module.name} (品质{module.quality}) - {parts_str}")
        
        print("\n属性分布:")
        self._log_result("\n属性分布:")
        for attr_name, value in sorted(combination.attr_breakdown.items()):
            print(f"  {attr_name}: +{value}")
            self._log_result(f"  {attr_name}: +{value}")
    
    def optimize_and_display(self, 
                           modules: List[ModuleInfo], 
                           category: ModuleCategory = ModuleCategory.ALL,
                           top_n: int = 20):
        """优化并显示结果
        
        Args:
            modules: 所有模组列表
            category: 目标模组类型（攻击/守护/辅助/全部），默认全部
            top_n: 显示前N个最优组合, 默认20
        """
        print(f"\n{'='*50}")
        self._log_result(f"\n{'='*50}")
        
        print(f"模组搭配优化 - {category.value}类型")
        self._log_result(f"模组搭配优化 - {category.value}类型")
        
        print(f"{'='*50}")
        self._log_result(f"{'='*50}")
        
        optimal_combinations = self.find_optimal_combinations_greedy(modules, category, top_n)
        
        if not optimal_combinations:
            print(f"未找到{category.value}类型的有效搭配")
            self._log_result(f"未找到{category.value}类型的有效搭配")
            return
        
        print(f"\n找到{len(optimal_combinations)}个最优搭配:")
        self._log_result(f"\n找到{len(optimal_combinations)}个最优搭配:")
        
        for i, combination in enumerate(optimal_combinations, 1):
            self.print_combination_details(combination, i)
        
        # 显示统计信息
        print(f"\n{'='*50}")
        self._log_result(f"\n{'='*50}")
        
        print("统计信息:")
        self._log_result("统计信息:")
        
        print(f"总模组数量: {len(modules)}")
        self._log_result(f"总模组数量: {len(modules)}")
        
        print(f"{category.value}类型模组: {len([m for m in modules if self.get_module_category(m) == category])}")
        self._log_result(f"{category.value}类型模组: {len([m for m in modules if self.get_module_category(m) == category])}")
        
        print(f"最高属性总值: {optimal_combinations[0].total_attr_value}")
        self._log_result(f"最高属性总值: {optimal_combinations[0].total_attr_value}")
        
        print(f"最高属性: {optimal_combinations[0].threshold_level}")
        self._log_result(f"最高属性: {optimal_combinations[0].threshold_level}")
        
        print(f"{'='*50}")
        self._log_result(f"{'='*50}")

