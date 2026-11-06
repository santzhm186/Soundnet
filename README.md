# Soundnet

原生 macOS 节点工作流合成器，同时提供独立 App 和可供 Logic Pro 加载的 Audio Unit v2 乐器。

## 节点与工作流

- 可拖动节点、接线、改接、断线、移除与恢复模块，带缩放、平移、撤销与重做。
- 三路振荡器，各有正弦、方波、锯齿、三角波、音高、电平和独立 A/S/R。
- MIDI 与立体声音频输入，四路混音、三段 EQ、Delay。
- 和弦输入节点，支持预制和弦和手动输入，如 F#m7。
- 和声走向节点，支持流行、爵士、Blues 模板，以及最多八个自定义和弦，如 Cmaj7 | Am7 | Dm7 | G7。
- 节奏速度节点，统一驱动 ARP、走向和力度变化，可跟随宿主 BPM。
- 力度自动化节点，设置最小/最大力度、拍数和正弦、渐强或强弱交替。
- 选择「工作流 → 和声编曲工作流」，点走向节点，勾选「循环演奏走向」即可开始。
- ARP 包含 Bossa Nova、Reggae 等节奏型，支持每音重复 1–8 次。
- JSON 工作流保存与加载，Logic 工程保存节点位置与接线。

## 一切皆节点

音乐输入、处理与输出通过带类型的端口组合。音符、音频和节奏速度分别有自己的连线，节点定义集中在共享引擎中。当前提供 14 个模块槽位，每个模块可移除、添加和重新连接。和弦信号可送至力度、ARP 或振荡器。

## 构建

需要 macOS 13+、支持 C++23 的 Xcode Command Line Tools。已在 Apple Silicon 上使用 Apple Clang 21 构建与验证。

```sh
./build-all.sh
```

产物位于 `build/release/Soundnet.app` 和 `build/release/Soundnet.component`，均包含 arm64 与 x86_64，并采用本地签名。

把 component 放到 `~/Library/Audio/Plug-Ins/Components/` 后重新打开 Logic。
为兼容已有工程，Audio Unit 的四字符标识和旧音色格式继续沿用初版。安装时请移走旧的 Ternary.component，保留一份相同标识的插件。

## 验证

```sh
mkdir -p build
xcrun clang++ -O2 -std=c++17 tests/EngineTests.cpp -o build/engine-tests
./build/engine-tests
xcrun clang++ -O2 -std=c++17 tests/MusicNodesTests.cpp -o build/music-tests
./build/music-tests
./build/release/Soundnet.app/Contents/MacOS/Soundnet --self-test
auval -strict -v aumu Tern Trny
```

原生编辑器、实际音频渲染、工作流 JSON、旧音色恢复，以及苹果独立插件进程均经过验证。Intel 架构已编译，硬件运行测试在 Apple Silicon 上完成。

## 源码

- `src/` 共享 C++ DSP。
- `ui/` AppKit 节点画布与参数编辑。
- `app/` AVAudioEngine 与 CoreMIDI 独立宿主。
- `au/` Audio Unit 及独立宿主进程状态同步。
- `tests/` 与 `tools/` 音频和交互验证。
- `examples/` 可加载的工作流。
- [中文使用说明](docs/使用说明.txt)

Apple AudioUnitSDK 的第三方源码及 Apache 2.0 许可证位于 `au/vendor/`。
