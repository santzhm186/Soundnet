#pragma once
#include "../../platform/runtime/ProcessorKernel.hpp"
#include "Processor.hpp"
namespace tri::nodes::noise_gate {
using Kernel=ProcessorKernel<GateProcessor>;
inline std::unique_ptr<NodeKernel> create(){return std::make_unique<Kernel>();}
}
