#include <immintrin.h>

#include "module_optimizer.h"

#ifdef USE_CUDA
// 外部CUDA函数声明
extern "C" int TestCuda();
extern "C" int GpuStrategyEnumeration(
    const int* module_matrix,
    int module_count,
    const int* slot_value_power,
    const int* min_attr_requirements,
    int max_solutions,
    int* result_scores,
    long long* result_indices,
    int combination_size);
#endif

namespace {
using BeamPickArray = std::array<int, 6>;
using DenseSlotArray = std::array<int, Constants::CUDA_ATTR_DIM>;
using CompactMinHeap =
    std::priority_queue<CompactSolution, std::vector<CompactSolution>, std::greater<CompactSolution>>;

constexpr int kMaxSlotValue = 20;
constexpr int kMaxTotalAttrValue = 120;
constexpr int kMinGreedyScanBase = 64;
constexpr int kGreedyScanPerRemainingSlot = 32;

static_assert(Constants::CUDA_ATTR_DIM == 24, "AVX2 helpers assume 24 dense slots");

struct DenseModuleData {
    DenseSlotArray slot_values = {};
    int total_attr_value = 0;
};

struct ClusteredBeamModule {
    const ModuleInfo* module = nullptr;
    DenseModuleData dense;
    size_t original_index = 0;
    int primary_slot = Constants::CUDA_ATTR_DIM;
    int primary_power = 0;
    int max_special_power = 0;
    int total_attr_value = 0;
};

struct BeamState {
    std::array<uint16_t, 5> indices = {};
    DenseSlotArray slot_sums = {};
    int total_attr_value = 0;
    int current_score = 0;
    int bound_score = 0;
    int depth = 0;
    int last_index = -1;
};

bool BetterBeamState(const BeamState& lhs, const BeamState& rhs) {
    if (lhs.bound_score != rhs.bound_score) {
        return lhs.bound_score > rhs.bound_score;
    }
    if (lhs.current_score != rhs.current_score) {
        return lhs.current_score > rhs.current_score;
    }
    if (lhs.depth != rhs.depth) {
        return lhs.depth > rhs.depth;
    }
    return lhs.last_index < rhs.last_index;
}

inline void AddSlotArrays(DenseSlotArray& dst, const DenseSlotArray& src) {
    for (int i = 0; i < Constants::CUDA_ATTR_DIM; i += 8) {
        const __m256i dst_values =
            _mm256_loadu_si256(reinterpret_cast<const __m256i*>(dst.data() + i));
        const __m256i src_values =
            _mm256_loadu_si256(reinterpret_cast<const __m256i*>(src.data() + i));
        _mm256_storeu_si256(
            reinterpret_cast<__m256i*>(dst.data() + i),
            _mm256_add_epi32(dst_values, src_values));
    }
}

inline void AddSuffixUpperBound(
    DenseSlotArray& dst,
    size_t next_start,
    int remaining_slots,
    const std::array<std::vector<BeamPickArray>, Constants::CUDA_ATTR_DIM>& suffix_slot_best) {

    DenseSlotArray suffix_delta = {};
    for (int slot = 0; slot < Constants::CUDA_ATTR_DIM; ++slot) {
        suffix_delta[slot] = suffix_slot_best[slot][next_start][remaining_slots];
    }
    AddSlotArrays(dst, suffix_delta);
}

inline bool MeetsMinAttrRequirements(
    const DenseSlotArray& slot_sums,
    const std::vector<int>& min_attr_requirements) {

    for (int slot = 0; slot < Constants::CUDA_ATTR_DIM; ++slot) {
        if (min_attr_requirements[slot] > 0 &&
            slot_sums[slot] < min_attr_requirements[slot]) {
            return false;
        }
    }
    return true;
}

inline int CurrentBeamThreshold(const CompactMinHeap& top_solutions, int max_solutions) {
    if (top_solutions.size() < static_cast<size_t>(max_solutions)) {
        return std::numeric_limits<int>::min();
    }
    return top_solutions.top().score;
}

void TrimBeamStates(std::vector<BeamState>& states, int limit, bool keep_sorted_prefix) {
    if (states.empty()) {
        return;
    }

    if (static_cast<int>(states.size()) <= limit) {
        if (keep_sorted_prefix) {
            std::sort(states.begin(), states.end(), BetterBeamState);
        }
        return;
    }

    auto middle = states.begin() + limit;
    if (keep_sorted_prefix) {
        std::partial_sort(states.begin(), middle, states.end(), BetterBeamState);
    } else {
        std::nth_element(states.begin(), middle, states.end(), BetterBeamState);
    }
    states.resize(static_cast<size_t>(limit));
}

std::vector<int> BuildSlotValuePower(
    const std::unordered_set<int>& target_attributes,
    const std::unordered_set<int>& exclude_attributes) {

    std::vector<int> slot_value_power(Constants::CUDA_ATTR_DIM * 21, 0);
    for (int slot = 0; slot < Constants::CUDA_ATTR_DIM; ++slot) {
        const int attr_id = Constants::CUDA_SLOT_ATTR_IDS[slot];
        int multiplier = 1;
        if (attr_id != 0) {
            if (!target_attributes.empty() && target_attributes.find(attr_id) != target_attributes.end()) {
                multiplier = 2;
            } else if (!exclude_attributes.empty() && exclude_attributes.find(attr_id) != exclude_attributes.end()) {
                multiplier = 0;
            }
        } else {
            multiplier = 0;
        }

        const auto& power_values = Constants::CUDA_SLOT_IS_SPECIAL[slot]
            ? Constants::SPECIAL_ATTR_POWER_VALUES
            : Constants::BASIC_ATTR_POWER_VALUES;
        for (int value = 0; value <= 20; ++value) {
            int max_level = 0;
            for (int level = 0; level < 6; ++level) {
                if (value >= Constants::ATTR_THRESHOLDS[level]) {
                    max_level = level + 1;
                } else {
                    break;
                }
            }
            slot_value_power[slot * 21 + value] =
                (max_level > 0 ? power_values[max_level - 1] * multiplier : 0);
        }
    }
    return slot_value_power;
}

std::vector<int> BuildMinAttrRequirementsDense(
    const std::unordered_map<int, int>& min_attr_sum_requirements) {

    std::vector<int> min_attr_requirements(Constants::CUDA_ATTR_DIM, 0);
    for (const auto& kv : min_attr_sum_requirements) {
        auto slot_it = Constants::CUDA_ATTR_SLOT_MAP.find(kv.first);
        if (slot_it != Constants::CUDA_ATTR_SLOT_MAP.end()) {
            min_attr_requirements[slot_it->second] = kv.second;
        }
    }
    return min_attr_requirements;
}

std::vector<DenseModuleData> BuildDenseModuleData(const std::vector<ModuleInfo>& modules) {
    std::vector<DenseModuleData> dense_modules(modules.size());
    for (size_t module_idx = 0; module_idx < modules.size(); ++module_idx) {
        auto& dense = dense_modules[module_idx];
        for (const auto& part : modules[module_idx].parts) {
            auto slot_it = Constants::CUDA_ATTR_SLOT_MAP.find(part.id);
            if (slot_it != Constants::CUDA_ATTR_SLOT_MAP.end()) {
                dense.slot_values[slot_it->second] += part.value;
            }
            dense.total_attr_value += part.value;
        }
    }
    return dense_modules;
}

int CalculateDenseScore(
    const DenseSlotArray& slot_sums,
    int total_attr_value,
    const std::vector<int>& slot_value_power) {

    int threshold_power = 0;
    for (int slot = 0; slot < Constants::CUDA_ATTR_DIM; ++slot) {
        threshold_power += slot_value_power[slot * 21 + std::min(slot_sums[slot], kMaxSlotValue)];
    }
    return threshold_power + Constants::TOTAL_ATTR_POWER_VALUES[std::min(total_attr_value, kMaxTotalAttrValue)];
}

inline int CalculateDenseScoreWithDelta(
    const DenseSlotArray& base_slots,
    const DenseSlotArray& delta_slots,
    int total_attr_value,
    const std::vector<int>& slot_value_power) {

    int threshold_power = 0;
    for (int slot = 0; slot < Constants::CUDA_ATTR_DIM; ++slot) {
        const int combined = std::min(base_slots[slot] + delta_slots[slot], kMaxSlotValue);
        threshold_power += slot_value_power[slot * 21 + combined];
    }
    return threshold_power + Constants::TOTAL_ATTR_POWER_VALUES[std::min(total_attr_value, kMaxTotalAttrValue)];
}

inline bool NextCombination(uint16_t* comb, size_t r, size_t n) {
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

uint64_t PackIndices(const uint16_t* indices, int combination_size) {
    uint64_t packed = 0;
    for (int i = 0; i < combination_size; ++i) {
        packed |= (static_cast<uint64_t>(indices[i] & 0x0FFFu) << (i * 12));
    }
    return packed;
}

int CalculateDenseScoreByIndices(
    const std::vector<size_t>& indices,
    const std::vector<DenseModuleData>& dense_modules,
    const std::vector<int>& slot_value_power) {

    DenseSlotArray slot_sums = {};
    int total_attr_value = 0;
    for (size_t index : indices) {
        AddSlotArrays(slot_sums, dense_modules[index].slot_values);
        total_attr_value += dense_modules[index].total_attr_value;
    }
    return CalculateDenseScore(slot_sums, total_attr_value, slot_value_power);
}

std::vector<int> BuildDenseModuleMatrix(const std::vector<DenseModuleData>& dense_modules) {
    std::vector<int> module_matrix(
        dense_modules.size() * static_cast<size_t>(Constants::CUDA_ATTR_DIM), 0);
    for (size_t module_idx = 0; module_idx < dense_modules.size(); ++module_idx) {
        std::copy(
            dense_modules[module_idx].slot_values.begin(),
            dense_modules[module_idx].slot_values.end(),
            module_matrix.data() + module_idx * Constants::CUDA_ATTR_DIM);
    }
    return module_matrix;
}

std::vector<ClusteredBeamModule> BuildClusteredBeamModules(
    const std::vector<ModuleInfo>& modules,
    const std::vector<DenseModuleData>& dense_modules,
    const std::vector<int>& slot_value_power,
    int sort_strategy) {

    std::vector<ClusteredBeamModule> clustered;
    clustered.reserve(modules.size());

    for (size_t module_idx = 0; module_idx < modules.size(); ++module_idx) {
        ClusteredBeamModule item;
        item.module = &modules[module_idx];
        item.dense = dense_modules[module_idx];
        item.original_index = module_idx;
        item.total_attr_value = dense_modules[module_idx].total_attr_value;

        for (int slot = 0; slot < Constants::CUDA_ATTR_DIM; ++slot) {
            const int slot_value = std::min(item.dense.slot_values[slot], 20);
            const int slot_power = slot_value_power[slot * 21 + slot_value];
            if (Constants::CUDA_SLOT_IS_SPECIAL[slot]) {
                item.max_special_power = std::max(item.max_special_power, slot_power);
            }
            if (slot_power > item.primary_power ||
                (slot_power == item.primary_power && item.dense.slot_values[slot] > item.dense.slot_values[item.primary_slot == Constants::CUDA_ATTR_DIM ? slot : item.primary_slot]) ||
                (slot_power == item.primary_power && slot < item.primary_slot)) {
                item.primary_slot = slot;
                item.primary_power = slot_power;
            }
        }

        clustered.push_back(std::move(item));
    }

    switch (sort_strategy) {
    case 1:
        std::sort(clustered.begin(), clustered.end(),
            [](const ClusteredBeamModule& lhs, const ClusteredBeamModule& rhs) {
                if (lhs.max_special_power != rhs.max_special_power) {
                    return lhs.max_special_power > rhs.max_special_power;
                }
                if (lhs.primary_slot != rhs.primary_slot) {
                    return lhs.primary_slot < rhs.primary_slot;
                }
                if (lhs.total_attr_value != rhs.total_attr_value) {
                    return lhs.total_attr_value > rhs.total_attr_value;
                }
                return lhs.original_index < rhs.original_index;
            });
        break;
    case 2:
        std::sort(clustered.begin(), clustered.end(),
            [](const ClusteredBeamModule& lhs, const ClusteredBeamModule& rhs) {
                if (lhs.total_attr_value != rhs.total_attr_value) {
                    return lhs.total_attr_value > rhs.total_attr_value;
                }
                if (lhs.primary_power != rhs.primary_power) {
                    return lhs.primary_power > rhs.primary_power;
                }
                return lhs.original_index < rhs.original_index;
            });
        break;
    case 0:
    default:
        std::sort(clustered.begin(), clustered.end(),
            [](const ClusteredBeamModule& lhs, const ClusteredBeamModule& rhs) {
                if (lhs.primary_slot != rhs.primary_slot) {
                    return lhs.primary_slot < rhs.primary_slot;
                }
                if (lhs.primary_power != rhs.primary_power) {
                    return lhs.primary_power > rhs.primary_power;
                }
                if (lhs.total_attr_value != rhs.total_attr_value) {
                    return lhs.total_attr_value > rhs.total_attr_value;
                }
                return lhs.original_index < rhs.original_index;
            });
        break;
    }

    return clustered;
}

void BuildSuffixUpperBounds(
    const std::vector<DenseModuleData>& dense_modules,
    int max_pick,
    std::array<std::vector<BeamPickArray>, Constants::CUDA_ATTR_DIM>& suffix_slot_best,
    std::vector<BeamPickArray>& suffix_total_best) {

    const size_t n = dense_modules.size();
    suffix_total_best.assign(n + 1, BeamPickArray{});
    for (int slot = 0; slot < Constants::CUDA_ATTR_DIM; ++slot) {
        suffix_slot_best[slot].assign(n + 1, BeamPickArray{});
    }

    for (int i = static_cast<int>(n) - 1; i >= 0; --i) {
        suffix_total_best[static_cast<size_t>(i)] = suffix_total_best[static_cast<size_t>(i + 1)];
        for (int slot = 0; slot < Constants::CUDA_ATTR_DIM; ++slot) {
            suffix_slot_best[slot][static_cast<size_t>(i)] = suffix_slot_best[slot][static_cast<size_t>(i + 1)];
        }

        for (int pick = 1; pick <= max_pick; ++pick) {
            suffix_total_best[static_cast<size_t>(i)][pick] = std::max(
                suffix_total_best[static_cast<size_t>(i)][pick],
                dense_modules[static_cast<size_t>(i)].total_attr_value +
                    suffix_total_best[static_cast<size_t>(i + 1)][pick - 1]);

            for (int slot = 0; slot < Constants::CUDA_ATTR_DIM; ++slot) {
                suffix_slot_best[slot][static_cast<size_t>(i)][pick] = std::max(
                    suffix_slot_best[slot][static_cast<size_t>(i)][pick],
                    dense_modules[static_cast<size_t>(i)].slot_values[slot] +
                        suffix_slot_best[slot][static_cast<size_t>(i + 1)][pick - 1]);
            }
        }
    }
}

bool CanSatisfyMinRequirements(
    const DenseSlotArray& slot_sums,
    size_t next_start,
    int remaining_slots,
    const std::vector<int>& min_attr_requirements,
    const std::array<std::vector<BeamPickArray>, Constants::CUDA_ATTR_DIM>& suffix_slot_best) {

    for (int slot = 0; slot < Constants::CUDA_ATTR_DIM; ++slot) {
        const int need = min_attr_requirements[slot];
        if (need <= 0) {
            continue;
        }
        const int optimistic = slot_sums[slot] + suffix_slot_best[slot][next_start][remaining_slots];
        if (optimistic < need) {
            return false;
        }
    }
    return true;
}

int CalculateOptimisticBound(
    const DenseSlotArray& slot_sums,
    int total_attr_value,
    size_t next_start,
    int remaining_slots,
    const std::vector<int>& slot_value_power,
    const std::array<std::vector<BeamPickArray>, Constants::CUDA_ATTR_DIM>& suffix_slot_best,
    const std::vector<BeamPickArray>& suffix_total_best) {

    DenseSlotArray optimistic_slots = slot_sums;
    AddSuffixUpperBound(optimistic_slots, next_start, remaining_slots, suffix_slot_best);
    const int optimistic_total_attr =
        total_attr_value + suffix_total_best[next_start][remaining_slots];
    return CalculateDenseScore(optimistic_slots, optimistic_total_attr, slot_value_power);
}

int GreedyCompletionScore(
    const BeamState& state,
    size_t next_start,
    int remaining_slots,
    const std::vector<DenseModuleData>& dense_modules,
    const std::vector<int>& slot_value_power) {

    if (remaining_slots <= 0 || next_start >= dense_modules.size()) {
        return state.current_score;
    }

    DenseSlotArray greedy_slots = state.slot_sums;
    int greedy_total_attr = state.total_attr_value;
    int greedy_score = state.current_score;
    size_t search_start = next_start;

    for (int pick = 0; pick < remaining_slots && search_start < dense_modules.size(); ++pick) {
        int best_candidate_score = std::numeric_limits<int>::min();
        size_t best_candidate_idx = dense_modules.size();

        const int scan_window = std::max(kMinGreedyScanBase, (remaining_slots - pick) * kGreedyScanPerRemainingSlot);
        const size_t max_greedy_scan = std::min(
            dense_modules.size(),
            search_start + static_cast<size_t>(scan_window));
        for (size_t module_idx = search_start; module_idx < max_greedy_scan; ++module_idx) {
            const int test_total_attr = greedy_total_attr + dense_modules[module_idx].total_attr_value;
            const int test_score = CalculateDenseScoreWithDelta(
                greedy_slots,
                dense_modules[module_idx].slot_values,
                test_total_attr,
                slot_value_power);

            if (test_score > best_candidate_score) {
                best_candidate_score = test_score;
                best_candidate_idx = module_idx;
            }
        }

        if (best_candidate_idx == dense_modules.size()) {
            break;
        }

        AddSlotArrays(greedy_slots, dense_modules[best_candidate_idx].slot_values);
        greedy_total_attr += dense_modules[best_candidate_idx].total_attr_value;
        greedy_score = best_candidate_score;
        search_start = best_candidate_idx + 1;
    }

    return greedy_score;
}

uint64_t PackIndicesFromState(const BeamState& state);

bool IsCombinationUniqueLocal(
    const std::vector<size_t>& indices,
    const std::set<std::vector<size_t>>& seen_combinations) {

    std::vector<size_t> sorted_indices = indices;
    std::sort(sorted_indices.begin(), sorted_indices.end());
    return seen_combinations.find(sorted_indices) == seen_combinations.end();
}

LightweightSolution LocalSearchImproveByIndicesLocal(
    const LightweightSolution& solution,
    const std::vector<DenseModuleData>& dense_modules,
    const std::vector<ModuleInfo>& all_modules,
    int iterations,
    const std::vector<int>& slot_value_power) {

    LightweightSolution best_solution = solution;

    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> module_dis(0, static_cast<int>(all_modules.size()) - 1);

    for (int iteration = 0; iteration < iterations; ++iteration) {
        bool improved = false;

        for (size_t i = 0; i < best_solution.module_indices.size(); ++i) {
            int candidate_count = std::min(20, static_cast<int>(all_modules.size()));
            std::vector<size_t> candidates;

            for (int j = 0; j < candidate_count; ++j) {
                candidates.push_back(static_cast<size_t>(module_dis(gen)));
            }

            for (size_t new_module_idx : candidates) {
                bool already_included = false;
                for (size_t existing_idx : best_solution.module_indices) {
                    if (existing_idx == new_module_idx) {
                        already_included = true;
                        break;
                    }
                }
                if (already_included) {
                    continue;
                }

                std::vector<size_t> new_indices = best_solution.module_indices;
                new_indices[i] = new_module_idx;

                int new_score = CalculateDenseScoreByIndices(
                    new_indices, dense_modules, slot_value_power);
                if (new_score > best_solution.score) {
                    best_solution = LightweightSolution(new_indices, new_score);
                    improved = true;
                    break;
                }
            }

            if (improved) {
                break;
            }
        }

        if (!improved && iteration > iterations / 2) {
            break;
        }
    }

    return best_solution;
}

std::vector<CompactSolution> ProcessCombinationRange(
    size_t start_combination,
    size_t end_combination,
    size_t n,
    const std::vector<DenseModuleData>& dense_modules,
    const std::vector<int>& slot_value_power,
    const std::vector<int>& min_attr_requirements,
    int local_top_capacity,
    int combination_size) {

    size_t range_size = end_combination - start_combination;

    std::vector<CompactSolution> solutions;
    int ext_space = local_top_capacity;
    solutions.reserve(std::min(range_size, static_cast<size_t>(local_top_capacity + ext_space)));
    int current_min = std::numeric_limits<int>::min();

    std::array<uint16_t, 5> combination_buffer = {};
    std::vector<size_t> temp_combination(static_cast<size_t>(combination_size));

    GetCombinationByIndex(n, static_cast<size_t>(combination_size), start_combination, temp_combination);
    for (int j = 0; j < combination_size; ++j) {
        combination_buffer[j] = static_cast<uint16_t>(temp_combination[j]);
    }

    size_t produced = 0;
    while (produced < range_size) {
        DenseSlotArray slot_sums = {};
        int total_attr_value = 0;
        for (int i = 0; i < combination_size; ++i) {
            const auto& dense = dense_modules[combination_buffer[i]];
            AddSlotArrays(slot_sums, dense.slot_values);
            total_attr_value += dense.total_attr_value;
        }

        if (MeetsMinAttrRequirements(slot_sums, min_attr_requirements)) {
            CompactSolution candidate;
            candidate.packed_indices = PackIndices(combination_buffer.data(), combination_size);
            candidate.score = CalculateDenseScore(slot_sums, total_attr_value, slot_value_power);

            if (static_cast<int>(solutions.size()) < local_top_capacity) {
                solutions.emplace_back(candidate);
                if (static_cast<int>(solutions.size()) == local_top_capacity) {
                    int mn = solutions[0].score;
                    for (int i = 1; i < local_top_capacity; ++i) {
                        mn = std::min(mn, solutions[i].score);
                    }
                    current_min = mn;
                }
            } else if (candidate.score > current_min) {
                solutions.emplace_back(candidate);
                if (static_cast<int>(solutions.size()) == local_top_capacity + ext_space) {
                    std::nth_element(
                        solutions.begin(),
                        solutions.begin() + local_top_capacity,
                        solutions.end(),
                        [](const CompactSolution& lhs, const CompactSolution& rhs) {
                            return lhs.score > rhs.score;
                        });
                    solutions.resize(static_cast<size_t>(local_top_capacity));
                    int mn = solutions[0].score;
                    for (int i = 1; i < local_top_capacity; ++i) {
                        mn = std::min(mn, solutions[i].score);
                    }
                    current_min = mn;
                }
            }
        }

        ++produced;
        if (produced >= range_size) {
            break;
        }
        if (!NextCombination(combination_buffer.data(), static_cast<size_t>(combination_size), n)) {
            break;
        }
    }

    if (static_cast<int>(solutions.size()) > local_top_capacity) {
        std::nth_element(
            solutions.begin(),
            solutions.begin() + local_top_capacity,
            solutions.end(),
            [](const CompactSolution& lhs, const CompactSolution& rhs) {
                return lhs.score > rhs.score;
            });
        solutions.resize(static_cast<size_t>(local_top_capacity));
    }

    return solutions;
}

std::vector<LightweightSolution> RunSingleBeam(
    const std::vector<ModuleInfo>& modules,
    const std::vector<DenseModuleData>& dense_modules_raw,
    const std::vector<int>& slot_value_power,
    const std::vector<int>& min_attr_requirements,
    int sort_strategy,
    int beam_width,
    int max_solutions,
    int expand_per_state,
    int combination_size) {

    const auto clustered_modules =
        BuildClusteredBeamModules(modules, dense_modules_raw, slot_value_power, sort_strategy);

    std::vector<ModuleInfo> beam_modules;
    beam_modules.reserve(clustered_modules.size());
    std::vector<DenseModuleData> dense_modules;
    dense_modules.reserve(clustered_modules.size());
    std::vector<size_t> sorted_to_original;
    sorted_to_original.reserve(clustered_modules.size());
    for (const auto& item : clustered_modules) {
        beam_modules.push_back(*item.module);
        dense_modules.push_back(item.dense);
        sorted_to_original.push_back(item.original_index);
    }

    std::array<std::vector<BeamPickArray>, Constants::CUDA_ATTR_DIM> suffix_slot_best;
    std::vector<BeamPickArray> suffix_total_best;
    BuildSuffixUpperBounds(dense_modules, combination_size, suffix_slot_best, suffix_total_best);

    std::vector<BeamState> frontier;
    frontier.reserve(static_cast<size_t>(std::min(beam_width, static_cast<int>(beam_modules.size()))));
    CompactMinHeap top_solutions;

    for (size_t module_idx = 0; module_idx < beam_modules.size(); ++module_idx) {
        BeamState state;
        state.indices[0] = static_cast<uint16_t>(module_idx);
        state.depth = 1;
        state.last_index = static_cast<int>(module_idx);
        state.slot_sums = dense_modules[module_idx].slot_values;
        state.total_attr_value = dense_modules[module_idx].total_attr_value;
        state.current_score = CalculateDenseScore(state.slot_sums, state.total_attr_value, slot_value_power);

        const int remaining_slots = combination_size - state.depth;
        const size_t next_start = module_idx + 1;
        if (!CanSatisfyMinRequirements(
                state.slot_sums,
                next_start,
                remaining_slots,
                min_attr_requirements,
                suffix_slot_best)) {
            continue;
        }

        const int optimistic_bound = CalculateOptimisticBound(
            state.slot_sums,
            state.total_attr_value,
            next_start,
            remaining_slots,
            slot_value_power,
            suffix_slot_best,
            suffix_total_best);
        if (optimistic_bound <= CurrentBeamThreshold(top_solutions, max_solutions)) {
            continue;
        }
        state.bound_score = std::max(
            state.current_score,
            GreedyCompletionScore(
                state,
                next_start,
                remaining_slots,
                dense_modules,
                slot_value_power));
        frontier.push_back(state);
    }

    TrimBeamStates(frontier, beam_width, false);

    while (!frontier.empty() && frontier.front().depth < combination_size) {
        std::vector<BeamState> next_frontier;
        const int next_depth = frontier.front().depth + 1;

        for (const auto& parent : frontier) {
            std::vector<BeamState> local_children;
            if (expand_per_state > 0) {
                local_children.reserve(static_cast<size_t>(std::min(expand_per_state, static_cast<int>(beam_modules.size()))));
            }

            for (size_t module_idx = static_cast<size_t>(parent.last_index + 1); module_idx < beam_modules.size(); ++module_idx) {
                BeamState child = parent;
                child.indices[static_cast<size_t>(parent.depth)] = static_cast<uint16_t>(module_idx);
                child.depth = next_depth;
                child.last_index = static_cast<int>(module_idx);

                AddSlotArrays(child.slot_sums, dense_modules[module_idx].slot_values);
                child.total_attr_value += dense_modules[module_idx].total_attr_value;
                child.current_score = CalculateDenseScore(child.slot_sums, child.total_attr_value, slot_value_power);

                const int remaining_slots = combination_size - child.depth;
                const size_t next_start = module_idx + 1;
                if (!CanSatisfyMinRequirements(
                        child.slot_sums,
                        next_start,
                        remaining_slots,
                        min_attr_requirements,
                        suffix_slot_best)) {
                    continue;
                }

                const int optimistic_bound = CalculateOptimisticBound(
                    child.slot_sums,
                    child.total_attr_value,
                    next_start,
                    remaining_slots,
                    slot_value_power,
                    suffix_slot_best,
                    suffix_total_best);
                if (optimistic_bound <= CurrentBeamThreshold(top_solutions, max_solutions)) {
                    continue;
                }
                child.bound_score = std::max(
                    child.current_score,
                    GreedyCompletionScore(
                        child,
                        next_start,
                        remaining_slots,
                        dense_modules,
                        slot_value_power));

                if (child.depth == combination_size) {
                    if (!MeetsMinAttrRequirements(child.slot_sums, min_attr_requirements)) {
                        continue;
                    }
                    CompactSolution compact;
                    compact.packed_indices = PackIndicesFromState(child);
                    compact.score = child.current_score;
                    if (top_solutions.size() < static_cast<size_t>(max_solutions)) {
                        top_solutions.push(compact);
                    } else if (compact.score > top_solutions.top().score) {
                        top_solutions.pop();
                        top_solutions.push(compact);
                    }
                } else if (expand_per_state > 0) {
                    local_children.push_back(child);
                } else {
                    next_frontier.push_back(child);
                }
            }

            if (expand_per_state > 0 && !local_children.empty()) {
                TrimBeamStates(local_children, expand_per_state, false);
                next_frontier.insert(next_frontier.end(), local_children.begin(), local_children.end());
            }
        }

        if (next_depth >= combination_size) {
            break;
        }

        TrimBeamStates(next_frontier, beam_width, true);
        frontier.swap(next_frontier);
    }

    std::vector<CompactSolution> compact_results;
    compact_results.reserve(top_solutions.size());
    while (!top_solutions.empty()) {
        compact_results.push_back(top_solutions.top());
        top_solutions.pop();
    }
    std::reverse(compact_results.begin(), compact_results.end());

    std::vector<LightweightSolution> refined_solutions;
    refined_solutions.reserve(compact_results.size());
    std::set<std::vector<size_t>> seen_combinations;
    constexpr int kBeamLocalSearchIterations = 20;

    for (const auto& compact : compact_results) {
        LightweightSolution solution(compact.unpack_indices_vector(combination_size), compact.score);
        auto improved_solution = LocalSearchImproveByIndicesLocal(
            solution,
            dense_modules,
            beam_modules,
            kBeamLocalSearchIterations,
            slot_value_power);

        std::vector<size_t> original_indices;
        original_indices.reserve(improved_solution.module_indices.size());
        for (size_t sorted_index : improved_solution.module_indices) {
            original_indices.push_back(sorted_to_original[sorted_index]);
        }

        LightweightSolution original_solution(original_indices, improved_solution.score);
        if (IsCombinationUniqueLocal(original_solution.module_indices, seen_combinations)) {
            std::vector<size_t> sorted_indices = original_solution.module_indices;
            std::sort(sorted_indices.begin(), sorted_indices.end());
            seen_combinations.insert(sorted_indices);
            refined_solutions.push_back(original_solution);
        }
    }

    std::sort(refined_solutions.begin(), refined_solutions.end(),
        [](const LightweightSolution& lhs, const LightweightSolution& rhs) {
            return lhs.score > rhs.score;
        });
    if (static_cast<int>(refined_solutions.size()) > max_solutions) {
        refined_solutions.resize(static_cast<size_t>(max_solutions));
    }

    return refined_solutions;
}

uint64_t PackIndicesFromState(const BeamState& state) {
    uint64_t packed = 0;
    for (int i = 0; i < state.depth; ++i) {
        packed |= (static_cast<uint64_t>(state.indices[i] & 0x0FFFu) << (i * 12));
    }
    return packed;
}

std::vector<ModuleSolution> BuildGpuSolutions(
    const std::vector<ModuleInfo>& modules,
    int gpu_result_count,
    const std::vector<int>& gpu_scores,
    const std::vector<long long>& gpu_indices,
    int combination_size) {

    std::vector<ModuleSolution> final_solutions;
    final_solutions.reserve(std::max(0, gpu_result_count));

    for (int i = 0; i < gpu_result_count; ++i) {
        long long packed = gpu_indices[i];
        std::vector<ModuleInfo> solution_modules;
        solution_modules.reserve(static_cast<size_t>(combination_size));

        for (int j = 0; j < combination_size; ++j) {
            size_t module_idx = static_cast<size_t>((packed >> (j * 12)) & 0x0FFF);
            if (module_idx < modules.size()) {
                solution_modules.push_back(modules[module_idx]);
            }
        }

        auto result = ModuleOptimizerCpp::CalculateCombatPower(solution_modules);
        final_solutions.emplace_back(solution_modules, gpu_scores[i], result.second);
    }

    return final_solutions;
}
} // namespace


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
        
