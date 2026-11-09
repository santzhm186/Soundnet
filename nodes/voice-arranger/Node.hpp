#pragma once
#include "../_legacy/Declarations.hpp"
// soundnet-module: soundnet.voice-arranger
// soundnet-order: 12
// Descriptor is independent. Existing DSP uses the documented legacy adapter.
namespace tri::nodes::voice_arranger {
inline bool validate(const KernelConfig& config,std::string& why) {
    for(int v=0;v<3;++v) {
        const int p=ArrHighMin+3*v;
        if(config.value(p,arrangerVoiceInfo[v][0].def)>config.value(p+1,arrangerVoiceInfo[v][1].def)) {
            why="同一声部的最低音不能高于最高音。";return false;
        }
    }
    return true;
}
inline NodeModule module() {
    using namespace tri::detail;
    const uint8_t req=0;
    NodeSpec spec{"soundnet.voice-arranger",ArrangerNode,"三声部编排器","处理",1,1,true,true,
             CapNoteSource|CapData|CapMultiOutput,TimeMode::Timed,0,
             {pin("notes","音符",Signal::Midi,RoleMain)},
             {pout("high","高声部",Signal::Midi,RoleMain),pout("middle","中声部",Signal::Midi,RoleMain),
              pout("low","低声部",Signal::Midi,RoleMain)},
             {ArrHighMin,ArrHighMax,ArrHighTexture,ArrMidMin,ArrMidMax,ArrMidTexture,
              ArrLowMin,ArrLowMax,ArrLowTexture}, nullptr};
    spec.validateConfig=validate;
    spec.legacyAdapter=true;
    return {std::move(spec),{}};
}
} // namespace tri::nodes::voice_arranger
