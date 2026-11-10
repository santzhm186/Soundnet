#pragma once
#include "../../platform/runtime/ProcessorKernel.hpp"
#include "Processor.hpp"
namespace tri::nodes::reverb {
using Kernel=ProcessorKernel<ReverbProcessor>;
inline std::unique_ptr<NodeKernel> create(){return std::make_unique<Kernel>();}
}
