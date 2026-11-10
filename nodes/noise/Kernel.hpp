#pragma once
#include "../../platform/runtime/ProcessorKernel.hpp"
#include "Processor.hpp"
namespace tri::nodes::noise {
using Kernel=ProcessorKernel<NoiseProcessor>;
inline std::unique_ptr<NodeKernel> create(){return std::make_unique<Kernel>();}
}
