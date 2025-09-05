#pragma once

#include <string>
#include <vector>
#include <map>
#include <set>
#include <memory>
#include <thread>
#include <future>
#include <atomic>
#include <mutex>
#include <unordered_set>
#include <unordered_map>
#include <random>
#include <algorithm>
#include <array>
#include <iostream>

#include "simple_thread_pool.h"

/// @brief 游戏模组常量定义
namespace Constants {
    /// @brief 属性阈值
    inline const std::array<int, 6> ATTR_THRESHOLDS = {1, 4, 8, 12, 16, 20};
    
    /// @brief 基础属性战斗力映射
    /// @details 基础属性对应的战斗力
    inline const std::array<int, 6> BASIC_ATTR_POWER_VALUES = {7, 14, 29, 44, 167, 254};

    /// @brief 特殊属性战斗力映射
    /// @details 特殊属性对应的战斗力
    inline const std::array<int, 6> SPECIAL_ATTR_POWER_VALUES = {14, 29, 59, 89, 298, 448};
    
    /// @brief 特殊属性映射
    /// @details 特殊属性映射, int
    inline const std::unordered_map<int, bool> SPECIAL_ATTR_NAMES = {
        {2104, true}, {2105, true}, {2204, true},
        {2205, true}, {2404, true}, {2405, true},
        {2406, true}, {2304, true}
    };

    /// @brief 特殊属性映射
    /// @details 特殊属性映射, str
    inline const std::unordered_map<std::string, bool> SPECIAL_ATTR_NAMES_STR = {
        {"极-伤害叠加", true}, {"极-灵活身法", true}, {"极-生命凝聚", true},
        {"极-急救措施", true}, {"极-生命波动", true}, {"极-生命汲取", true},
        {"极-全队幸暴", true}, {"极-绝境守护", true}
    };
    
    /// @brief 总属性战斗力映射表
    /// @details 从0到120的属性总值对应的战斗力映射
    inline const std::array<int, 121> TOTAL_ATTR_POWER_VALUES = {
        0, 5, 11, 17, 23, 29, 34, 40, 46, 52, 58, 64, 69, 75, 81, 87, 93, 99, 104, 110, 116,
        122, 128, 133, 139, 145, 151, 157, 163, 168, 174, 180, 186, 192, 198, 203, 209, 215, 221, 227, 233,
        238, 244, 250, 256, 262, 267, 273, 279, 285, 291, 297, 302, 308, 314, 320, 326, 332, 337, 343, 349,
        355, 361, 366, 372, 378, 384, 390, 396, 401, 407, 413, 419, 425, 431, 436, 442, 448, 454, 460, 466,
        471, 477, 483, 489, 495, 500, 506, 512, 518, 524, 530, 535, 541, 547, 553, 559, 565, 570, 576, 582,
        588, 594, 599, 605, 611, 617, 623, 629, 634, 640, 646, 652, 658, 664, 669, 675, 681, 687, 693, 699
    };
}

/// @brief 计算组合数 C(n,r)
/// @param n 总元素数量
/// @param r 选择元素数量
/// @return 组合数
size_t CombinationCount(size_t n, size_t r);

/// @brief 根据索引直接计算第k个组合（填充到输出参数）
/// @param n 总元素数量
/// @param r 选择元素数量
/// @param index 组合索引
/// @param combination 输出参数，用于存储组合结果
void GetCombinationByIndex(size_t n, size_t r, size_t index, std::vector<size_t>& combination);

/// @brief 模组属性数据结构
/// @details 表示单个模组属性的信息
struct ModulePart {
    /// @brief 模组属性ID
    int id;
    
    /// @brief 模组属性名称
    std::string name;
    
    /// @brief 属性数值
    int value;
    
    /// @brief 构造函数
    /// @param id 模组属性ID
    /// @param name 模组属性名称
    /// @param value 属性数值
    ModulePart(int id, const std::string& name, int value) 
        : id(id), name(name), value(value) {}
};

/// @brief 模组信息数据结构
/// @details 包含模组的完整信息，包括名称、名称ID、UUID、品质和属性列表
struct ModuleInfo {
    /// @brief 模组名称
    std::string name;
    
    /// @brief 模组名称ID
    int config_id;
    
    /// @brief 模组唯一标识符
    int uuid;
    
    /// @brief 模组品质等级
    int quality;
    
    /// @brief 模组属性列表
    std::vector<ModulePart> parts;
    
