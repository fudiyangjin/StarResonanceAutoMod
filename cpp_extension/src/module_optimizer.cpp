#include "module_optimizer.h"

#ifdef USE_CUDA
// 外部CUDA函数声明
extern "C" int TestCuda();
extern "C" int GpuStrategyEnumeration(
    // 模组数据
    const int* module_attr_ids,
    const int* module_attr_values,
    const int* module_attr_counts,
    const int* module_offsets,
    int module_count,
    int total_attrs, 
    
    // 目标和排除属性
    const int* target_attrs,
    int target_count,
    const int* exclude_attrs,
    int exclude_count,
    
    // 最小属性总和约束
    const int* min_attr_ids,
    const int* min_attr_values,
    int min_attr_count,
    
    // 结果
    int max_solutions,
    int* result_scores,
    long long* result_indices);
#endif

inline bool NextCombination(std::array<uint16_t, 4>& comb, size_t n) {
    const size_t r = 4;
    for (int pos = static_cast<int>(r) - 1; pos >= 0; --pos) {
        uint16_t limit = static_cast<uint16_t>(n - r + pos);
        if (comb[pos] < limit) {
            ++comb[pos];
            for (size_t k = pos + 1; k < r; ++k) {
                comb[k] = static_cast<uint16_t>(comb[k - 1] + 1);
            }
            return true;
        }
    }
    return false;
}

size_t CombinationCount(size_t n, size_t r) {
    if (r > n) return 0;
    if (r == 0 || r == n) return 1;
    if (r > n - r) r = n - r;
    
    size_t result = 1;
    for (size_t i = 0; i < r; ++i) {
        result = result * (n - i) / (i + 1);
    }
    return result;
}

void GetCombinationByIndex(size_t n, size_t r, size_t index, std::vector<size_t>& combination) {
    size_t remaining = index;
    
    for (size_t i = 0; i < r; ++i) {
        size_t start = (i == 0) ? 0 : combination[i-1] + 1;
        for (size_t j = start; j < n; ++j) {
            size_t combinations_after = CombinationCount(n - j - 1, r - i - 1);
            if (remaining < combinations_after) {
                combination[i] = j;
                break;
            }
            remaining -= combinations_after;
        }
    }
}

