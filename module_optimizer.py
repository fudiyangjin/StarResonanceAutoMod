"""
模组搭配优化器 - 贪心+局部搜索版本
"""

import logging
import os
import random
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
class ModuleSolution:
    """模组搭配解
    
    Attributes:
        modules: 模组列表
        score: 综合评分
        attr_breakdown: 属性分布
    """
    modules: List[ModuleInfo]
    score: float
    attr_breakdown: Dict[str, int]


class ModuleOptimizer:
    """模组搭配优化器"""
    
    def __init__(self):
        """初始化模组搭配优化器
        """
        self.logger = logger
        self._result_log_file = None
        
        self.local_search_iterations = 30  # 局部搜索迭代次数
        self.max_solutions = 40            # 最大解数量
        
        # 属性等级权重
        self.level_weights = {
            1: 1.0,   # 1级权重
            2: 2.0,   # 2级权重
            3: 3.0,   # 3级权重
            4: 4.0,   # 4级权重
            5: 20.0,  # 5级权重
            6: 30.0   # 6级权重
        }
    
    def _get_current_log_file(self) -> Optional[str]:
        """获取当前日志文件路径
        
        Returns:
            Optional[str]: 日志文件路径, 如果未找到则返回 None
            
        Raises:
            Exception: 获取日志文件路径时可能出现的异常
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
        """记录筛选结果到日志文件
        
        Args:
            message: 要记录的消息内容
            
        Raises:
            Exception: 写入日志文件时可能出现的异常
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
            ModuleCategory: 模组类型分类，默认为攻击类型
        """
        return MODULE_CATEGORY_MAP.get(module.config_id, ModuleCategory.ATTACK)
    
    def prefilter_modules(self, modules: List[ModuleInfo]) -> List[ModuleInfo]:
        """预筛选模组，选择高质量候选
        
        Args:
            modules: 所有模组列表
            
        Returns:
            List[ModuleInfo]: 筛选后的候选模组列表
        """
        self.logger.info(f"开始预筛选，原始模组数量: {len(modules)}")
        
        # 统计所有出现的属性类型
        attr_modules = {}
        for module in modules:
            for part in module.parts:
                attr_name = part.name
                if attr_name not in attr_modules:
                    attr_modules[attr_name] = []
                attr_modules[attr_name].append((module, part.value))
        
        # 取每种属性的前30个模组
        candidate_modules = set()
        for attr_name, module_values in attr_modules.items():
            sorted_by_attr = sorted(module_values, key=lambda x: x[1], reverse=True)
            top_modules = [item[0] for item in sorted_by_attr[:30]]
            candidate_modules.update(top_modules)
        
        filtered_modules = list(candidate_modules)
        
        self.logger.info(f"预筛选完成，筛选后模组数量: {len(filtered_modules)}")
        self.logger.info(f"涉及的属性类型: {list(attr_modules.keys())}")
        return filtered_modules
    
    def calculate_solution_score(self, modules: List[ModuleInfo]) -> Tuple[float, Dict[str, int]]:
        """计算模组搭配的综合评分
        
        Args:
            modules: 模组列表
            
        Returns:
            Tuple[float, Dict[str, int]]: (综合评分, 属性分布字典)
        """
        # 计算属性分布
        attr_breakdown = {}
        for module in modules:
            for part in module.parts:
                attr_name = part.name
                if attr_name in attr_breakdown:
                    attr_breakdown[attr_name] += part.value
                else:
                    attr_breakdown[attr_name] = part.value
        
        # 计算属性分数
        high_level_score = 0.0
        for attr_name, attr_value in attr_breakdown.items():
            # 获得属性等级
            threshold_level = 0
            for i, threshold in enumerate(ATTR_THRESHOLDS):
                if attr_value >= threshold:
                    threshold_level = i + 1 
                else:
                    break
            
            # 计算权重得分
            if threshold_level > 0:
                weight = self.level_weights.get(threshold_level, 1.0)
                high_level_score += weight
        
        # 计算总属性值
        total_value = sum(attr_breakdown.values())
        
        # 计算属性浪费
        total_waste = 0.0
        for attr_name, attr_value in attr_breakdown.items():
            # 找到该属性值能达到的最高阈值
            max_threshold = 0
            for threshold in ATTR_THRESHOLDS:
                if attr_value >= threshold:
                    max_threshold = threshold
                else:
                    break
            
            # 计算超过阈值的浪费
            waste = attr_value - max_threshold
            total_waste += waste
        
        # 综合评分：90%高等级属性 + 5%总属性值 - 5%属性浪费
        score = 0.9 * high_level_score + 0.05 * total_value - 0.05 * total_waste
        
        return score, attr_breakdown
    
    def greedy_construct_solution(self, modules: List[ModuleInfo]) -> ModuleSolution:
        """初始值构造
        
        Args:
            modules: 候选模组列表
            
        Returns:
            ModuleSolution: 构造的初始解, 如果模组数量不足则返回None
        """
        if len(modules) < 4:
            return None
        
        # 随机选择起始模组
        current_modules = [random.choice(modules)]
        
        # 贪心添加模组
        for _ in range(3):
            candidates = []
            candidate_scores = []
            
            for module in modules:
                if module in current_modules:
                    continue
                
                test_modules = current_modules + [module]
                score, _ = self.calculate_solution_score(test_modules)
                
                candidates.append(module)
                candidate_scores.append(score)
            
            if not candidates:
                break
            
            # 80%概率选择最优，20%概率随机选择前3个
            if random.random() < 0.8:
                # 选择最优模组
                best_idx = candidate_scores.index(max(candidate_scores))
                best_module = candidates[best_idx]
            else:
                # 随机选择前3个候选
                top_candidates = sorted(zip(candidate_scores, candidates), reverse=True)[:3]
                best_module = random.choice([c for _, c in top_candidates])
            
            current_modules.append(best_module)
        
        # 计算最终评分
        final_score, attr_breakdown = self.calculate_solution_score(current_modules)
        
        return ModuleSolution(
            modules=current_modules,
            score=final_score,
            attr_breakdown=attr_breakdown
        )
    
    def local_search_improve(self, solution: ModuleSolution, all_modules: List[ModuleInfo]) -> ModuleSolution:
        """局部搜索改进解
        
        Args:
            solution: 初始解
            all_modules: 所有候选模组列表
            
        Returns:
            ModuleSolution: 改进后的解
        """
        best_solution = solution
        
        for iteration in range(self.local_search_iterations):
            improved = False
            
            # 尝试替换每个模组
            for i in range(len(best_solution.modules)):
                # 随机选择候选模组
                candidates = random.sample(all_modules, min(20, len(all_modules)))
                
                for new_module in candidates:
                    if new_module in best_solution.modules:
                        continue
                    
                    # 尝试替换
                    new_modules = best_solution.modules.copy()
                    new_modules[i] = new_module
                    
                    new_score, new_attr_breakdown = self.calculate_solution_score(new_modules)
                    
                    if new_score > best_solution.score:
                        best_solution = ModuleSolution(
                            modules=new_modules,
                            score=new_score,
                            attr_breakdown=new_attr_breakdown
                        )
                        improved = True
                        break
                
                if improved:
                    break
            
            # 如果连续没有改进，提前结束
            if not improved and iteration > self.local_search_iterations // 2:
                break
        
        return best_solution
    
    def optimize_modules(self, modules: List[ModuleInfo], category: ModuleCategory, top_n: int = 40) -> List[ModuleSolution]:
        """优化模组搭配
        
        Args:
            modules: 所有模组列表
            category: 目标模组类型
            top_n: 返回前N个最优解, 默认40
            
        Returns:
            List[ModuleSolution]: 最优解列表
        """
        self.logger.info(f"开始优化{category.value}类型模组搭配")
        
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
        
        # 筛选模组
        candidate_modules = self.prefilter_modules(filtered_modules)
        
        solutions = []
        seen_combinations = set()
        
        attempts = 0
        max_attempts = self.max_solutions * 20
        
        while len(solutions) < self.max_solutions and attempts < max_attempts:
            attempts += 1
            
            # 贪心构造初始解
            initial_solution = self.greedy_construct_solution(candidate_modules)
            if not initial_solution:
                continue
            
            # 局部搜索
            improved_solution = self.local_search_improve(initial_solution, candidate_modules)
            
            # 去重
            improved_module_ids = tuple(sorted([module.uuid for module in improved_solution.modules]))
            if improved_module_ids not in seen_combinations:
                seen_combinations.add(improved_module_ids)
                solutions.append(improved_solution)
        
        # 按评分排序
        solutions.sort(key=lambda x: x.score, reverse=True)
        
        # 返回前top_n个解
        result = solutions[:top_n]
        self.logger.debug(f"第{attempts}次尝试，找到{len(result)}个最优解")
        self.logger.info(f"优化完成，返回{len(result)}个最优解")
        
        return result
    
    def print_solution_details(self, solution: ModuleSolution, rank: int):
        """打印解详细信息
        
        Args:
            solution: 模组搭配解
            rank: 排名
            
        Note:
            同时输出到控制台和日志文件
        """
        print(f"\n=== 第{rank}名搭配 ===")
        self._log_result(f"\n=== 第{rank}名搭配 ===")
        
        total_value = sum(solution.attr_breakdown.values())
        
        print(f"总属性值: {total_value}")
        self._log_result(f"总属性值: {total_value}")
        
        print(f"综合评分: {solution.score:.2f}")
        self._log_result(f"综合评分: {solution.score:.2f}")
        
        print("\n模组列表:")
        self._log_result("\n模组列表:")
        for i, module in enumerate(solution.modules, 1):
            parts_str = ", ".join([f"{p.name}+{p.value}" for p in module.parts])
            print(f"  {i}. {module.name} (品质{module.quality}) - {parts_str}")
            self._log_result(f"  {i}. {module.name} (品质{module.quality}) - {parts_str}")
        
        print("\n属性分布:")
        self._log_result("\n属性分布:")
        for attr_name, value in sorted(solution.attr_breakdown.items()):
            print(f"  {attr_name}: +{value}")
            self._log_result(f"  {attr_name}: +{value}")
    
    def optimize_and_display(self, 
                           modules: List[ModuleInfo], 
                           category: ModuleCategory = ModuleCategory.ALL,
                           top_n: int = 40):
        """优化并显示结果
        
        Args:
            modules: 所有模组列表
            category: 目标模组类型，默认全部
            top_n: 显示前N个最优解，默认40
            
        Note:
            执行优化算法并格式化显示结果
        """
        print(f"\n{'='*50}")
        self._log_result(f"\n{'='*50}")
        
        print(f"模组搭配优化 - {category.value}类型")
        self._log_result(f"模组搭配优化 - {category.value}类型")
        
        print(f"{'='*50}")
        self._log_result(f"{'='*50}")
        
        optimal_solutions = self.optimize_modules(modules, category, top_n)
        
        if not optimal_solutions:
            print(f"未找到{category.value}类型的有效搭配")
            self._log_result(f"未找到{category.value}类型的有效搭配")
            return
        
        print(f"\n找到{len(optimal_solutions)}个最优搭配:")
        self._log_result(f"\n找到{len(optimal_solutions)}个最优搭配:")
        
        for i, solution in enumerate(optimal_solutions, 1):
            self.print_solution_details(solution, i)
        
        # 显示统计信息
        print(f"\n{'='*50}")
        self._log_result(f"\n{'='*50}")
        
        print("统计信息:")
        self._log_result("统计信息:")
        
        print(f"总模组数量: {len(modules)}")
        self._log_result(f"总模组数量: {len(modules)}")
        
        print(f"{category.value}类型模组: {len([m for m in modules if self.get_module_category(m) == category])}")
        self._log_result(f"{category.value}类型模组: {len([m for m in modules if self.get_module_category(m) == category])}")
        
        print(f"最高评分: {optimal_solutions[0].score:.2f}")
        self._log_result(f"最高评分: {optimal_solutions[0].score:.2f}")
        
        print(f"{'='*50}")
        self._log_result(f"{'='*50}")