    /// @brief 构造函数
    /// @param name 模组名称
    /// @param config_id 模组配置ID
    /// @param uuid 模组唯一标识符
    /// @param quality 模组品质等级
    /// @param parts 模组部件列表
    ModuleInfo(const std::string& name, int config_id, int uuid, 
               int quality, const std::vector<ModulePart>& parts)
        : name(name), config_id(config_id), uuid(uuid), quality(quality), parts(parts) {}
};

/// @brief 模组简易解
/// @details 用于中间计算，只存储索引和分数
struct LightweightSolution {
    /// @brief 模组索引数组
    std::vector<size_t> module_indices;
    
    /// @brief 分数
    int score;
    
    /// @brief 默认构造函数
    LightweightSolution() : score(0) {}
    
    /// @brief 构造函数
    /// @param indices 模组索引数组
    /// @param score 分数
    LightweightSolution(const std::vector<size_t>& indices, int score)
        : module_indices(indices), score(score) {}
    
    /// @brief 大于比较运算符，用于排序
    /// @param other 另一个解决方案
    /// @return 如果当前解决方案分数更高返回true
    bool operator>(const LightweightSolution& other) const {
        return score > other.score;
    }
};

/// @brief 更加紧凑的模组解
/// @details 用于中间计算, 将4个模组的索引打包进64位整数中
struct CompactSolution {
    /// @brief 打包的模组索引
    uint64_t packed_indices;
    
    /// @brief 分数
    int score;
    
    /// @brief 默认构造函数
    CompactSolution() : packed_indices(0), score(0) {}
    
    /// @brief 从数组构造
    /// @param indices 模组索引数组
    /// @param score 分数
    CompactSolution(const std::array<uint16_t, 4>& indices, int score) : score(score) {
        pack_indices_array(indices);
    }
    
    /// @brief 从数组打包索引
    /// @param indices 模组索引数组
    void pack_indices_array(const std::array<uint16_t, 4>& indices) {
        packed_indices = 0;
        for (size_t i = 0; i < 4; ++i) {
            packed_indices |= (static_cast<uint64_t>(indices[i]) << (i * 16));
        }
    }
    
    /// @brief 解包索引
    /// @return 模组索引
    std::vector<size_t> unpack_indices_vector() const {
        std::vector<size_t> indices;
        indices.reserve(4);
        for (size_t i = 0; i < 4; ++i) {
            uint16_t idx = static_cast<uint16_t>((packed_indices >> (i * 16)) & 0xFFFF);
            indices.push_back(static_cast<size_t>(idx));
        }
        return indices;
    }
    
    /// @brief 大于比较运算符, 用于排序
    /// @param other 另一个解
    /// @return 如果当前解决方案分数更高返回true
    bool operator>(const CompactSolution& other) const {
        return score > other.score;
    }
    
    /// @brief 小于比较运算符, 用于最小堆
    /// @param other 另一个解
    /// @return 如果当前解决方案分数更低返回true
    bool operator<(const CompactSolution& other) const {
        return score < other.score;
    }
};

/// @brief 模组完整解
/// @details 包含完整的模组信息和属性信息
struct ModuleSolution {
    /// @brief 模组信息列表
    std::vector<ModuleInfo> modules;
    
    /// @brief 解决方案分数
    int score;
    
    /// @brief 组合属性值
    std::map<std::string, int> attr_breakdown;
    
    /// @brief 默认构造函数
    ModuleSolution() : score(0) {}
    
    /// @brief 构造函数
    /// @param modules 模组信息列表
    /// @param score 解决方案分数
    /// @param attr_breakdown 属性属性映射表
    ModuleSolution(const std::vector<ModuleInfo>& modules, int score, 
                   const std::map<std::string, int>& attr_breakdown)
        : modules(modules), score(score), attr_breakdown(attr_breakdown) {}
};

/// @brief 模组优化器主类
/// @details 提供模组组合优化功能，包括战斗力计算、策略枚举和贪心优化算法
class ModuleOptimizerCpp {
public:
    /// @brief 计算模组组合的战斗力
    /// @param modules 模组信息列表
    /// @return 返回战斗力和组合属性值
    static std::pair<int, std::map<std::string, int>> CalculateCombatPower(
        const std::vector<ModuleInfo>& modules);