std::vector<CompactSolution> ModuleOptimizerCpp::ProcessCombinationRange(
    size_t start_combination, size_t end_combination, size_t n,
    const std::vector<ModuleInfo>& modules,
    const std::unordered_set<int>& target_attributes,
    const std::unordered_set<int>& exclude_attributes,
    const std::unordered_map<int, int>& min_attr_sum_requirements,
    int local_top_capacity) {
    
    size_t range_size = end_combination - start_combination;

    std::vector<CompactSolution> solution;
    int ext_space = local_top_capacity;
    solution.reserve(std::min(range_size, static_cast<size_t>(local_top_capacity + ext_space)));
    int current_min = std::numeric_limits<int>::min();

    thread_local std::array<uint16_t, 4> combination_buffer;
    thread_local std::vector<size_t> temp_combination(4);

    GetCombinationByIndex(n, 4, start_combination, temp_combination);
    for (size_t j = 0; j < 4; ++j) {
        combination_buffer[j] = static_cast<uint16_t>(temp_combination[j]);
    }

    size_t produced = 0;
    while (produced < range_size) {
        // 先按 -mas 硬性约束筛掉不合格组合
        if (!min_attr_sum_requirements.empty()) {
            bool ok = true;
            for (const auto& kv : min_attr_sum_requirements) {
                int attr_id = kv.first;
                int need_sum = kv.second;
                int got_sum = 0;
                for (uint16_t idx : combination_buffer) {
                    const auto& parts = modules[idx].parts;
                    for (const auto& p : parts) {
                        if (p.id == attr_id) got_sum += p.value;
                    }
                }
                if (got_sum < need_sum) { ok = false; break; }
            }
            if (ok) {
                uint64_t packed = 0;
                for (size_t i = 0; i < 4; ++i) {
                    packed |= (static_cast<uint64_t>(combination_buffer[i]) << (i * 16));
                }
                int score = CalculateCombatPowerByPackedIndices(packed, modules, target_attributes, exclude_attributes);

                if (static_cast<int>(solution.size()) < local_top_capacity) {
                    // 小于local_top_capacity无脑入栈并记录最小值
                    CompactSolution cs; 
                    cs.packed_indices = packed; 
                    cs.score = score;
                    solution.emplace_back(cs);
                    if (static_cast<int>(solution.size()) == local_top_capacity) {
                        int mn = solution[0].score;
                        for (int i = 1; i < local_top_capacity; ++i) mn = std::min(mn, solution[i].score);
                        current_min = mn;
                    }
                } else if (score > current_min) {
                    // 在local_top_capacity-ext_space期间比在local_top_capacity内分高的才入栈
                    CompactSolution cs; 
                    cs.packed_indices = packed; 
                    cs.score = score; 
                    solution.emplace_back(cs);
                    if (static_cast<int>(solution.size()) == local_top_capacity + ext_space) {
                        // 保持local_top_capacity为TOP
                        std::nth_element(solution.begin(), solution.begin() + local_top_capacity, solution.end(),
                            [](const CompactSolution& a, const CompactSolution& b){ return a.score > b.score; });
                            solution.resize(local_top_capacity);
                        int mn = solution[0].score;
                        // 重新计算最小值
                        for (int i = 1; i < local_top_capacity; ++i) mn = std::min(mn, solution[i].score);
                        current_min = mn;
                    }
                }
            }
        } else {
            uint64_t packed = 0;
            for (size_t i = 0; i < 4; ++i) {
                packed |= (static_cast<uint64_t>(combination_buffer[i]) << (i * 16));
            }
            int score = CalculateCombatPowerByPackedIndices(packed, modules, target_attributes, exclude_attributes);

            if (static_cast<int>(solution.size()) < local_top_capacity) {
                CompactSolution cs; cs.packed_indices = packed; cs.score = score; solution.emplace_back(cs);
                if (static_cast<int>(solution.size()) == local_top_capacity) {
                    int mn = solution[0].score;
                    for (int i = 1; i < local_top_capacity; ++i) mn = std::min(mn, solution[i].score);
                    current_min = mn;
                }
            } else if (score > current_min) {
                CompactSolution cs; cs.packed_indices = packed; cs.score = score; solution.emplace_back(cs);
                if (static_cast<int>(solution.size()) == local_top_capacity + ext_space) {
                    std::nth_element(solution.begin(), solution.begin() + local_top_capacity, solution.end(),
                        [](const CompactSolution& a, const CompactSolution& b){ return a.score > b.score; });
                    solution.resize(local_top_capacity);
                    int mn = solution[0].score;
                    for (int i = 1; i < local_top_capacity; ++i) mn = std::min(mn, solution[i].score);
                    current_min = mn;
                }
            }
        }

        ++produced;
        if (produced >= range_size) break;
        if (!NextCombination(combination_buffer, n)) break;
    }

    // 保持local_top_capacity为TOP
    if (static_cast<int>(solution.size()) > local_top_capacity) {
        std::nth_element(solution.begin(), solution.begin() + local_top_capacity, solution.end(),
            [](const CompactSolution& a, const CompactSolution& b){ return a.score > b.score; });
        solution.resize(local_top_capacity);
    }

    return solution;
}

