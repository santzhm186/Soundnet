#pragma once
#include "../_legacy/Declarations.hpp"
// soundnet-module: soundnet.ai-music
// soundnet-order: 9
// Descriptor is independent. Existing DSP uses the documented legacy adapter.
namespace tri::nodes::ai_music {
inline bool validate(const KernelConfig& config,std::string& why) {
    if(config.value(AudioClipStart,0)>=config.value(AudioClipEnd,1)) {
        why="音频起止位置无效：开始必须早于结束。";return false;
    }
    return true;
}
inline NodeModule module() {
    using namespace tri::detail;
    const uint8_t req=0;
    NodeSpec spec{"soundnet.ai-music",AiMusicNode,"AI 音乐生成","输入",1,1,true,true,
             CapTask|CapGenerator|CapAssetAudio|CapStream|CapBypassable,TimeMode::Timed,0,
             {pin("trigger","触发",Signal::Trigger,RoleTrigger,PortOptional|PortFeedback)},
             {pout("audio","音频",Signal::Audio,RoleMain)},
             {AiMusicBackend,AiMusicModel,AiMusicDuration,AiMusicSeed,AiMusicQuality,AiMusicOutMode,
              AudioClipGain,AudioClipLoop,AudioClipStart,AudioClipEnd,AudioClipFade,
              AudioClipPitch,AudioClipReverse,AudioClipXfade},
             "audio",
             {{"ai.prompt","风格描述"},{"ai.lyrics","歌词"},{"ai.execPath","后端可执行文件"},
              {"ai.modelPath","模型文件"},{"ai.extraArgs","附加参数"},{"ai.score","乐谱（ABC）"}}};
    spec.validateConfig=validate;
    for(auto& text:spec.texts) if(std::strcmp(text.id,"ai.lyrics")==0 || std::strcmp(text.id,"ai.score")==0)text.multiline=true;
    spec.legacyAdapter=true;
    return {std::move(spec),{}};
}
} // namespace tri::nodes::ai_music