        int total_attr_power = total_attr_power = Constants::TOTAL_ATTR_POWER_VALUES[std::min(total_attr_value, 120)];
        
        int total_power = threshold_power + total_attr_power;
        
        std::map<std::string, int> result_map;
        for (const auto& [key, value] : attr_breakdown) {
            result_map.emplace(key, value);
        }
        
        return {total_power, result_map};
}

std::vector<ModuleSolution> ModuleOptimizerCpp::StrategyEnumeration(
    const std::vector<ModuleInfo>& modules,
    const std::unordered_set<int>& target_attributes,
    const std::unordered_set<int>& exclude_attributes,
    const std::unordered_map<int, int>& min_attr_sum_requirements,
    int max_solutions,
    int max_workers,
    int combination_size) {

    const auto slot_value_power = BuildSlotValuePower(target_attributes, exclude_attributes);
    const auto dense_modules = BuildDenseModuleData(modules);
    const auto min_attr_requirements = BuildMinAttrRequirementsDense(min_attr_sum_requirements);
    size_t n = modules.size();
    size_t total_combinations = CombinationCount(n, static_cast<size_t>(combination_size));

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
        futures.push_back(pool->enqueue(
            [start_combination, end_combination, n, local_top_capacity, combination_size,
             &dense_modules, &slot_value_power, &min_attr_requirements]() {
                return ProcessCombinationRange(
                    start_combination, end_combination, n,
                    dense_modules, slot_value_power, min_attr_requirements,
                    local_top_capacity, combination_size);
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
        auto indices = solution.unpack_indices_vector(combination_size);
        std::vector<ModuleInfo> solution_modules;
        solution_modules.reserve(indices.size());
        for (size_t index : indices) {
            solution_modules.push_back(modules[index]);
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
    int max_workers,
    int combination_size) {

#ifdef USE_CUDA
    (void)max_workers;
    if (TestCuda()) {
        printf("CUDA GPU acceleration enabled - dense LUT kernel\n");

        const auto slot_value_power = BuildSlotValuePower(target_attributes, exclude_attributes);
        const auto dense_modules = BuildDenseModuleData(modules);
        const auto min_attr_requirements = BuildMinAttrRequirementsDense(min_attr_sum_requirements);
        const auto module_matrix = BuildDenseModuleMatrix(dense_modules);

        std::vector<int> gpu_scores(max_solutions);
        std::vector<long long> gpu_indices(max_solutions);

        int gpu_result_count = GpuStrategyEnumeration(
            module_matrix.data(),
            static_cast<int>(modules.size()),
            slot_value_power.data(),
            min_attr_requirements.data(),
            max_solutions,
            gpu_scores.data(),
            gpu_indices.data(),
            combination_size);

        return BuildGpuSolutions(modules, gpu_result_count, gpu_scores, gpu_indices, combination_size);
    }

    printf("CUDA not available, using CPU optimized version\n");
    return StrategyEnumeration(
        modules, target_attributes, exclude_attributes,
        min_attr_sum_requirements, max_solutions, max_workers, combination_size);
#else
    return StrategyEnumeration(
        modules, target_attributes, exclude_attributes,
        min_attr_sum_requirements, max_solutions, max_workers, combination_size);
#endif
}

#ifdef USE_OPENCL
extern "C" int TestOpenCL();
#endif

std::vector<ModuleSolution> ModuleOptimizerCpp::StrategyEnumerationGPU(
    const std::vector<ModuleInfo>& modules,
    const std::unordered_set<int>& target_attributes,
    const std::unordered_set<int>& exclude_attributes,
    const std::unordered_map<int, int>& min_attr_sum_requirements,
    int max_solutions,
    int max_workers,
    int combination_size) {
#ifdef USE_CUDA
    if (TestCuda()) {
        return StrategyEnumerationCUDA(
            modules, target_attributes, exclude_attributes,
            min_attr_sum_requirements, max_solutions, max_workers, combination_size);
    }
#endif

#ifdef USE_OPENCL
    if (combination_size <= 4 && TestOpenCL()) {
        return StrategyEnumerationOpenCL(
            modules, target_attributes, exclude_attributes,
            min_attr_sum_requirements, max_solutions, max_workers, combination_size);
    }
#endif
    return StrategyEnumeration(
        modules, target_attributes, exclude_attributes,
        min_attr_sum_requirements, max_solutions, max_workers, combination_size);
}

std::vector<ModuleSolution> ModuleOptimizerCpp::StrategyBeamSearch(
    const std::vector<ModuleInfo>& modules,
    const std::unordered_set<int>& target_attributes,
    const std::unordered_set<int>& exclude_attributes,
    const std::unordered_map<int, int>& min_attr_sum_requirements,
    int max_solutions,
    int beam_width,
    int expand_per_state,
    int combination_size,
    int max_workers) {

    if (modules.empty() || max_solutions <= 0 || beam_width <= 0) {
        return {};
    }
    if (combination_size <= 0 || combination_size > 5 ||
        static_cast<size_t>(combination_size) > modules.size()) {
        return {};
    }
    const int worker_count = std::max(1, max_workers);

    const auto slot_value_power = BuildSlotValuePower(target_attributes, exclude_attributes);
    const auto dense_modules_raw = BuildDenseModuleData(modules);
    const auto min_attr_requirements = BuildMinAttrRequirementsDense(min_attr_sum_requirements);
    constexpr int kBeamStrategyCount = 3;
    auto pool = std::make_unique<SimpleThreadPool>(static_cast<size_t>(std::min(worker_count, kBeamStrategyCount)));
    std::vector<std::future<std::vector<LightweightSolution>>> futures;
    futures.reserve(kBeamStrategyCount);

    for (int strategy = 0; strategy < kBeamStrategyCount; ++strategy) {
        futures.push_back(pool->enqueue(
            [&, strategy]() {
                return RunSingleBeam(
                    modules,
                    dense_modules_raw,
                    slot_value_power,
                    min_attr_requirements,
                    strategy,
                    beam_width,
                    max_solutions,
                    expand_per_state,
                    combination_size);
            }));
    }

    std::vector<LightweightSolution> all_solutions;
    for (auto& future : futures) {
        auto batch = future.get();
        all_solutions.insert(all_solutions.end(), batch.begin(), batch.end());
    }
    pool.reset();

    std::set<std::vector<size_t>> seen_combinations;
    std::vector<LightweightSolution> unique_solutions;
    unique_solutions.reserve(all_solutions.size());
    std::sort(all_solutions.begin(), all_solutions.end(),
        [](const LightweightSolution& lhs, const LightweightSolution& rhs) {
            return lhs.score > rhs.score;
        });
    for (const auto& solution : all_solutions) {
        if (IsCombinationUnique(solution.module_indices, seen_combinations)) {
            std::vector<size_t> sorted_indices = solution.module_indices;
            std::sort(sorted_indices.begin(), sorted_indices.end());
            seen_combinations.insert(sorted_indices);
            unique_solutions.push_back(solution);
            if (static_cast<int>(unique_solutions.size()) >= max_solutions) {
                break;
            }
        }
    }

    std::vector<ModuleSolution> final_solutions;
    final_solutions.reserve(unique_solutions.size());
    for (const auto& solution : unique_solutions) {
        std::vector<ModuleInfo> solution_modules;
        solution_modules.reserve(solution.module_indices.size());
        for (size_t original_index : solution.module_indices) {
            solution_modules.push_back(modules[original_index]);
        }
        auto result = CalculateCombatPower(solution_modules);
        final_solutions.emplace_back(solution_modules, solution.score, result.second);
    }

    return final_solutions;
}

bool ModuleOptimizerCpp::IsCombinationUnique(
    const std::vector<size_t>& indices,
    const std::set<std::vector<size_t>>& seen_combinations) {
    
    std::vector<size_t> sorted_indices = indices;
    std::sort(sorted_indices.begin(), sorted_indices.end());
    
    return seen_combinations.find(sorted_indices) == seen_combinations.end();
}