std::pair<int, std::map<std::string, int>> ModuleOptimizerCpp::CalculateCombatPower(
    const std::vector<ModuleInfo>& modules) {
        std::unordered_map<std::string, int> attr_breakdown;
        attr_breakdown.reserve(20);
        
        for (const auto& module : modules) {
            for (const auto& part : module.parts) {
                attr_breakdown[part.name] += part.value;
            }
        }
        
        int threshold_power = 0;
        int total_attr_value = 0;
        
        for (const auto& [attr_name, attr_value] : attr_breakdown) {
            total_attr_value += attr_value;
            
            int max_level = 0;
            for (int level = 0; level < 6; ++level) {
                if (attr_value >= Constants::ATTR_THRESHOLDS[level]) {
                    max_level = level + 1;
                } else {
                    break;
                }
            }
            
            if (max_level > 0) {
                bool is_special = Constants::SPECIAL_ATTR_NAMES_STR.find(attr_name) != Constants::SPECIAL_ATTR_NAMES_STR.end();
                
                int base_power;
                if (is_special) {
                    base_power = Constants::SPECIAL_ATTR_POWER_VALUES[max_level - 1];
                } else {
                    base_power = Constants::BASIC_ATTR_POWER_VALUES[max_level - 1];
                }
                threshold_power += base_power;
            }
        }
        
        int total_attr_power = total_attr_power = Constants::TOTAL_ATTR_POWER_VALUES[total_attr_value];
        
        int total_power = threshold_power + total_attr_power;
        
        std::map<std::string, int> result_map;
        for (const auto& [key, value] : attr_breakdown) {
            result_map.emplace(key, value);
        }
        
        return {total_power, result_map};
}

int ModuleOptimizerCpp::CalculateCombatPowerByIndices(
    const std::vector<size_t>& indices,
    const std::vector<ModuleInfo>& modules,
    const std::unordered_set<int>& target_attributes,
    const std::unordered_set<int>& exclude_attributes) {

    std::array<int, 20> attr_values = {};
    std::array<int, 20> attr_ids;
    size_t attr_count = 0;
    
    int total_attr_value = 0;
    
    for (size_t index : indices) {
        const auto& module = modules[index];
        for (const auto& part : module.parts) {
            size_t i;
            for (i = 0; i < attr_count; ++i) {
                if (attr_ids[i] == part.id) {
                    attr_values[i] += part.value;
                    break;
                }
            }
            if (i == attr_count) {
                attr_ids[attr_count] = part.id;
                attr_values[attr_count] = part.value;
                ++attr_count;
            }
            total_attr_value += part.value;
        }
    }
    
    int threshold_power = 0;
    
    for (size_t i = 0; i < attr_count; ++i) {
        int attr_value = attr_values[i];
        int attr_id = attr_ids[i];
        
        int max_level = 0;
        for (int level = 0; level < 6; ++level) {
            if (attr_value >= Constants::ATTR_THRESHOLDS[level]) {
                max_level = level + 1;
            } else {
                break;
            }
        }
        
        if (max_level > 0) {
            bool is_special = Constants::SPECIAL_ATTR_NAMES.find(attr_id) != Constants::SPECIAL_ATTR_NAMES.end();
            
            int base_power;
            if (is_special) {
                base_power = Constants::SPECIAL_ATTR_POWER_VALUES[max_level - 1];
            } else {
                base_power = Constants::BASIC_ATTR_POWER_VALUES[max_level - 1];
            }
            
            // 是否为-attr携带的属性, 如果是就双倍
            if (!target_attributes.empty() && target_attributes.find(attr_id) != target_attributes.end()) {
                threshold_power += base_power * 2;
            } else if (!exclude_attributes.empty() && exclude_attributes.find(attr_id) != exclude_attributes.end()) {
                // 是否为-exattr携带的属性, 如果是就为0
                threshold_power += 0;
            } else {
                threshold_power += base_power;
            }
        }
    }
    
    int total_attr_power = Constants::TOTAL_ATTR_POWER_VALUES[total_attr_value];
    
    return threshold_power + total_attr_power;
}

