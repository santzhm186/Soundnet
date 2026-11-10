#pragma once
#include "../../platform/NodeDescriptor.hpp"
namespace tri::nodes::channel_filter {
inline constexpr int typeID=nodeTypeID("soundnet.channel-filter");
inline constexpr int ChannelFilter=parameterID("soundnet.channel-filter","channelFilter.channel");
inline constexpr ParamInfo ChannelFilterInfo{"Channel",0.0f,16.0f,0.0f,""};
inline std::vector<std::pair<int,ParamSpec>> parameterSpecs(){return {{ChannelFilter,ParamSpec{&ChannelFilterInfo,"channelFilter.channel",true,nullptr,0,ParamGroup(0)}}};}
}
