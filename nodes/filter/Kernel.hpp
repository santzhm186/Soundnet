#pragma once
#include "../../platform/runtime/ProcessorKernel.hpp"
#include "Processor.hpp"
namespace tri::nodes::filter {
using Kernel=ProcessorKernel<FilterProcessor>;
inline std::unique_ptr<NodeKernel> create(){return std::make_unique<Kernel>();}
}