    /// @brief 根据索引计算战斗力
    /// @param indices 模组索引数组
    /// @param modules 模组信息列表
    /// @param target_attributes 目标属性名称列表
    /// @param exclude_attributes 排除属性名称列表
    /// @return 返回战斗力数值
    static int CalculateCombatPowerByIndices(
        const std::vector<size_t>& indices,
        const std::vector<ModuleInfo>& modules,
        const std::unordered_set<int>& target_attributes = {},
        const std::unordered_set<int>& exclude_attributes = {});

    /// @brief 根据紧凑索引计算战斗力
    /// @param packed_indices 打包的模组索引
    /// @param modules 模组信息列表
    /// @param target_attributes 目标属性名称列表
    /// @param exclude_attributes 排除属性名称列表
    /// @return 返回战斗力数值
    static int CalculateCombatPowerByPackedIndices(
        uint64_t packed_indices,
        const std::vector<ModuleInfo>& modules,
        const std::unordered_set<int>& target_attributes = {},
        const std::unordered_set<int>& exclude_attributes = {}
    );

    /// @brief 处理组合范围
    /// @param start_combination 起始组合编号
    /// @param end_combination 结束组合编号
    /// @param n 总模组数量
    /// @param modules 模组信息列表
    /// @param target_attributes 目标属性名称集合
    /// @param exclude_attributes 排除属性名称集合
    /// @return 返回简易解列表
    static std::vector<CompactSolution> ProcessCombinationRange(
        size_t start_combination, 
        size_t end_combination, 
        size_t n,
        const std::vector<ModuleInfo>& modules,
        const std::unordered_set<int>& target_attributes = {},
        const std::unordered_set<int>& exclude_attributes = {},
        const std::unordered_map<int, int>& min_attr_sum_requirements = {}
    );

    /// @brief 策略枚举算法
    /// @param modules 模组信息列表
    /// @param target_attributes 目标属性名称集合
    /// @param exclude_attributes 排除属性名称集合
    /// @param max_solutions 最大解决方案数量，默认为60
    /// @param max_workers 最大工作线程数，默认为8
    /// @return 返回模组解决方案列表
    static std::vector<ModuleSolution> StrategyEnumeration(
        const std::vector<ModuleInfo>& modules,
        const std::unordered_set<int>& target_attributes = {},
        const std::unordered_set<int>& exclude_attributes = {},
        const std::unordered_map<int, int>& min_attr_sum_requirements = {},
        int max_solutions = 60,
        int max_workers = 8);

    /// @brief 优化模组组合
    /// @param modules 模组信息列表
    /// @param target_attributes 目标属性名称集合
    /// @param exclude_attributes 排除属性名称集合
    /// @param max_solutions 最大解决方案数量，默认为60
    /// @param max_attempts_multiplier 最大尝试次数倍数，默认为20
    /// @param local_search_iterations 局部搜索迭代次数，默认为30
    /// @return 返回最优的模组解决方案列表
    static std::vector<ModuleSolution> OptimizeModules(
        const std::vector<ModuleInfo>& modules,
        const std::unordered_set<int>& target_attributes = {},
        const std::unordered_set<int>& exclude_attributes = {},
        int max_solutions = 60,
        int max_attempts_multiplier = 20,
        int local_search_iterations = 30);

private:
    /// @brief 贪心构造解决方案
    /// @param modules 模组信息列表
    /// @param target_attributes 目标属性名称集合
    /// @param exclude_attributes 排除属性名称集合
    /// @return 返回简易解
    static LightweightSolution GreedyConstructSolutionByIndices(
        const std::vector<ModuleInfo>& modules,
        const std::unordered_set<int>& target_attributes = {},
        const std::unordered_set<int>& exclude_attributes = {});
    
    /// @brief 局部搜索改进算法
    /// @param solution 初始解决方案
    /// @param all_modules 所有模组信息列表
    /// @param iterations 迭代次数，默认为10
    /// @param target_attributes 目标属性名称集合
    /// @param exclude_attributes 排除属性名称集合
    /// @return 返回改进后的简易解
    static LightweightSolution LocalSearchImproveByIndices(
        const LightweightSolution& solution,
        const std::vector<ModuleInfo>& all_modules,
        int iterations = 10,
        const std::unordered_set<int>& target_attributes = {},
        const std::unordered_set<int>& exclude_attributes = {});
    
    /// @brief 检查组合是否唯一
    /// @param indices 当前组合索引
    /// @param seen_combinations 已有的组合
    /// @return 如果组合唯一返回true，否则返回false
    static bool IsCombinationUnique(
        const std::vector<size_t>& indices,
        const std::set<std::vector<size_t>>& seen_combinations);
};