int ModuleOptimizerCpp::CalculateCombatPowerByPackedIndices(
    uint64_t packed_indices,
    const std::vector<ModuleInfo>& modules,
    const std::unordered_set<int>& target_attributes,
    const std::unordered_set<int>& exclude_attributes) {
    
    // 解包索引
    std::array<uint16_t, 4> indices;
    for (size_t i = 0; i < 4; ++i) {
        indices[i] = static_cast<uint16_t>((packed_indices >> (i * 16)) & 0xFFFF);
    }
    
    std::array<int, 20> attr_values = {};
    std::array<int, 20> attr_ids;
    size_t attr_count = 0;
    
    int total_attr_value = 0;
    
    for (size_t idx_pos = 0; idx_pos < 4; ++idx_pos) {
        size_t index = static_cast<size_t>(indices[idx_pos]);
        
        const auto& module = modules[index];
        for (const auto& part : module.parts) {
            size_t i;
            for (i = 0; i < attr_count; ++i) {
                if (attr_ids[i] == part.id) {
                    attr_values[i] += part.value;
                    break;
                }
            }
            if (i == attr_count && attr_count < 20) {
                attr_ids[attr_count] = part.id;
                attr_values[attr_count] = part.value;
                ++attr_count;
            }
            total_attr_value += part.value;
        }
    }
    
    int threshold_power = 0;
    
    for (size_t i = 0; i < attr_count; ++i) {
        int attr_value = attr_values[i];
        int attr_id = attr_ids[i];
        
        int max_level = 0;
        for (int level = 0; level < 6; ++level) {
            if (attr_value >= Constants::ATTR_THRESHOLDS[level]) {
                max_level = level + 1;
            } else {
                break;
            }
        }
        
        if (max_level > 0) {
            bool is_special = Constants::SPECIAL_ATTR_NAMES.find(attr_id) != Constants::SPECIAL_ATTR_NAMES.end();
            
            int base_power;
            if (is_special) {
                base_power = Constants::SPECIAL_ATTR_POWER_VALUES[max_level - 1];
            } else {
                base_power = Constants::BASIC_ATTR_POWER_VALUES[max_level - 1];
            }
            
            if (!target_attributes.empty() && target_attributes.find(attr_id) != target_attributes.end()) {
                // 是否为-attr携带的属性, 如果是就双倍
                threshold_power += base_power * 2;
            } else if (!exclude_attributes.empty() && exclude_attributes.find(attr_id) != exclude_attributes.end()) {
                // 是否为-exattr携带的属性, 如果是就为0
                threshold_power += 0;
            } else {
                threshold_power += base_power;
            }
        }
    }
    
    int total_attr_power = Constants::TOTAL_ATTR_POWER_VALUES[total_attr_value];
    
    return threshold_power + total_attr_power;
}

