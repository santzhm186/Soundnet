#pragma once
#include "../../platform/NodeDescriptor.hpp"
// soundnet-module: soundnet.timecode
// soundnet-order: 98
#include "Kernel.hpp"
namespace tri::nodes::timecode {
inline NodeModule module() {
    using namespace tri::detail;
    NodeSpec spec{"soundnet.timecode",nodeTypeID("soundnet.timecode"),"时间码","工程",1,1,true,true,
        CapStream|CapGenerator|CapClockSource,TimeMode::Stream,0,{},
        {pout("time","时间码",Signal::Clock,RoleClock)},{},nullptr};
    spec.kernelFactory=create;
    spec.environmentRole=EnvironmentRole::Timecode;
    return {std::move(spec),{},{{"时间码","Timecode"},{"工程","Project"}}};
}
}
