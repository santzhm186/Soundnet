# 示例工程

这些 JSON 是可分发的 v4 内嵌示例：节点身份、参数和端口均使用稳定字符串键，素材随文件携带，每个示例包含时间码与项目设定两个环境节点。用当前平台打开后，正常保存会写成 v6 工程清单，并把素材交给工程文件夹管理；无需手工转换素材。

- `Soundnet1`、`layered-arp`、`sampler-pluck` 需要 MIDI 输入。`filter-reverb-noise` 的噪声支路会直接发声，振荡器支路需要 MIDI 输入。
- `clip-model` 需要开启 MIDI Clip 的播放参数；`composition` 需要开启和弦进行的播放参数。
- 含开始节点的示例点击「开始」发声；离线导出会自动触发一次开始。`random-gate` 保留 65% 通过概率，单次点击可能没有声音。
- `clip-to-midi` 输出 MIDI，不产生音频。
- `three-loops-then-wait` 的循环完成触发经计数和停止节点形成反馈回路。当前平台将实际反馈边延迟一个处理块，因此第三次完成后的停止在下一块生效；随后等待一秒启动第二个片段。
- `three-voice-arrangement.json` 和 `three-voice-arrangement.v4.json` 都是当前格式，保留两个文件名供既有教程引用。

本轮迁移保留原有音乐链路、素材和有效参数。旧示例有两种参数误挂：`filter-reverb-noise`、`sampler-pluck` 的输出音量曾写在参数 23（实际属于琶音器的 Gate），现把原来的 0.7 设为输出节点的 `output.master`；`audio-clip-compressor` 同类多余字段与已有 0.8 音量合并。部分 MIDI Clip 曾携带 Master=1，该参数没有音频增益作用，现移除这个无效字段，已有输出节点的音量保持原值。

19 份示例已用当前平台严格解码，并各渲染一秒确认得到有限且非空的音频或 MIDI。需要手动源的示例在验证副本中启动或注入 MIDI，文件本身仍保留上述操作方式。
