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
#include <random>
#include <algorithm>

#include "simple_thread_pool.h"

/// @brief 游戏模组常量定义
namespace Constants {
    /// @brief 属性阈值
    inline const std::vector<int> ATTR_THRESHOLDS = {1, 4, 8, 12, 16, 20};
    
    /// @brief 基础属性战斗力映射
    /// @details 键为属性等级，值为对应的战斗力
    inline const std::map<int, int> BASIC_ATTR_POWER_VALUES = {
        {1, 7}, {2, 14}, {3, 29}, {4, 44}, {5, 167}, {6, 254}
    };
    
    /// @brief 特殊属性战斗力映射
    /// @details 键为属性等级，值为对应的战斗力
    inline const std::map<int, int> SPECIAL_ATTR_POWER_VALUES = {
        {1, 14}, {2, 29}, {3, 59}, {4, 89}, {5, 298}, {6, 448}
    };
    
    /// @brief 属性名称到类型的映射常量
    /// @details 将属性名称映射到 "basic" 或 "special" 类型
    inline const std::map<std::string, std::string> ATTR_NAME_TYPE_VALUES = {
        {"力量加持", "basic"}, {"敏捷加持", "basic"}, {"智力加持", "basic"},
        {"特攻伤害", "basic"}, {"精英打击", "basic"}, {"特攻治疗加持", "basic"},
        {"专精治疗加持", "basic"}, {"施法专注", "basic"}, {"攻速专注", "basic"},
        {"暴击专注", "basic"}, {"幸运专注", "basic"}, {"抵御魔法", "basic"},
        {"抵御物理", "basic"}, {"极-伤害叠加", "special"}, {"极-灵活身法", "special"},
        {"极-生命凝聚", "special"}, {"极-急救措施", "special"}, {"极-生命波动", "special"},
        {"极-生命汲取", "special"}, {"极-全队幸暴", "special"}, {"极-绝境守护", "special"}
    };
    
    /// @brief 总属性战斗力映射表
    /// @details 从0到120的属性总值对应的战斗力映射
    inline const std::map<int, int> TOTAL_ATTR_POWER_VALUES = {
        {0, 0}, {1, 5}, {2, 11}, {3, 17}, {4, 23}, {5, 29}, {6, 34}, {7, 40}, {8, 46},
        {18, 104}, {19, 110}, {20, 116}, {21, 122}, {22, 128}, {23, 133}, {24, 139}, {25, 145},
        {26, 151}, {27, 157}, {28, 163}, {29, 168}, {30, 174}, {31, 180}, {32, 186}, {33, 192},
        {34, 198}, {35, 203}, {36, 209}, {37, 215}, {38, 221}, {39, 227}, {40, 233}, {41, 238},
        {42, 244}, {43, 250}, {44, 256}, {45, 262}, {46, 267}, {47, 273}, {48, 279}, {49, 285},
        {50, 291}, {51, 297}, {52, 302}, {53, 308}, {54, 314}, {55, 320}, {56, 326}, {57, 332},
        {58, 337}, {59, 343}, {60, 349}, {61, 355}, {62, 361}, {63, 366}, {64, 372}, {65, 378},
        {66, 384}, {67, 390}, {68, 396}, {69, 401}, {70, 407}, {71, 413}, {72, 419}, {73, 425},
        {74, 431}, {75, 436}, {76, 442}, {77, 448}, {78, 454}, {79, 460}, {80, 466}, {81, 471},
        {82, 477}, {83, 483}, {84, 489}, {85, 495}, {86, 500}, {87, 506}, {88, 512}, {89, 518},
        {90, 524}, {91, 530}, {92, 535}, {93, 541}, {94, 547}, {95, 553}, {96, 559}, {97, 565},
        {98, 570}, {99, 576}, {100, 582}, {101, 588}, {102, 594}, {103, 599}, {104, 605}, {105, 611},
        {106, 617}, {113, 658}, {114, 664}, {115, 669}, {116, 675}, {117, 681}, {118, 687}, {119, 693}, {120, 699}
    };
}

