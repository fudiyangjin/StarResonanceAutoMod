"""
模组解析器
"""

import json
import logging
from typing import Dict, List, Optional, Any
from BlueProtobuf_pb2 import CharSerialize
from logging_config import get_logger
from module_types import (
    ModuleInfo, ModulePart, ModuleType, ModuleAttrType, ModuleCategory,
    MODULE_NAMES, MODULE_ATTR_NAMES, MODULE_CATEGORY_MAP
)
from module_optimizer import ModuleOptimizer

# 获取日志器
logger = get_logger(__name__)


class ModuleParser:
    """模组解析器"""
    
    def __init__(self):
        self.logger = logger
    
    def parse_module_info(self, v_data: CharSerialize, category: str = "全部", attributes: List[str] = None, 
                         exclude_attributes: List[str] = None, match_count: int = 1, enumeration_mode: bool = False):
        """
        解析模组信息

        Args:
            v_data: VData数据
            category: 模组类型（攻击/守护/辅助/全部）
            attributes: 要筛选的属性词条列表
            exclude_attributes: 要排除的属性词条列表
            match_count: 模组需要包含的指定词条数量
            enumeration_mode: 是否启用枚举模式
        """
        self.logger.info("开始解析模组")
        
        mod_infos = v_data.Mod.ModInfos

        modules = []
        for package_type, package in v_data.ItemPackage.Packages.items():
            for key, item in package.Items.items():
                if item.HasField('ModNewAttr') and item.ModNewAttr.ModParts:
                    config_id = item.ConfigId
                    module_name = MODULE_NAMES.get(config_id, f"未知模组({config_id})")
                    mod_parts = list(item.ModNewAttr.ModParts)
                    # 查找模组详细信息
                    mod_info = mod_infos.get(key) if mod_infos else None

                    module_info = ModuleInfo(
                        name=module_name,
                        config_id=config_id,
                        uuid=item.Uuid,
                        quality=item.Quality,
                        parts=[]
                    )

                    init_link_nums = mod_info.InitLinkNums
                    for i, part_id in enumerate(mod_parts):
                        if i < len(init_link_nums):
                            attr_name = MODULE_ATTR_NAMES.get(part_id, f"未知属性({part_id})")
                            attr_value = init_link_nums[i]
                            module_part = ModulePart(
                                id=part_id,
                                name=attr_name,
                                value=attr_value
                            )
                            module_info.parts.append(module_part)
                    modules.append(module_info)

                    # 打印每个模组的详细信息
                    self.logger.debug(f"模组: {module_name} (ID: {config_id})")
                    for part in module_info.parts:
                        self.logger.debug(f"  - {part.name}: {part.value}")
                else:
                    # 不是模组背包
                    break
        if modules:
            self.logger.debug(f"解析到 {len(modules)} 个模组信息")
            self.logger.debug("模组信息摘要:")
            for i, module in enumerate(modules, 1):
                parts_str = ", ".join([f"{p.name}+{p.value}" for p in module.parts])
                self.logger.debug(f"  {i}. {module.name} ({parts_str})")
            
            # 属性筛选
            if attributes or exclude_attributes:
                filtered_modules = self._filter_modules_by_attributes(
                    modules, attributes, exclude_attributes, match_count
                )
                self.logger.info(f"属性筛选后剩余 {len(filtered_modules)} 个模组")
            else:
                filtered_modules = modules
            
            # 筛选最优模组
            self._optimize_module_combinations(filtered_modules, category, attributes, exclude_attributes, enumeration_mode)
        
        return modules
    
    def _filter_modules_by_attributes(self, modules: List[ModuleInfo], attributes: List[str] = None, 
                                     exclude_attributes: List[str] = None, match_count: int = 1) -> List[ModuleInfo]:
        """根据属性词条筛选模组
        
        Args:
            modules: 模组列表
            attributes: 要筛选的属性词条列表
            exclude_attributes: 要排除的属性词条列表
            match_count: 模组需要包含的指定词条数量
            
        Returns:
            筛选后的模组列表
        """
        filtered_modules = []
        
        for module in modules:
            # 获取模组的所有属性名称
            module_attrs = [part.name for part in module.parts]
                        
            # 检查包含的属性数量
            if attributes:
                matching_attrs = [attr for attr in module_attrs if attr in attributes]
                if len(matching_attrs) < match_count:
                    self.logger.debug(f"模组 '{module.name}' 包含的指定属性数量不足: {len(matching_attrs)} < {match_count} (模组词条: {', '.join(module_attrs)})")
                    continue
                
                self.logger.debug(f"模组 '{module.name}' 通过筛选: 包含{len(matching_attrs)}个指定属性 ({', '.join(matching_attrs)}) (模组词条: {', '.join(module_attrs)})")
            else:
                self.logger.debug(f"模组 '{module.name}' 通过筛选: 无属性筛选条件")
            
            filtered_modules.append(module)
        
        return filtered_modules
    
    def _optimize_module_combinations(self, modules: List[ModuleInfo], category: str, attributes: List[str] = None, exclude_attributes: List[str] = None, enumeration_mode: bool = False):
        """筛选模组并展示
        
        Args:
            modules: 模组列表
            category: 模组类型
            attributes: 目标属性列表
            exclude_attributes: 排除属性列表
            enumeration_mode: 是否启用枚举模式
        """
        
        try:
            
            # 映射中文类型到枚举
            category_map = {
                "攻击": ModuleCategory.ATTACK,
                "守护": ModuleCategory.GUARDIAN,
                "辅助": ModuleCategory.SUPPORT,
                "全部": ModuleCategory.ALL
            }
            
            target_category = category_map.get(category, ModuleCategory.ALL)
            
            optimizer = ModuleOptimizer(target_attributes=attributes, exclude_attributes=exclude_attributes)
            
            optimizer.optimize_and_display(modules, target_category, top_n=40, enumeration_mode=enumeration_mode)
            
            # 模组筛选完成后自动退出程序
            self.logger.info("=== 模组筛选完成，准备退出程序 ===")
            import os
            os._exit(0)
            
        except ImportError as e:
            self.logger.warning(f"无法导入模组优化器: {e}")
        except Exception as e:
            self.logger.error(f"模组搭配优化失败: {e}")