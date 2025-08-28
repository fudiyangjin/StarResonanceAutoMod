"""
模组搭配优化器 - 多策略并行, 使用C++进行核心运算
"""

import logging
import os
import random
import multiprocessing as mp
from typing import Dict, List, Optional, Tuple, Set
from dataclasses import dataclass
from itertools import combinations
from logging_config import get_logger
from module_types import (
    ModuleInfo, ModuleType, ModulePart, ModuleAttrType, ModuleCategory,
    MODULE_CATEGORY_MAP, ATTR_THRESHOLDS, BASIC_ATTR_POWER_MAP, SPECIAL_ATTR_POWER_MAP,
    TOTAL_ATTR_POWER_MAP, BASIC_ATTR_IDS, SPECIAL_ATTR_IDS, ATTR_NAME_TYPE_MAP
)
from cpp_extension.module_optimizer_cpp import (
    ModulePart as CppModulePart,
    ModuleInfo as CppModuleInfo,
    ModuleSolution as CppModuleSolution,
    strategy_enumeration_cpp,
    optimize_modules_cpp
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
    
    def __init__(self, target_attributes: List[str] = None):
        """初始化模组搭配优化器
        
        Args:
            target_attributes: 目标属性列表，用于优先筛选
        """
        self.logger = logger
        self._result_log_file = None
        self.target_attributes = target_attributes or []
        
        self.local_search_iterations = 50  # 局部搜索迭代次数
        self.max_attempts = 20             # 贪心+局部搜索最大尝试次数
        self.max_solutions = 100           # 最大解数量
        self.max_workers = 8               # 最大线程数
        self.enumeration_num = 150         # 并行策略中最大枚举模组数
    
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
        
        if self.target_attributes:
            self.logger.info(f"使用指定属性优先级筛选: {self.target_attributes}")
            return self._prefilter_by_target_attributes(modules)
        else:
            return self._prefilter_by_all_attributes(modules)
    
    def _prefilter_by_target_attributes(self, modules: List[ModuleInfo]) -> List[ModuleInfo]:
        """基于指定属性进行预筛选
        
        Args:
            modules: 所有模组列表
            
        Returns:
            List[ModuleInfo]: 筛选后的候选模组列表
        """
        
        # 根据总属性取优质模组
        module_scores = []
        for module in modules:
            target_attr_sum = 0
            for part in module.parts:
                if part.name in self.target_attributes:
                    target_attr_sum += part.value
            
            module_scores.append((module, target_attr_sum))
        
        sorted_modules = sorted(module_scores, key=lambda x: x[1], reverse=True)
        top_modules = [item[0] for item in sorted_modules[:self.enumeration_num]]
        
        # 每种属性再取优质模组
        attr_modules = {}
        for module in modules:
            for part in module.parts:
                if part.name in self.target_attributes:
                    if part.name not in attr_modules:
                        attr_modules[part.name] = []
                    attr_modules[part.name].append((module, part.value))
        
        for attr_name, module_values in attr_modules.items():
            sorted_by_attr = sorted(module_values, key=lambda x: x[1], reverse=True)
            top_attr_modules = [item[0] for item in sorted_by_attr[:30]]
            top_modules.extend(top_attr_modules)
        
        # 去重
        candidate_modules = list(set(top_modules))
        
        self.logger.info(f"基于指定属性筛选完成，筛选后模组数量: {len(candidate_modules)}")
        self.logger.info(f"涉及的指定属性: {list(attr_modules.keys())}")
        return candidate_modules
    
    def _prefilter_by_all_attributes(self, modules: List[ModuleInfo]) -> List[ModuleInfo]:
        """基于所有属性进行预筛选
        
        Args:
            modules: 所有模组列表
            
        Returns:
            List[ModuleInfo]: 筛选后的候选模组列表
        """
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
        
        self.logger.info(f"基于所有属性筛选完成，筛选后模组数量: {len(filtered_modules)}")
        self.logger.info(f"涉及的属性类型: {list(attr_modules.keys())}")
        return filtered_modules
    
    def _prefilter_for_enumeration(self, modules: List[ModuleInfo]) -> List[ModuleInfo]:
        """返回enumeration_num个属性分布平均的模组
        
        Args:
            modules: 所有模组列表
            
        Returns:
            List[ModuleInfo]: 筛选后的enumeration_num个候选模组列表
        """
        
        if len(modules) <= self.enumeration_num:
            return modules

        attr_modules = {}
        for module in modules:
            for part in module.parts:
                attr_name = part.name
                if attr_name not in attr_modules:
                    attr_modules[attr_name] = []
                attr_modules[attr_name].append((module, part.value))
        
        attr_count = len(attr_modules)
        if attr_count == 0:
            return modules[:self.enumeration_num] if len(modules) > self.enumeration_num else modules
        
        # 为每种属性分配模组数量，确保总数接近self.enumeration_num
        target_per_attr = max(1, self.enumeration_num // attr_count)
        remaining_slots = self.enumeration_num - (target_per_attr * attr_count)
        
        candidate_modules = []
        used_modules = set()
        
        # 按属性类型分配模组
        for attr_name, module_values in attr_modules.items():
            sorted_by_attr = sorted(module_values, key=lambda x: x[1], reverse=True)
            
            current_target = target_per_attr
            if remaining_slots > 0:
                current_target += 1
                remaining_slots -= 1
            
            available_modules = [item[0] for item in sorted_by_attr[:current_target]]
            candidate_modules.extend(available_modules)
            used_modules.update(available_modules)
        
        # 去重
        unique_modules = list(set(candidate_modules))
        
        # 可能筛完模组数量不足enumeration_num了, 补充到enumeration_num
        if len(unique_modules) < self.enumeration_num:
            remaining_modules = [m for m in modules if m not in used_modules]
            
            # 总属性值排序剩余模组
            remaining_scores = []
            for module in remaining_modules:
                total_value = sum(part.value for part in module.parts)
                remaining_scores.append((module, total_value))
            
            remaining_scores.sort(key=lambda x: x[1], reverse=True)
            
            # 补充模组
            needed_count = self.enumeration_num - len(unique_modules)
            for i in range(min(needed_count, len(remaining_scores))):
                unique_modules.append(remaining_scores[i][0])
        
        if len(unique_modules) > self.enumeration_num:
            unique_modules = unique_modules[:self.enumeration_num]
        
        self.logger.debug(f"枚举策略预筛选完成，筛选后模组数量: {len(unique_modules)}")
        self.logger.debug(f"涉及的属性类型: {list(attr_modules.keys())}")
        self.logger.debug(f"每种属性平均分配模组数量: {target_per_attr}")
        
        return unique_modules
    
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
        greedy_solutions = []
        if len(candidate_modules) > self.enumeration_num:
            self.logger.info("并行策略开始")
            num_processes = min(2, mp.cpu_count())

            # 创建进程池
            with mp.Pool(processes=num_processes) as pool:

                greedy_future = pool.apply_async(self._strategy_greedy_local_search, (filtered_modules,))
                enum_future = pool.apply_async(self._strategy_enumeration, (filtered_modules,))
                
                greedy_solutions = greedy_future.get()
                enum_solutions = enum_future.get()
        else:
            # 枚举开始
            enum_solutions = self._strategy_enumeration(filtered_modules)

        all_solution = greedy_solutions + enum_solutions
        unique_solutions = self._complete_deduplicate(all_solution)
        unique_solutions.sort(key=lambda x: x.score, reverse=True)
        # 返回前top_n个解
        result = unique_solutions[:top_n]
        self.logger.info(f"优化完成，返回{len(result)}个最优解")
        
        return result
    
    def _strategy_enumeration(self, modules: List[ModuleInfo]) -> List[ModuleSolution]:
        """枚举
        
        Args:
            modules: 所有模组列表
            
        Returns:
            List[ModuleSolution]: 最优解列表
        """
 
        # 为枚举策略专门筛选enumeration_num个属性分布平均的优质模组
        candidate_modules = self._prefilter_for_enumeration(modules)
        
        cpp_modules = self._convert_to_cpp_modules(candidate_modules)
        
        cpp_solutions = strategy_enumeration_cpp(cpp_modules, self.max_solutions, self.max_workers)
        
        result = self._convert_from_cpp_solutions(cpp_solutions)

        return result
    
    def _strategy_greedy_local_search(self, modules: List[ModuleInfo]) -> List[ModuleSolution]:
        """贪心+局部搜索
        
        Args:
            modules: 所有模组列表
            
        Returns:
            List[ModuleSolution]: 最优解列表
        """
        
        cpp_modules = self._convert_to_cpp_modules(modules)
        
        cpp_solutions = optimize_modules_cpp(cpp_modules, self.max_solutions, self.max_attempts, self.local_search_iterations)
        
        result = self._convert_from_cpp_solutions(cpp_solutions)
        
        return result
    
    def _complete_deduplicate(self, solutions: List[ModuleSolution]) -> List[ModuleSolution]:
        """模组去重++
        
        Args:
            modules: 模组列表
            
        Returns:
            List: 去重后的模组列表
        """
        
        solutions.sort(key=lambda x: x.score, reverse=True)
        
        unique_solutions = []
        seen_combinations = set()
        
        for solution in solutions:
            module_ids = tuple(sorted([module.uuid for module in solution.modules]))
            if module_ids not in seen_combinations:
                seen_combinations.add(module_ids)
                unique_solutions.append(solution)
        
        return unique_solutions
    
    def _convert_to_cpp_modules(self, modules: List[ModuleInfo]) -> List:
        """python数据结构转C++
        
        Args:
            modules: 模组列表
            
        Returns:
            List: C++模组结构
        """

        cpp_modules = []
        for module in modules:
            cpp_parts = []
            for part in module.parts:
                cpp_parts.append(CppModulePart(int(part.id), part.name, int(part.value)))
            cpp_modules.append(CppModuleInfo(
                module.name, module.config_id, module.uuid, 
                module.quality, cpp_parts
            ))
        return cpp_modules
    
    def _convert_from_cpp_solutions(self, cpp_solutions: List) -> List[ModuleSolution]:
        """C++数据结构转python
        
        Args:
            modules: 模组列表
            
        Returns:
            List: python模组结构
        """
        
        solutions = []
        for cpp_solution in cpp_solutions:
            modules = []
            for cpp_module in cpp_solution.modules:
                parts = []
                for cpp_part in cpp_module.parts:
                    parts.append(ModulePart(cpp_part.id, cpp_part.name, cpp_part.value))
                modules.append(ModuleInfo(
                    cpp_module.name, cpp_module.config_id, cpp_module.uuid,
                    cpp_module.quality, parts
                ))
            solutions.append(ModuleSolution(
                modules, cpp_solution.score, cpp_solution.attr_breakdown
            ))
        return solutions

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
        
        print(f"战斗力: {solution.score:.2f}")
        self._log_result(f"战斗力: {solution.score:.2f}")
        
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
            top_n: 显示前N个最优解, 默认40
            
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
        
        print(f"最高战斗力: {optimal_solutions[0].score:.2f}")
        self._log_result(f"最高战斗力: {optimal_solutions[0].score:.2f}")
        
        print(f"{'='*50}")
        self._log_result(f"{'='*50}")