/// @brief 计算组合数 C(n,r)
/// @param n 总元素数量
/// @param r 选择元素数量
/// @return 组合数
size_t CombinationCount(size_t n, size_t r);

/// @brief 根据索引直接计算第k个组合
/// @param n 总元素数量
/// @param r 选择元素数量
/// @param index 组合索引
/// @return 第index个组合的索引数组
std::vector<size_t> GetCombinationByIndex(size_t n, size_t r, size_t index);

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
    /// @param target_attributes 目标属性名称列表(可选，默认为空)
    /// @return 返回战斗力数值
    static int CalculateCombatPowerByIndices(
        const std::vector<size_t>& indices,
        const std::vector<ModuleInfo>& modules,
        const std::unordered_set<std::string>& target_attributes = {});

    /// @brief 处理组合范围
    /// @param start_combination 起始组合编号
    /// @param end_combination 结束组合编号
    /// @param n 总模组数量
    /// @param modules 模组信息列表
    /// @param target_attributes 目标属性名称集合(可选，默认为空)
    /// @return 返回简易解列表
    static std::vector<LightweightSolution> ProcessCombinationRange(
        size_t start_combination, 
        size_t end_combination, 
        size_t n,
        const std::vector<ModuleInfo>& modules,
        const std::unordered_set<std::string>& target_attributes = {}
    );

    /// @brief 策略枚举算法
    /// @param modules 模组信息列表
    /// @param target_attributes 目标属性名称集合(可选，默认为空)
    /// @param max_solutions 最大解决方案数量，默认为60
    /// @param max_workers 最大工作线程数，默认为8
    /// @return 返回模组解决方案列表
    static std::vector<ModuleSolution> StrategyEnumeration(
        const std::vector<ModuleInfo>& modules,
        const std::unordered_set<std::string>& target_attributes = {},
        int max_solutions = 60,
        int max_workers = 8);

    /// @brief 优化模组组合
    /// @param modules 模组信息列表
    /// @param target_attributes 目标属性名称集合（可选，默认为空）
    /// @param max_solutions 最大解决方案数量，默认为60
    /// @param max_attempts_multiplier 最大尝试次数倍数，默认为20
    /// @param local_search_iterations 局部搜索迭代次数，默认为30
    /// @return 返回最优的模组解决方案列表
    static std::vector<ModuleSolution> OptimizeModules(
        const std::vector<ModuleInfo>& modules,
        const std::unordered_set<std::string>& target_attributes = {},
        int max_solutions = 60,
        int max_attempts_multiplier = 20,
        int local_search_iterations = 30);

private:
    /// @brief 贪心构造解决方案
    /// @param modules 模组信息列表
    /// @param target_attributes 目标属性名称集合（可选，默认为空）
    /// @return 返回简易解
    static LightweightSolution GreedyConstructSolutionByIndices(
        const std::vector<ModuleInfo>& modules,
        const std::unordered_set<std::string>& target_attributes = {});
    
    /// @brief 局部搜索改进算法
    /// @param solution 初始解决方案
    /// @param all_modules 所有模组信息列表
    /// @param iterations 迭代次数，默认为10
    /// @param target_attributes 目标属性名称集合（可选，默认为空）
    /// @return 返回改进后的简易解
    static LightweightSolution LocalSearchImproveByIndices(
        const LightweightSolution& solution,
        const std::vector<ModuleInfo>& all_modules,
        int iterations = 10,
        const std::unordered_set<std::string>& target_attributes = {});
    
    /// @brief 检查组合是否唯一
    /// @param indices 当前组合索引
    /// @param seen_combinations 已有的组合
    /// @return 如果组合唯一返回true，否则返回false
    static bool IsCombinationUnique(
        const std::vector<size_t>& indices,
        const std::set<std::vector<size_t>>& seen_combinations);
};
