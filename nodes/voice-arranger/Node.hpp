#pragma once
#include "Parameters.hpp"
#include "Translations.hpp"
#include "Kernel.hpp"
// soundnet-module: soundnet.voice-arranger
// soundnet-order: 12
// Independent SDK module; stable keys remain scoped to this node.
namespace tri::nodes::voice_arranger {
inline bool validate(const KernelConfig& config,std::string& why) {
    const int mins[]={ArrHighMin,ArrMidMin,ArrLowMin},maxs[]={ArrHighMax,ArrMidMax,ArrLowMax};
    const float minDefaults[]={72,55,36},maxDefaults[]={95,71,54};
    for(int v=0;v<3;++v)if(config.value(mins[v],minDefaults[v])>config.value(maxs[v],maxDefaults[v])) {
        why="同一声部的最低音不能高于最高音。";return false;
    }
    return true;
}
inline NodeModule module() {
    using namespace tri::detail;
    const uint8_t req=0;
    NodeSpec spec{"soundnet.voice-arranger",typeID,"三声部编排器","音符",1,1,true,true,
             CapNoteSource|CapData|CapMultiOutput,TimeMode::Timed,0,
             {pin("notes","音符",Signal::Midi,RoleMain)},
             {pout("high","高声部",Signal::Midi,RoleMain),pout("middle","中声部",Signal::Midi,RoleMain),
              pout("low","低声部",Signal::Midi,RoleMain)},
             {ArrHighMin,ArrHighMax,ArrHighTexture,ArrMidMin,ArrMidMax,ArrMidTexture,
              ArrLowMin,ArrLowMax,ArrLowTexture}, nullptr};
    spec.validateConfig=validate;
    spec.kernelFactory=create;
    spec.customParameterEditor=true;
    NodeModule result{std::move(spec),parameterSpecs()};
    appendTranslations(result);
    return result;
}
} // namespace tri::nodes::voice_arranger