std::vector<ModuleSolution> ModuleOptimizerCpp::StrategyEnumeration(
    const std::vector<ModuleInfo>& modules,
    const std::unordered_set<int>& target_attributes,
    const std::unordered_set<int>& exclude_attributes,
    const std::unordered_map<int, int>& min_attr_sum_requirements,
    int max_solutions,
    int max_workers) {

    std::vector<ModuleInfo> candidate_modules = modules;
    // 计算组合
    size_t n = candidate_modules.size();
    size_t total_combinations = (n * (n - 1) * (n - 2) * (n - 3)) / 24;

    size_t batch_size = std::max(static_cast<size_t>(1000), total_combinations / (max_workers * 4));
    batch_size = std::min(batch_size, static_cast<size_t>(1307072));
    size_t num_batches = (total_combinations + batch_size - 1) / batch_size;
    // 创建线程池
    auto pool = std::make_unique<SimpleThreadPool>(max_workers);
    std::vector<std::future<std::vector<CompactSolution>>> futures;
    futures.reserve(num_batches); 

    // 提交任务
    for (size_t batch_idx = 0; batch_idx < num_batches; ++batch_idx) {
        size_t start_combination = batch_idx * batch_size;
        size_t end_combination = std::min(start_combination + batch_size, total_combinations);
        size_t range_size = end_combination - start_combination;
        int oversample_factor = 2;
        int local_top_capacity = static_cast<int>(std::min(range_size, static_cast<size_t>(max_solutions * oversample_factor)));
        auto min_req_copy = min_attr_sum_requirements;
        futures.push_back(pool->enqueue(
            [start_combination, end_combination, n, local_top_capacity,
             &candidate_modules, target_attributes, exclude_attributes, min_req_copy]() {
                return ProcessCombinationRange(
                    start_combination, end_combination, n,
                    candidate_modules, target_attributes, exclude_attributes, min_req_copy,
                    local_top_capacity);
            }
        ));
    }
    
    // 优先队列收集解保持真正占内存的只有最后的解+运行中线程创建的LightweightSolution
    std::priority_queue<CompactSolution, std::vector<CompactSolution>, 
                       std::greater<CompactSolution>> top_solutions;
    while (!futures.empty()) {
        auto completed_future = std::find_if(futures.begin(), futures.end(),
            [](auto& f) { return f.wait_for(std::chrono::seconds(0)) == std::future_status::ready; });
        
        if (completed_future != futures.end()) {
            auto batch_result = std::move(completed_future->get());
            for (const auto& solution : batch_result) {
                if (top_solutions.size() < static_cast<size_t>(max_solutions)) {
                    top_solutions.push(solution);
                } else if (solution.score > top_solutions.top().score) {
                    top_solutions.pop();
                    top_solutions.push(solution);
                }
            }
            
            futures.erase(completed_future);
        } else {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }
    pool.reset();
    
    
    // 优先队列->vector
    std::vector<CompactSolution> all_solutions;
    all_solutions.reserve(top_solutions.size());
    
    while (!top_solutions.empty()) {
        all_solutions.push_back(top_solutions.top());
        top_solutions.pop();
    }
    
    // 降序提取前max_solutions结果, 构造完整解
    std::reverse(all_solutions.begin(), all_solutions.end());
    std::vector<ModuleSolution> final_solutions;
    final_solutions.reserve(all_solutions.size());
    for (const auto& solution : all_solutions) {
        auto indices = solution.unpack_indices_vector();
        std::vector<ModuleInfo> solution_modules;
        solution_modules.reserve(indices.size());
        for (size_t index : indices) {
            solution_modules.push_back(candidate_modules[index]);
        }
        auto result = CalculateCombatPower(solution_modules);
        final_solutions.emplace_back(solution_modules, solution.score, result.second);
    }

    return final_solutions;
}

std::vector<ModuleSolution> ModuleOptimizerCpp::StrategyEnumerationCUDA(
    const std::vector<ModuleInfo>& modules,
    const std::unordered_set<int>& target_attributes,
    const std::unordered_set<int>& exclude_attributes,
    const std::unordered_map<int, int>& min_attr_sum_requirements,
    int max_solutions,
    int max_workers) {
    
#ifdef USE_CUDA
    // 测试CUDA是否可用
    if (TestCuda()) {
        printf("CUDA GPU acceleration enabled - all calculations performed on GPU\n");
        
        // 准备模组数据传输到GPU
        std::vector<int> all_attr_ids;
        std::vector<int> all_attr_values;
        std::vector<int> module_attr_counts;
        std::vector<int> module_offsets;
        
        size_t current_offset = 0;
        for (const auto& module : modules) {
            module_offsets.push_back(static_cast<int>(current_offset));
            module_attr_counts.push_back(static_cast<int>(module.parts.size()));
            
            for (const auto& part : module.parts) {
                all_attr_ids.push_back(part.id);
                all_attr_values.push_back(part.value);
            }
            
            current_offset += module.parts.size();
        }
        
        std::vector<int> target_attrs(target_attributes.begin(), target_attributes.end());
        std::vector<int> exclude_attrs(exclude_attributes.begin(), exclude_attributes.end());
     
        std::vector<int> min_attr_ids;
        std::vector<int> min_attr_values;
        for (const auto& kv : min_attr_sum_requirements) {
            min_attr_ids.push_back(kv.first);
            min_attr_values.push_back(kv.second);
        }
        
        std::vector<int> gpu_scores(max_solutions);
        std::vector<long long> gpu_indices(max_solutions);
    
        // CUDA运算
        int gpu_result_count = GpuStrategyEnumeration(
            // 模组数据
            all_attr_ids.data(),
            all_attr_values.data(),
            module_attr_counts.data(),
            module_offsets.data(),
            static_cast<int>(modules.size()),
            static_cast<int>(all_attr_ids.size()),
            
            // 目标和排除属性
            target_attrs.empty() ? nullptr : target_attrs.data(),
            static_cast<int>(target_attrs.size()),
            exclude_attrs.empty() ? nullptr : exclude_attrs.data(),
            static_cast<int>(exclude_attrs.size()),
            
            // 最小属性总和约束
            min_attr_ids.empty() ? nullptr : min_attr_ids.data(),
            min_attr_values.empty() ? nullptr : min_attr_values.data(),
            static_cast<int>(min_attr_ids.size()),
            
            // 结果
            max_solutions,
            gpu_scores.data(),
            gpu_indices.data());
        
        // 转换GPU结果
        std::vector<ModuleSolution> final_solutions;
        final_solutions.reserve(gpu_result_count);
        
        for (int i = 0; i < gpu_result_count; ++i) {   
            // 解包索引
            long long packed = gpu_indices[i];
            std::vector<ModuleInfo> solution_modules;
            solution_modules.reserve(4);
            
            for (int j = 0; j < 4; ++j) {
                size_t module_idx = static_cast<size_t>((packed >> (j * 16)) & 0xFFFF);
                if (module_idx < modules.size()) {
                    solution_modules.push_back(modules[module_idx]);
                }
            }
            
            std::vector<size_t> solution_indices;
            for (int j = 0; j < 4; ++j) {
                size_t module_idx = static_cast<size_t>((packed >> (j * 16)) & 0xFFFF);
                solution_indices.push_back(module_idx);
            }
            
            auto result = CalculateCombatPower(solution_modules);
            final_solutions.emplace_back(solution_modules, gpu_scores[i], result.second);
        }
        
        return final_solutions;
        
    } else {
        printf("CUDA not available, using CPU optimized version\n");
        return StrategyEnumeration(modules, target_attributes, exclude_attributes, min_attr_sum_requirements, max_solutions, max_workers);
    }
#else
    return StrategyEnumeration(modules, target_attributes, exclude_attributes, min_attr_sum_requirements, max_solutions, max_workers);
#endif
}

std::vector<ModuleSolution> ModuleOptimizerCpp::OptimizeModules(
    const std::vector<ModuleInfo>& modules,
    const std::unordered_set<int>& target_attributes,
    const std::unordered_set<int>& exclude_attributes,
    int max_solutions,
    int max_attempts_multiplier,
    int local_search_iterations) {

    auto candidate_modules = modules;

    std::vector<LightweightSolution> lightweight_solutions;
    std::set<std::vector<size_t>> seen_combinations;
    
    int max_attempts = max_solutions * max_attempts_multiplier;
    int attempts = 0;
    
    while (lightweight_solutions.size() < static_cast<size_t>(max_solutions) && attempts < max_attempts) {
        attempts++;
        
        // 构造贪心初始解
        auto solution = GreedyConstructSolutionByIndices(candidate_modules, target_attributes, exclude_attributes);
        if (solution.module_indices.empty()) continue;
        
        // 局部搜索改进解
        auto improved_solution = LocalSearchImproveByIndices(solution, candidate_modules, local_search_iterations, target_attributes, exclude_attributes);
        
        // 去重
        if (IsCombinationUnique(improved_solution.module_indices, seen_combinations)) {
            std::vector<size_t> sorted_indices = improved_solution.module_indices;
            std::sort(sorted_indices.begin(), sorted_indices.end());
            seen_combinations.insert(sorted_indices);
            lightweight_solutions.push_back(improved_solution);
        }
    }
    
    // 按评分排序
    std::sort(lightweight_solutions.begin(), lightweight_solutions.end(),
              [](const auto& a, const auto& b) { return a.score > b.score; });
    
    // 构造完整的ModuleSolution对象
    std::vector<ModuleSolution> solutions;
    solutions.reserve(lightweight_solutions.size());
    
    for (const auto& lightweight_solution : lightweight_solutions) {
        std::vector<ModuleInfo> solution_modules;
        solution_modules.reserve(lightweight_solution.module_indices.size());
        for (size_t index : lightweight_solution.module_indices) {
            solution_modules.push_back(candidate_modules[index]);
        }
        
        auto result = CalculateCombatPower(solution_modules);
        
        solutions.emplace_back(solution_modules, lightweight_solution.score, result.second);
    }
    
    return solutions;
}

LightweightSolution ModuleOptimizerCpp::GreedyConstructSolutionByIndices(
    const std::vector<ModuleInfo>& modules,
    const std::unordered_set<int>& target_attributes,
    const std::unordered_set<int>& exclude_attributes) {
    
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_real_distribution<> dis(0.0, 1.0);
    
    std::uniform_int_distribution<> module_dis(0, modules.size() - 1);
    std::vector<size_t> current_indices;
    current_indices.push_back(module_dis(gen));
    
    for (int step = 0; step < 3; ++step) {
        std::vector<size_t> candidates;
        std::vector<int> candidate_scores;
        
        for (size_t module_idx = 0; module_idx < modules.size(); ++module_idx) {

            // 判重
            bool already_included = false;
            for (size_t current_idx : current_indices) {
                if (current_idx == module_idx) {
                    already_included = true;
                    break;
                }
            }
            if (already_included) continue;
            
            std::vector<size_t> test_indices = current_indices;
            test_indices.push_back(module_idx);
            
            int score = CalculateCombatPowerByIndices(test_indices, modules, target_attributes, exclude_attributes);
            
            candidates.push_back(module_idx);
            candidate_scores.push_back(score);
        }
        
        if (candidates.empty()) break;
        
        // 80%最优, 20%前3
        if (dis(gen) < 0.8) {
            auto max_it = std::max_element(candidate_scores.begin(), candidate_scores.end());
            size_t best_idx = std::distance(candidate_scores.begin(), max_it);
            current_indices.push_back(candidates[best_idx]);
        } else {
            std::vector<std::pair<int, size_t>> scored_candidates;
            for (size_t i = 0; i < candidates.size(); ++i) {
                scored_candidates.emplace_back(candidate_scores[i], candidates[i]);
            }
            
            std::sort(scored_candidates.begin(), scored_candidates.end(),
                     [](const auto& a, const auto& b) { return a.first > b.first; });
            
            int top_count = std::min(3, static_cast<int>(scored_candidates.size()));
            std::uniform_int_distribution<> top_dis(0, top_count - 1);
            int selected_idx = top_dis(gen);
            current_indices.push_back(scored_candidates[selected_idx].second);
        }
    }
    
    int final_score = CalculateCombatPowerByIndices(current_indices, modules, target_attributes, exclude_attributes);
    
    return LightweightSolution(current_indices, final_score);
}

LightweightSolution ModuleOptimizerCpp::LocalSearchImproveByIndices(
    const LightweightSolution& solution,
    const std::vector<ModuleInfo>& all_modules,
    int iterations,
    const std::unordered_set<int>& target_attributes,
    const std::unordered_set<int>& exclude_attributes) {
    
    LightweightSolution best_solution = solution;
    
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> module_dis(0, all_modules.size() - 1);
    
    for (int iteration = 0; iteration < iterations; ++iteration) {
        bool improved = false;
        
        for (size_t i = 0; i < best_solution.module_indices.size(); ++i) {
            int candidate_count = std::min(20, static_cast<int>(all_modules.size()));
            std::vector<size_t> candidates;
            
            // 尝试替换每个采样模组
            for (int j = 0; j < candidate_count; ++j) {
                candidates.push_back(module_dis(gen));
            }
            
            for (size_t new_module_idx : candidates) {
                // 判重
                bool already_included = false;
                for (size_t existing_idx : best_solution.module_indices) {
                    if (existing_idx == new_module_idx) {
                        already_included = true;
                        break;
                    }
                }
                if (already_included) continue;
                
                std::vector<size_t> new_indices = best_solution.module_indices;
                new_indices[i] = new_module_idx;
                
                int new_score = CalculateCombatPowerByIndices(new_indices, all_modules, target_attributes, exclude_attributes);
                
                if (new_score > best_solution.score) {
                    best_solution = LightweightSolution(new_indices, new_score);
                    improved = true;
                    break;
                }
            }
            
            if (improved) break;
        }
        
        // 连续没有改善就提前结束
        if (!improved && iteration > iterations / 2) {
            break;
        }
    }
    
    return best_solution;
}

bool ModuleOptimizerCpp::IsCombinationUnique(
    const std::vector<size_t>& indices,
    const std::set<std::vector<size_t>>& seen_combinations) {
    
    std::vector<size_t> sorted_indices = indices;
    std::sort(sorted_indices.begin(), sorted_indices.end());
    
    return seen_combinations.find(sorted_indices) == seen_combinations.end();
}