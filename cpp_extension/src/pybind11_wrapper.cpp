#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#include <pybind11/functional.h>
#include "module_optimizer.h"

namespace py = pybind11;

PYBIND11_MODULE(module_optimizer_cpp, m) {
    m.doc() = "C++ implementation of module optimizer for performance optimization";
    
    // 绑定ModulePart结构体
    py::class_<ModulePart>(m, "ModulePart")
        .def(py::init<int, const std::string&, int>())
        .def_readwrite("id", &ModulePart::id)
        .def_readwrite("name", &ModulePart::name)
        .def_readwrite("value", &ModulePart::value)
        .def("__repr__", [](const ModulePart& self) {
            return "ModulePart(id=" + std::to_string(self.id) + 
                   ", name='" + self.name + "', value=" + std::to_string(self.value) + ")";
        });
    
    // 绑定ModuleInfo结构体
    py::class_<ModuleInfo>(m, "ModuleInfo")
        .def(py::init<const std::string&, int, int, int, const std::vector<ModulePart>&>())
        .def_readwrite("name", &ModuleInfo::name)
        .def_readwrite("config_id", &ModuleInfo::config_id)
        .def_readwrite("uuid", &ModuleInfo::uuid)
        .def_readwrite("quality", &ModuleInfo::quality)
        .def_readwrite("parts", &ModuleInfo::parts)
        .def("__repr__", [](const ModuleInfo& self) {
            return "ModuleInfo(name='" + self.name + "', uuid='" + std::to_string(self.uuid) + "')";
        });
    
    // 绑定ModuleSolution结构体
    py::class_<ModuleSolution>(m, "ModuleSolution")
        .def(py::init<const std::vector<ModuleInfo>&, int, const std::map<std::string, int>&>())
        .def_readwrite("modules", &ModuleSolution::modules)
        .def_readwrite("score", &ModuleSolution::score)
        .def_readwrite("attr_breakdown", &ModuleSolution::attr_breakdown)
        .def("__repr__", [](const ModuleSolution& self) {
            return "ModuleSolution(score=" + std::to_string(self.score) + 
                   ", modules_count=" + std::to_string(self.modules.size()) + ")";
        });
    
    m.def("strategy_enumeration_cpp", &ModuleOptimizerCpp::StrategyEnumeration,
          "枚举",
          py::arg("modules"),
          py::arg("target_attributes") = std::unordered_set<std::string>{},
          py::arg("max_solutions") = 60,
          py::arg("max_workers") = 8);
    
    m.def("optimize_modules_cpp", &ModuleOptimizerCpp::OptimizeModules,
          "贪心+局部搜索",
          py::arg("modules"),
          py::arg("target_attributes") = std::unordered_set<std::string>{},
          py::arg("max_solutions") = 60,
          py::arg("max_attempts_multiplier") = 20,
          py::arg("local_search_iterations") = 30);
}
