#pragma once
#include "../../platform/NodeDescriptor.hpp"
namespace tri::nodes::scale_quantize {
inline constexpr int typeID=nodeTypeID("soundnet.scale-quantize");
inline constexpr int QuantizeRoot=parameterID("soundnet.scale-quantize","quantize.root");
inline constexpr ParamInfo QuantizeRootInfo{"Root",0.0f,11.0f,0.0f,""};
inline constexpr const char* QuantizeRootOptions[]={"C","C#","D","D#","E","F","F#","G","G#","A","A#","B"};
inline constexpr int QuantizeScale=parameterID("soundnet.scale-quantize","quantize.scale");
inline constexpr ParamInfo QuantizeScaleInfo{"Scale",0.0f,4.0f,0.0f,""};
inline constexpr const char* QuantizeScaleOptions[]={"大调","小调","多利亚","五声","半音"};
inline constexpr int QuantizeMode=parameterID("soundnet.scale-quantize","quantize.mode");
inline constexpr ParamInfo QuantizeModeInfo{"Mode",0.0f,2.0f,0.0f,""};
inline constexpr const char* QuantizeModeOptions[]={"就近","向上","向下"};
inline std::vector<std::pair<int,ParamSpec>> parameterSpecs(){return {{QuantizeRoot,ParamSpec{&QuantizeRootInfo,"quantize.root",true,QuantizeRootOptions,12,ParamGroup(8)}},{QuantizeScale,ParamSpec{&QuantizeScaleInfo,"quantize.scale",true,QuantizeScaleOptions,5,ParamGroup(8)}},{QuantizeMode,ParamSpec{&QuantizeModeInfo,"quantize.mode",true,QuantizeModeOptions,3,ParamGroup(8)}}};}
}
