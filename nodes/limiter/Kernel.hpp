#pragma once
#include "../../platform/runtime/ProcessorKernel.hpp"
#include "Processor.hpp"
namespace tri::nodes::limiter {
using Kernel=ProcessorKernel<LimiterProcessor>;
inline std::unique_ptr<NodeKernel> create(){return std::make_unique<Kernel>();}
}
