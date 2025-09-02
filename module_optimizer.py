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
import psutil
from module_types import (
    ModuleInfo, ModuleType, ModulePart, ModuleAttrType, ModuleCategory,
    MODULE_CATEGORY_MAP, ATTR_THRESHOLDS, BASIC_ATTR_POWER_MAP, SPECIAL_ATTR_POWER_MAP,
    TOTAL_ATTR_POWER_MAP, BASIC_ATTR_IDS, SPECIAL_ATTR_IDS, ATTR_NAME_TYPE_MAP, MODULE_ATTR_IDS
)
from cpp_extension.module_optimizer_cpp import (
    ModulePart as CppModulePart,
    ModuleInfo as CppModuleInfo,
    ModuleSolution as CppModuleSolution,
    strategy_enumeration_cpp,
    optimize_modules_cpp
)

# 多进程保护, 延迟初始化日志器
logger = None

def _get_logger():
    """延迟获取日志器"""
    global logger
    if logger is None:
        logger = get_logger(__name__)
    return logger


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
    
    def __init__(self, target_attributes: List[str] = None, exclude_attributes: List[str] = None, min_attr_sum_requirements: dict | None = None):
        """初始化模组搭配优化器
        
        Args:
            target_attributes: 目标属性列表，用于优先筛选
            exclude_attributes: 排除属性列表, 用于权重为0
        """
        self.logger = _get_logger()
        self._result_log_file = None
        self.target_attributes = target_attributes or []
        self.exclude_attributes = exclude_attributes or []
        self.min_attr_sum_requirements = min_attr_sum_requirements or {}
        
        self.local_search_iterations = 50  # 局部搜索迭代次数
        self.max_attempts = 20             # 贪心+局部搜索最大尝试次数
        self.max_solutions = 100           # 最大解数量
        self.max_workers = 8               # 最大线程数
        self.enumeration_num = 200         # 并行策略中最大枚举模组数
    
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

    def get_cpu_count(self) -> int:
        """获取CPU核心数"""
        try:
            return psutil.cpu_count(logical=True)
        except (NotImplementedError, OSError, RuntimeError):
            pass
        return 8
    
    def get_module_category(self, module: ModuleInfo) -> ModuleCategory:
        """获取模组类型分类
        
        Args:
            module: 模组信息对象
            
        Returns:
            ModuleCategory: 模组类型分类，默认为攻击类型
        """
        return MODULE_CATEGORY_MAP.get(module.config_id, ModuleCategory.ATTACK)
    
    def _prefilter_modules(self, modules: List[ModuleInfo]) -> Tuple[List[ModuleInfo], List[ModuleInfo]]:
        """预筛选模组，选择高质量候选
        
        Args:
            modules: 所有模组列表
            
        Returns:
            Tuple[List[ModuleInfo], List[ModuleInfo]]: (top_modules, candidate_modules)
                - top_modules: 第一轮筛选出的优质模组, 基于总属性值
                - candidate_modules: 第二轮筛选出的候选模组, 基于各属性值分布
        """
        # 基于总属性值
        top_modules = self._prefilter_modules_by_total_scores(modules, self.enumeration_num)
        
        attr_modules = {}
        for module in modules:
            for part in module.parts:
                attr_name = part.name
                if self.target_attributes:
                    if part.name in self.target_attributes:
                        if attr_name not in attr_modules:
                            attr_modules[attr_name] = []
                        attr_modules[attr_name].append((module, part.value))
                else:
                    if attr_name not in attr_modules:
                        attr_modules[attr_name] = []
                    attr_modules[attr_name].append((module, part.value))
        
        attr_count = len(attr_modules.keys())
        single_attr_num = 90 if attr_count <= 5 else 30

        candidate_modules = top_modules.copy()
        for attr_name, module_values in attr_modules.items():
            sorted_by_attr = sorted(module_values, key=lambda x: x[1], reverse=True)
            top_attr_modules = [item[0] for item in sorted_by_attr[:single_attr_num]]
            candidate_modules.extend(top_attr_modules)
        
        candidate_modules = list(set(candidate_modules))
        
        self.logger.info(f"筛选后模组数量: candidate_modules={len(candidate_modules)} top_modules={len(top_modules)}")
        self.logger.info(f"涉及的属性类型: {list(attr_modules.keys())}")
        return top_modules, candidate_modules
    
    def _prefilter_modules_by_total_scores(self, modules: List[ModuleInfo], num: int) -> List[ModuleInfo]:
        """预筛选模组，选择高质量候选
        
        Args:
            modules: 所有模组列表
            
        Returns:
            List[ModuleInfo] 筛选后的模组
        """
        # 基于总属性值
        module_scores = []
        for module in modules:
            total_attr_sum = 0
            for part in module.parts:
                if self.target_attributes:
                    if part.name in self.target_attributes:
                        total_attr_sum += part.value
                else:
                    total_attr_sum += part.value
            
            module_scores.append((module, total_attr_sum))
        
        sorted_modules = sorted(module_scores, key=lambda x: x[1], reverse=True)
        top_modules = [item[0] for item in sorted_modules[:num]]
        
        return top_modules
    
    def optimize_modules(self, modules: List[ModuleInfo], category: ModuleCategory, top_n: int = 40) -> List[ModuleSolution]:
        """优化模组搭配
        
        Args:
            modules: 所有模组列表
            category: 目标模组类型
            top_n: 返回前N个最优解, 默认40
            
        Returns:
            List[ModuleSolution]: 最优解列表
        """
        self.logger.info(f"开始优化{category.value}类型模组搭配, cpu_count={self.get_cpu_count()}")
        
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
        top_modules, candidate_modules = self._prefilter_modules(filtered_modules)
        greedy_solutions = []
        
        if len(candidate_modules) > self.enumeration_num:
            self.logger.info("并行策略开始")
            num_processes = min(2, mp.cpu_count())

            # 创建进程池, spawn兼容打包环境
            ctx = mp.get_context('spawn')
            with ctx.Pool(processes=num_processes) as pool:
                # 贪心策略
                greedy_future = pool.apply_async(self._strategy_greedy_local_search, (candidate_modules,))
                # 枚举策略
                enum_future = pool.apply_async(self._strategy_enumeration, (top_modules,))
                
                greedy_solutions = greedy_future.get()
                enum_solutions = enum_future.get()
        else:
            # 枚举开始
            enum_solutions = self._strategy_enumeration(top_modules)

        all_solution = greedy_solutions + enum_solutions
        unique_solutions = self._complete_deduplicate(all_solution)
        unique_solutions = self._filter_by_min_attr(unique_solutions)
        unique_solutions.sort(key=lambda x: x.score, reverse=True)
        # 返回前top_n个解
        result = unique_solutions[:top_n]
        
        # 如果使用了目标属性，在最终返回前恢复原始评分
        if self.target_attributes or self.min_attr_sum_requirements:
            result = self._restore_original_scores(result)
        
        self.logger.info(f"优化完成，返回{len(result)}个最优解")
        
        return result
    
    def _filter_by_min_attr(self, solutions: List[ModuleSolution]) -> List[ModuleSolution]:
        """按硬性总和约束过滤解；约束来自 self.min_attr_sum_requirements（键为中文属性名）"""
        if not self.min_attr_sum_requirements:
            return solutions
        req = self.min_attr_sum_requirements
        out = []
        for s in solutions:
            bd = getattr(s, "attr_breakdown", {}) or {}
            ok = True
            for k, v in req.items():
                if bd.get(k, 0) < v:
                    ok = False
                    break
            if ok:
                out.append(s)
        return out


    def enumerate_modules(self, modules: List[ModuleInfo], category: ModuleCategory, top_n: int = 40) -> List[ModuleSolution]:
        """只进行枚举运算
        
        Args:
            modules: 所有模组列表
            category: 目标模组类型
            top_n: 返回前N个最优解, 默认40
            
        Returns:
            List[ModuleSolution]: 最优解列表
        """
        self.logger.info(f"开始优化{category.value}类型模组搭配 cpu_count={self.get_cpu_count()}")
        
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
        
        # 超过500个根据总属性筛下
        if len(filtered_modules) > 500:
            self.logger.info(f"枚举数量超过500, 进行筛选, 筛选后模组数量: {len(filtered_modules)}")
            filtered_modules = self._prefilter_modules_by_total_scores(filtered_modules, 500)
        
        enum_solutions = self._strategy_enumeration(filtered_modules)
        unique_solutions = self._complete_deduplicate(enum_solutions)
        unique_solutions = self._filter_by_min_attr(unique_solutions)
        unique_solutions.sort(key=lambda x: x.score, reverse=True)
        # 返回前top_n个解
        result = unique_solutions[:top_n]
        
        # 如果使用了目标属性，在最终返回前恢复原始评分
        if self.target_attributes:
            result = self._restore_original_scores(result)
        
        self.logger.info(f"优化完成，返回{len(result)}个最优解")
        
        return result
    
    def _strategy_enumeration(self, modules: List[ModuleInfo]) -> List[ModuleSolution]:
        """枚举
        
        Args:
            modules: 模组列表
            
        Returns:
            List[ModuleSolution]: 最优解列表
        """
        
        cpp_modules = self._convert_to_cpp_modules(modules)
        
        # 将目标属性列表转换为集合
        target_attributes_id = []
        if self.target_attributes:
            for attr_str in self.target_attributes:
                aid = MODULE_ATTR_IDS.get(attr_str)
                if aid is not None:
                    target_attributes_id.append(aid)
        target_attrs_set = set(target_attributes_id)
        
        # 将排除属性列表转换为集合
        exclude_attributes_id = []
        if self.exclude_attributes:
            for attr_str in self.exclude_attributes:
                exclude_attributes_id.append(MODULE_ATTR_IDS.get(attr_str))
        exclude_attrs_set = set(exclude_attributes_id)
        
        # 新增：把 -mas （中文名 -> 最小和）转换为 （属性ID -> 最小和），下沉到 C++ 硬过滤
        min_attr_id_requirements: Dict[int, int] = {}
        if self.min_attr_sum_requirements:
            for name, val in self.min_attr_sum_requirements.items():
                aid = MODULE_ATTR_IDS.get(name)
                if aid is not None:
                    min_attr_id_requirements[aid] = int(val)

        cpp_solutions = strategy_enumeration_cpp(
            cpp_modules,
            target_attrs_set,
            exclude_attrs_set,
            min_attr_id_requirements,     # 👈 传入 C++ 的硬约束
            self.max_solutions,
            self.get_cpu_count()
        )
        
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
        
        boost_attr_names: Set[str] = set(self.target_attributes or [])
        if self.min_attr_sum_requirements:
            boost_attr_names.update(self.min_attr_sum_requirements.keys())

        # 将 boost 后的目标属性名 -> id
        target_attributes_id: List[int] = []
        for attr_str in boost_attr_names:
            aid = MODULE_ATTR_IDS.get(attr_str)
            if aid is not None:
                target_attributes_id.append(aid)
        target_attrs_set = set(target_attributes_id)
        
        # 将排除属性列表转换为集合
        exclude_attributes_id = []
        if self.exclude_attributes:
            for attr_str in self.exclude_attributes:
                aid = MODULE_ATTR_IDS.get(attr_str)
                if aid is not None:
                    exclude_attributes_id.append(aid)
        exclude_attrs_set = set(exclude_attributes_id)
        
        cpp_solutions = optimize_modules_cpp(
            cpp_modules, target_attrs_set, exclude_attrs_set, self.max_solutions, self.max_attempts, self.local_search_iterations)
        
        result = self._convert_from_cpp_solutions(cpp_solutions)
        
        return result
    
    def _complete_deduplicate(self, solutions: List[ModuleSolution]) -> List[ModuleSolution]:
        """模组去重++
        
        Args:
            modules: 模组列表
            
        Returns:
            List: 去重后的模组列表
        """
                
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
    
    def _restore_original_scores(self, solutions: List[ModuleSolution]) -> List[ModuleSolution]:
        """恢复原始评分
        
        Args:
            solutions: 包含双倍评分的解决方案列表
            
        Returns:
            List[ModuleSolution]: 恢复原始评分的解决方案列表
        """
        
        restored_solutions = []
        for solution in solutions:
            # 重新计算原始评分
            attr_breakdown = {}
            for module in solution.modules:
                for part in module.parts:
                    attr_breakdown[part.name] = attr_breakdown.get(part.name, 0) + part.value
            
            # 计算原始战斗力
            threshold_power = 0
            total_attr_value = 0
            
            for attr_name, attr_value in attr_breakdown.items():
                total_attr_value += attr_value
                
                # 计算属性等级
                max_level = 0
                for i, threshold in enumerate(ATTR_THRESHOLDS):
                    if attr_value >= threshold:
                        max_level = i + 1
                    else:
                        break
                
                if max_level > 0:
                    attr_type = ATTR_NAME_TYPE_MAP.get(attr_name, "basic")
                    if attr_type == "special":
                        threshold_power += SPECIAL_ATTR_POWER_MAP.get(max_level, 0)
                    else:
                        threshold_power += BASIC_ATTR_POWER_MAP.get(max_level, 0)
            
            # 计算总属性战斗力
            total_attr_power = TOTAL_ATTR_POWER_MAP.get(total_attr_value, 0)
            original_score = threshold_power + total_attr_power
            
            restored_solutions.append(ModuleSolution(
                solution.modules, original_score, attr_breakdown
            ))
        
        return restored_solutions
    
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
                           top_n: int = 40,
                           enumeration_mode: bool = False):
        """优化并显示结果
        
        Args:
            modules: 所有模组列表
            category: 目标模组类型，默认全部
            top_n: 显示前N个最优解, 默认40
            enumeration_mode: 是否启用枚举模式
        Note:
            执行优化算法并格式化显示结果
        """
        print(f"\n{'='*50}")
        self._log_result(f"\n{'='*50}")
        
        print(f"模组搭配优化 - {category.value}类型")
        self._log_result(f"模组搭配优化 - {category.value}类型")
        
        print(f"{'='*50}")
        self._log_result(f"{'='*50}")
        
        if enumeration_mode:
            optimal_solutions = self.enumerate_modules(modules, category, self.max_solutions)
        else:
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
