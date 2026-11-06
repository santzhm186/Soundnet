# Soundnet

Soundnet 是 macOS 原生音乐工作流编辑器，提供独立 App 和 Logic Pro 可加载的 Audio Unit v2。画布上有三种连线：音符、时钟、立体声音频。同一条旋律可以触发内置振荡器，也可以经过模型变换后送给其他乐器。声卡或 DAW 侧链的音频可以直接进入效果器。

## 先运行一条线路

1. 打开 Soundnet，选择「工作流 → 打开工作流 JSON…」，载入 [四拍模型示例](examples/clip-model.json)。
2. 点「MIDI Clip」，勾选「启用播放」。五个音符依次经过音高镜像模型和一个振荡器发声。点「节奏速度」可以调整 BPM。
3. 在 Logic 中，把 Soundnet AU 放进软件乐器轨道的乐器插槽，可用同一份工作流。要跟随工程走带，在速度节点选择「DAW 工程速度」，再按 Logic 的播放键。

示例工作流内嵌原始 MIDI 和模型计算出的映射，移到另一台 Mac 也能播放。示例目录同时提供 [原始 MIDI](examples/FourBeat.mid) 和 [Core ML 音高镜像模型](examples/PitchMirror.mlmodel)，可用来试验素材导入。

![Soundnet 中的四拍工作流，速度节点驱动 MIDI Clip，音符经模型变换后送入振荡器和硬件输出。](docs/workflow.png)

## 输入、节奏和输出

| 用途 | 独立 App | Logic / AU 宿主 |
| --- | --- | --- |
| 本地 MIDI | 电脑键盘、屏幕琴键、CoreMIDI 输入 | 插件的屏幕琴键 |
| DAW MIDI | DAW 把轨道送至 `Soundnet MIDI In` | 当前乐器轨的现场 MIDI 或 MIDI 区域 |
| MIDI Clip | 导入标准 MIDI 文件，可单次或循环播放 | 相同，可跟随宿主播放、停止或定位 |
| 时钟 | 速度模块的 BPM，也可对虚拟 MIDI 端口发送 MIDI Clock 和传输消息 | DAW 工程速度和走带，或速度模块自己的 BPM |
| 音频输出 | 系统当前选中的扬声器或声卡 | 当前乐器通道的立体声输出，可继续送往总线或效果器 |
| MIDI 输出 | `Soundnet MIDI Out` 虚拟源 | 宿主须接收 AU 的 MIDI 输出回调 |

「DAW MIDI 演奏」模板可直接弹奏宿主轨道。「MIDI Clip → 模型 → 音频」可自己载入素材。「空白工作流」可从节点库开始搭线。保存的线路可在 App 与 AU 之间互换，两个环境分别连接自己的输出端。

## 节点

- **音符来源与处理**：本地 MIDI、DAW MIDI、MIDI Clip、和弦、和声走向、力度自动化、ARP 和音符模型。和弦与走向都提供预设和自定义输入，例如 `F#m7`、`Cmaj7 | Am7 | Dm7 | G7`。ARP 提供 Bossa Nova、Reggae 等节奏型，每音可重复 1–8 次。
- **时钟**：将节奏速度节点接到 Clip、ARP、走向或力度节点，可以显式控制时间来源。Clip 采用四分音符为拍，保留多轨音符、力度、踏板延音和结尾休止。
- **模型**：载入本地 Core ML 单输入、单输出音高模型（Int64 或 Double 标量），或者包含 128 个 MIDI 音高值的 JSON 映射。导入时对 0–127 每个音高运行一次模型，实时演奏直接使用结果。模型变更可撤销，映射随工程保存。接口适用于由单个音高决定输出的无状态处理。
- **音频**：三个振荡器各有正弦、方波、锯齿、三角波，独立 A/S/R、电平与音高。外部音频、四路混音、三段 EQ、立体声 Delay 和音频输出可以重新组合。单独处理外部声音可使用「外部音频效果器」模板。
- **MIDI 输出**：连接一个音符来源即可驱动下游软件。振荡器和音频输出可以从这条工作流中移除。

1.4 提供 18 个模块槽位，包括三个振荡器槽。可移除、恢复和重新连接各槽位，支持一出多接、信号类型检查和回路检测。旧工程中的 MIDI 输入默认兼容合并，DAW MIDI 节点接出连线后会分离两个来源。

## 构建和安装

需要 macOS 13 或更新版本，以及支持 C++23 的 Xcode Command Line Tools。

```sh
./build-all.sh
```

产物在 `build/release/Soundnet.app` 和 `build/release/Soundnet.component`，均包含 arm64 与 x86_64，并采用本地签名。将 component 放到 `~/Library/Audio/Plug-Ins/Components/` 后，在 Logic 中选择「AU Instruments → Soundnet → Soundnet」。为兼容旧 Logic 工程，Audio Unit 沿用初版的四字符标识。同一插件目录只需安装一份 Soundnet 或旧版 Ternary。

## 验证

```sh
mkdir -p build/tests
for test in EngineTests MusicNodesTests WorkflowSourcesTests; do
  xcrun clang++ -O2 -std=c++17 "tests/$test.cpp" -o "build/tests/$test" && "build/tests/$test" || exit 1
done
xcrun clang++ -O2 -std=c++17 -fobjc-arc tests/AssetImportTests.mm src/WorkflowAssets.mm \
  -framework Foundation -framework CoreML -o build/tests/AssetImportTests
build/tests/AssetImportTests
build/release/Soundnet.app/Contents/MacOS/Soundnet --self-test
auval -strict -v aumu Tern Trny
```

[中文使用说明](docs/使用说明.txt)。Apple AudioUnitSDK 的第三方源码及 Apache 2.0 许可证位于 `au/vendor/`。
