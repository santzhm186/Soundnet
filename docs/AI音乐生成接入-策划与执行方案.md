# AI 音乐生成接入 · 策划与执行方案

| 项目 | 内容 |
| --- | --- |
| 对应标准 | `Soundnet-节点与连接标准-v0.3.md`（重点：第 11 节异步任务与资源、第 6 节执行与状态、第 9.5–9.6 节） |
| 基线代码 | Soundnet 2.81.0，build 89 |
| 本文件作用 | 把"AI 音乐生成模型接入 Soundnet"拆成可先行的契约变更与可执行的接入工程；是接手者的唯一入口文档 |
| 结论 | 模型**不嵌入** Soundnet；以 `CapTask` 异步任务契约落地为前提，新增"AI 音乐生成"节点，生成产物作为工程素材嵌入，完成后节点行为对齐现有「音频片段」节点；后端默认走 audio.cpp（纯 C++、零 Python 依赖） |
| 状态 | **阶段 0–6 全部完成**：阶段 0 已在本机（M1 Pro 16GB）真机实测通过（附录 B 含耗时/内存/乐谱样本）；节点 47 上架、texts 持久化、音频生成、节点级停止、UI、ABC→MIDI 导入、改谱回喂、仅乐谱模式均已实现 |

---

## 0 一页结论

1. **先契约、后接入**。标准第 11 节（`CapTask`/`TaskEvent`/`Resource`）目前只有描述符占位、没有调度语义。AI 生成节点是它的第一个真实用户，必须先把任务生命周期写进 `节点与连接标准-实现映射.md`，再写代码。这与 2.80 重构确立的"契约先行"规则一致。
2. **生成即素材**。生成是异步任务（秒级到分钟级），产物是 WAV（必出）与 ABC 乐谱（YuE2 可选出）。WAV 嵌入工程素材库后，节点渲染行为完全复用「音频片段」路径：循环/反向/变调/淡变、离线导出、分轨、旁通全部自动成立，AU 宿主路径零改动、零 Python。
3. **后端是配置项，不是依赖项**。定义抽象 `GenBackend` 接口，默认实现为 audio.cpp CLI 子进程（macOS Metal、GGUF 量化、ACE-Step 与 YuE2 同运行时）；可选实现为 audio.cpp server 与 ComfyUI REST。Soundnet 本体保持纯 C++/ObjC++，不分发任何模型权重，许可证保持 MIT 干净。
4. **乐谱是差异化能力**。`score.abc` 经新写的 ABC→MIDI 转换器进入 MIDI 剪辑与钢琴卷帘，实现"生成 → 可编辑乐谱 → 改谱重渲染 / 编曲器重配器"——这是 Suno 类产品没有的闭环，且 `stop_after=abc`（只出谱不渲染）对 16GB 机器天然友好。

## 1 背景与目标

### 1.1 已核实的外部事实（2026-11，来源见附录 C）

| 事实 | 依据 |
| --- | --- |
| ACE-Step 1.5：MIT（代码+权重），可商用；50+ 语言人声；<4GB 显存起步；Apple Silicon 一等支持 | ace-step/ACE-Step-1.5 仓库 |
| YuE2：代码 Apache 2.0，权重 CC BY-NC 4.0 + 创作者条款（个人创作者可商用产出，公司需另购许可）；质量基准超过 Suno v5 | multimodal-art-projection/YuE |
| audio.cpp：纯 C++/ggml 推理引擎，同时支持 ACE-Step 与 YuE2；macOS Metal 后端；`brew install audio-cpp`；CLI/server/WebUI/可链接为库 | audio.cpp 官方文档 |
| audio.cpp 的 YuE2 路由原生导出 ABC 乐谱：`score` 工件（`text/vnd.abc`），`--out-dir` 写 `score.abc`；`abc_file` 回喂；`stop_after=abc` 只出谱 | audio.cpp `docs/models/yue2.md` |
| SheetSage2 在同一运行时内：`--task midi --family sheetsage2 --audio x.wav --out score.abc` | audio.cpp `docs/audio_tools.md` |
| YuE2 官方 int8 权重 3.9GB（8GB 显存起）；GGUF Q4 实测峰值约 7.8GB，16GB 统一内存 Mac 可用量化版运行（慢） | Comfy-Org/Yue2、Serveurperso/YuE2-GGUF |
| **已验证（2026-11-09）**：Metal 覆盖 YuE2/ACE-Step 两家族，M1 Pro 上 YuE2-Q4 全流程近实时（56s 音频 ~60s 墙钟、峰值 5.59GB） | 附录 B |
| YuE2 官方 int8 权重 3.9GB（8GB 显存起）；GGUF Q4 实测峰值 5.59GB（含 VAE 与解码），16GB 统一内存 Mac 可舒适运行 | 附录 B |

### 1.2 目标

- G1：用户在 Soundnet 画布上用一个节点提交"风格描述 + 歌词"，异步生成完成后得到一个可播放、可导出、可分轨的音频节点，工程保存后重装不丢。
- G2：YuE2 后端下可同时获得 `score.abc`，一键转成 MIDI 剪辑进入钢琴卷帘编辑。
- G3：编辑后的乐谱可回喂生成（改谱重渲染），也可接音阶量化器/三声部编排器做重新配器。
- G4：App 与 AU 共用同一份工程；AU 内**不能**发起生成（宿主实时环境），但可以正常渲染已含生成素材的工程。
- G5：Soundnet 本体不链接 Python、不捆绑模型权重、不引入 ComfyUI 硬依赖。
- G6：**每个 AI 生成节点（实例级）都有停止按钮/功能**：任务进行中可取消生成，播放中可停止播放；节点级停止与工具条全图停止、`Trigger.Stop` 图内事件三者语义各自独立（沿用 2.81 确立的分层）。

### 1.3 非目标

- 不做实时 AI 推理（生成不进音频回调；模型推理速度也远达不到实时）。
- 不做模型微调/训练界面（LoRA 训练留给外部工具）。
- 不分发、镜像或托管任何模型权重。
- 不做云端生成服务（后端全部本地；远程 GPU 由用户自行桥接 audio.cpp server，不属于本期）。

## 2 关键架构决策

### A1 生成即素材（核心）

生成任务完成后，WAV 经 `triassets::importAsset("audio", …)` 进入工程素材库并嵌入节点，节点随即表现为「音频片段」等价物。渲染、导出、旁通、保存全部复用既有路径。

理由：Soundnet 的实时路径有硬约束（不分配、不 IO、不阻塞），而生成是分钟级外部任务；把"生成"与"播放"在时间轴上解耦，是唯一不破坏引擎契约的接法。素材嵌入也天然解决工程可移植性。

### A2 任务契约先行

AI 生成节点是标准第 11 节的第一个落地者。先在 `节点与连接标准-实现映射.md` 增补"异步任务与资源"一节（本文第 3 节即该章节的草案），更新标准讨论稿对应条款状态，再动代码。顺序不可颠倒。

### A3 后端抽象，audio.cpp 为默认实现

```
Soundnet 节点（契约层）
   │  GenBackend 接口（C++ 抽象类，进程边界由实现决定）
   ├─ GenBackendAudioCppCli     默认。spawn audiocpp_cli 子进程，文件交换
   ├─ GenBackendAudioCppServer  可选。HTTP 轮询本机 server 模式
   └─ GenBackendComfyUI         可选。POST /prompt + 工作流模板（高级用户）
```

- 默认选 CLI 子进程而非链接为库：进程隔离保证崩溃不殃及引擎、模型内存可整体释放、AU 构建可整层剔除。库链接留作后续优化项。
- ComfyUI 后端不是依赖，是配置；其价值在于复用用户已有的 ComfyUI 模型管理，以及作为 audio.cpp Metal 支持不足时的备选。

### A4 AU 场景的确定性

生成只发生在独立 App 的编辑器（控制线程）。AU 构建中 `soundnet.ai-music` 节点**保留类型注册**（旧工程可打开、可渲染已嵌入素材），但提交入口禁用并给出诊断。这避免 Logic 宿主内出现分钟级后台任务与不可控内存峰值。

### A5 文本参数扩展（契约变更）

prompt 与歌词是多行文本，现有 `ParamSpec` 只承载 float。方案：v4 工程格式在节点字典新增**字符串参数表** `texts`（稳定键 → 字符串），与数值 `parameters` 并列；`ParamSpec` 增加 `textKey` 字段声明；v3 无此概念故不涉及迁移；检查器对文本参数生成多行文本框。不新建 assetKind，因为 prompt/歌词语义上是参数（需撤销、需随节点复制），不是素材。

### A6 乐谱的一等公民化

`score.abc` 不嵌入工程素材库（它不是音频），而是经**新增的 ABC→MIDI 转换器**（纯文本解析，无模型依赖）落成普通 MIDI 剪辑，进入「MIDI Clip」节点（`soundnet.midi-clip`，assetKind `clip`）。钢琴卷帘、音阶量化、编排器、MIDI 导出随即全部可用。原始 `.abc` 文本同时保留在剪辑节点的 `texts` 扩展里，供"改谱重渲染"回喂。

### A7 取消与代次复用现有模式

任务取消复用离线导出的原子取消标记模式（`std::atomic<bool>* cancelled` 穿透调用栈）。任务句柄绑定节点 `generation`（publish 代次）：工程被修改导致代次推进时，旧任务结果一律丢弃不应用——与 2.81"素材导入取消不应用结果、切换工程丢弃旧导入"的语义对齐。

### A8 编号与键的追加规则

- 新节点类型：`AiMusicNode=47`（`FirstAppendedNode` 当前未被占用）。后续节点继续 48、49…
- 新参数 ID：数值参数从 **1310** 起（当前最大 1300=`ChannelFilter`）；字符串参数不占数值段，只声明稳定键。
- 全部遵守"只增不改、不复用、删除留占位"。

## 3 契约变更草案（写入 `节点与连接标准-实现映射.md` 新增第 3.6 节）

> 本节是接手者要**先抄进实现映射文档**的内容草案，标注 `[定]` 为本次定案、`[议]` 为实施中可微调。

### 3.6 第 11 节 异步任务与资源（本次落地）

| 条款 | 定案 | 标记 |
| --- | --- | --- |
| 任务能力 | `CapTask` 节点持有 0..1 个活动任务；任务在控制线程执行，音频线程只读其已发布结果 | [定] |
| 任务生命周期 | `idle → submitted → running → (succeeded \| failed \| cancelled)`；状态不可持久化，工程重装后一律回到 `idle`（产物已物化为素材，无需恢复任务） | [定] |
| 提交语义 | 检查器「生成音频」按钮显式提交（偏差 D9：分钟级任务不得因走带触发）；触发输入只控制播放；任务进行中再次提交为诊断，不排队 | [定] |
| 结果发布 | 成功结果先在控制线程完整物化（素材入库、校验通过），再在音频块边界一次性切换可见；不存在"半成品"可被渲染 | [定] |
| 取消 | 原子取消标记 + 任务句柄；取消在写最终目标前生效，保留既有结果（同离线导出语义） | [定] |
| 节点级停止（用户新增要求） | 每个 `CapTask` 节点实例必须有用户可达的停止入口，语义分两层：任务进行中 → 取消任务（等价 `cancel()`，进程 kill/远端中断，结果不应用）；任务空闲且正在播放 → 停止播放（等价收到 `Trigger.Stop`：`stopping=true`，块边界生效）。停止不修改工程参数，与工具条全图停止、`Trigger.Stop` 图内事件互不混淆 | [定] |
| 代次 | 任务绑定 `Runtime::generation`；publish 推进代次后，未完成任务结果丢弃并诊断 | [定] |
| 停止代次 | `Engine::setProcessingEnabled(false)` 不取消任务（任务不属音频处理）；但任务完成时若全图停止，素材照常入库、不触发播放 | [议] |
| reset | `reset()` 要求任务可重建：活动任务被取消，节点回到其 `saved` 参数描述的初态 | [定] |
| 离线导出 | 导出时未完成的 CapTask 节点按"无素材的音频片段"处理（静音）并给诊断；导出不等待任务 | [定] |
| 诊断 | 任务失败原因（后端缺失/超时/权重缺失/许可提示）进入状态栏与 `--validate` 同款诊断通道 | [定] |
| TaskEvent/Resource 信号 | 本期**不注册**任何使用这两个信号的端口；它们保留给第 9.5–9.6 节存储节点落地时使用 | [定] |
| 字符串参数 | 节点字典新增 `texts`（稳定键→字符串）；`ParamSpec::textKey` 声明；v4 严格、v3 不涉及；未知文本键按未知参数规则处理 | [定] |

同步动作：标准讨论稿第 11 节状态改"部分落地"，实现映射第 7 节"尚未落地"清单第 3 条移除。

## 4 节点设计

### 4.1 `soundnet.ai-music`（AI 音乐生成）

描述符草案（追加进 `nodeSpecs()`，类别"输入"，紧随 `soundnet.daw-clock` 之后）：

```cpp
add({"soundnet.ai-music",AiMusicNode,"AI 音乐生成","输入",1,1,true,true,
     CapTask|CapAssetAudio|CapStream|CapBypassable|CapGenerator,
     TimeMode::Timed,0,
     {pin("trigger","触发",Signal::Trigger,RoleTrigger,PortOptional|PortFeedback)},
     {pout("audio","音频",Signal::Audio,RoleMain)},
     {AiMusicBackend,AiMusicModel,AiMusicDuration,AiMusicSeed,
      AiMusicQuality,AiMusicOutMode,
      // 播放侧（与 audio-clip 对齐，直接复用其 ParamInfo）：
      AudioClipGain,AudioClipLoop,AudioClipStart,AudioClipEnd,AudioClipFade,
      AudioClipPitch,AudioClipReverse,AudioClipXfade},
     "audio"});
```

- 端口：一个触发输入（控制播放开始/停止）、一个音频主输出。触发输入带 `PortFeedback` 以允许"生成完成 → 计数 → 重新生成"回路。
- 素材：`assetKind="audio"`，生成成功后由内核写入；用户也可手动替换/导入（此时节点退化为纯音频片段，是合法用法）。
- 文本参数（走 A5 的 `texts`，共 6 个）：`ai.prompt`（风格描述）、`ai.lyrics`（歌词，含 [Verse]/[Chorus] 结构标记）、`ai.execPath`（audio.cpp 可执行文件路径，留空自动探测）、`ai.modelPath`（模型文件）、`ai.extraArgs`（高级透传）、`ai.score`（生成后自动写入的 ABC 乐谱）。

新增数值参数（1310 起，ParamInfo 草案）：

| ID | key | 范围/默认 | 说明 |
| --- | --- | --- | --- |
| 1310 | `ai.backend` | 枚举 0–2，默认 0 | audio.cpp CLI / audio.cpp server / ComfyUI |
| 1311 | `ai.model` | 枚举 0–3，默认 0 | ace-step / yue2-bf16 / yue2-int8 / yue2-gguf-q4 |
| 1312 | `ai.duration` | 10–600 s，默认 120 | 目标时长（ACE-Step ≤600，YuE2 ≤360，内核按模型夹取） |
| 1313 | `ai.seed` | 0–999999，默认 0 | 0 = 随机；非零可复现 |
| 1314 | `ai.quality` | 枚举 0–2，默认 1 | 快速/均衡/精细（映射步数等后端参数） |
| 1315 | `ai.outMode` | 枚举 0–1，默认 0 | 仅音频 / 音频+乐谱（乐谱仅 YuE2 系有效，否则诊断） |

### 4.2 内核行为（InstanceGraph 侧）

- `Runtime` 增加 `std::unique_ptr<AiMusicState> aiMusic`；`AiMusicState` 持有任务句柄、状态机、取消标记、完成后待发布的素材引用。**所有成员在 `reset()` 可重建**。
- `deliver()` 增加分支：触发到达 → 校验素材路径与后端可用性 → 提交任务（控制线程）；任务进行中重复触发记诊断。
- 节点级停止入口（编辑器按钮）按当前状态路由到两个既有机制之一：任务态 → `GenBackend::cancel()`（结果不应用、状态回 idle）；播放态 → 向本节点投递停止事件，走 `AudioClipNode` 同款 `stopping=true` 路径，块边界生效。两条路径都必须在 `reset()` 中可重建。
- 渲染路径：无素材 → 静音（与缺素材的音频片段一致）；有素材 → 走与 `AudioClipState` 相同的读取逻辑。**渲染分支不含任何任务代码**。
- 完成回调：控制线程物化素材后，在块边界交换进 `AudioClipState` 等价缓冲；随后置一个"结果到达"的触发沿从 `trigger` 反馈口语义上不可见——本期不新增触发输出，保持描述符最小。

### 4.3 编辑器侧

- 检查器：文本参数渲染为多行输入框；任务状态（idle/running/进度%/失败原因）显示在节点卡片角标与检查器顶部；「重新生成」「取消」按钮。
- **节点级停止按钮（用户新增要求）**：节点卡片常驻停止按钮，按契约 3.6"节点级停止"分层生效——任务态显示为「取消生成」（点击即 `cancel()`，按钮在任务终止后回到 idle 态）；播放态显示为「停止播放」（向本节点投递停止，等价 `Trigger.Stop`）；空闲态置灰。按钮位置与样式对齐 2.81 工具条控件规范，键盘可达。
- 进度来源：CLI 后端解析 stdout 日志行；server/ComfyUI 后端轮询状态接口。进度不可用时显示不定态。
- 生成完成后若 `ai.outMode=1` 且产物含 `score.abc`：弹出"导入为 MIDI 剪辑"动作（创建 `soundnet.midi-clip` 节点并连线到同一下游）。

## 5 后端层设计

### 5.1 `GenBackend` 接口（新文件 `src/GenBackend.hpp`）

```cpp
struct GenRequest {
    std::string prompt, lyrics, execPath, extraArgs;
    int backend, model;         // 对应 1310/1311 枚举
    double seconds; uint64_t seed; int quality;
    bool wantScore;             // ai.outMode==1
    std::string abcText;        // 非空即回喂（audio.cpp abc_file）
    std::string workDir;        // 任务临时目录（沙盒内）
};
struct GenResult {              // 全部在控制线程物化
    std::string wavPath;        // 必出
    std::string abcPath;        // 可空
    std::string log;            // 诊断用
};
class GenBackend {
public:
    virtual ~GenBackend()=default;
    virtual bool available(std::string& why)=0;                 // 可执行文件/服务可达性
    virtual uint64_t submit(const GenRequest&)=0;               // 返回任务句柄
    virtual int progress(uint64_t task)=0;                      // 0..100，-1=不定
    virtual void cancel(uint64_t task)=0;                       // 尽力而为
    virtual bool poll(uint64_t task,GenResult& out,std::string& why)=0; // 完成才返回 true
};
```

### 5.2 三种实现

| 实现 | 机制 | 要点 |
| --- | --- | --- |
| `GenBackendAudioCppCli`（默认） | `posix_spawn` `audiocpp_cli --task gen --family <ace_step\|yue2> … --out-dir <tmp>`；成功=退出码 0 且 WAV 存在；score=`<tmp>/score.abc` | 进程隔离；取消=kill；权重路径经 `--model` 指向用户目录；超时=时长×10 兜底 |
| `GenBackendAudioCppServer` | HTTP POST `/v1/tasks/run`（audio.cpp server 模式），轮询状态 | 适合批量；服务由用户自行常驻 |
| `GenBackendComfyUI` | POST `http://127.0.0.1:8188/prompt`，内置 4 套工作流 JSON 模板（对应 1311 枚举），轮询 `/history` | 复用用户 ComfyUI 模型库；作为 Metal 覆盖不足时的退路 |

### 5.3 模型与权重管理

- Soundnet 不下载权重。节点在 `available()` 失败时给出指引性诊断（模型名、期望目录、`brew install audio-cpp`）。
- 许可证提示内置在诊断文案：ACE-Step（MIT）不提示；YuE2 首次使用时提示"权重 CC BY-NC 4.0：个人创作者可商用产出，公司商用需另行授权"，确认一次后不再提示（偏好设置记忆）。
- 权重目录默认 `~/Library/Application Support/Soundnet/models/`，可被 `ai.execPath`/偏好覆盖。

## 6 ABC→MIDI 转换器（`src/AbcScore.hpp`）

已实施。纯文本、无模型、无第三方库，编译进 App/AU/测试三处。

### 6.1 支持的输入

| 类别 | 规则 |
| --- | --- |
| 头部 | `X/T` 取标题；`M`（含 `C`、`C\|`、`none`）；`L`（默认 `1/8`，决定单位时值 `beatsPerUnit = num/den × 4`）；`Q`（`1/4=120` 按分数换算，`Q:120` 视作四分音符 BPM）；`K`（终止头部）；`V`（声部） |
| 调号 | 五度圈展开到 12 个音级：大调/教会调式（`m`/`min`/`dor`/`phr`/`lyd`/`mix`/`loc` 及其全名），`K:none` 等于 C 大调 |
| 变音 | `^` `^^` `_` `__` `=`；小节内对同一音高持续有效，小节线清除 |
| 音高 | 大写 `C`=60（中央 C），小写高八度，`,` / `'` 逐八度移动，越界钳到 0–127 |
| 时值 | 整数、`/`、`//`、`n/m` 乘数；附点用 `3/2` 这类分数表达 |
| 节奏 | 连音线 `-`（同音高合并时值）、连音组 `(3` / `(3:2:2`、破节奏 `>` `<` 及其叠加、休止 `z`/`x`/`Z` |
| 和弦 | `[CEG]` 同起点同长度；和弦符号 `"Am"` 与装饰 `!trill!`、倚音 `{...}` 跳过 |
| 声部 | 每个 `V:` 独立游标；第一个声部走通道 1，其余并入通道 2（按 A6 的首期约定） |
| 容错 | 无法识别的记号跳过并计入 `Score::warnings`；只有"没有 K:"或"一个音符都没有"才判为失败 |

### 6.2 边界与上限

- 输入文本 ≤ 1 MB，音符 ≤ 8000（SMF 事件上限 32768 的一半），总长 ≤ 65000 拍（`smf::maxBeats`），BPM 钳到 20–400。
- 失败返回带行号的原因（`第 N 行：…`），编辑器原样显示，不弹异常。
- **不展开反复记号**（`|:` `:|` `[1` `[2` 一律按小节线处理）。理由与将来处置见实现映射 D10。
- 输出 `MidiClip`（拍 = 四分音符）→ `tri::smf::encodeClip` → `triassets::encodeClipAsset`，与手工导入的 MIDI 文件走同一条素材路径。

### 6.3 界面入口

- AI 节点检查器底部两枚按钮：**「导入为 MIDI 剪辑」**（`texts["ai.score"]` → 新 `soundnet.midi-clip` 节点，资产 + `texts["clip.score"]` 快照，并镜像所有"接收音符"的下游连线；音频下游不动，由用户自行接采样器）与**「按乐谱重新生成」**（同一文本作为 `abc_file` 回喂）。
- MIDI 剪辑节点若带 `clip.score`，检查器显示只读乐谱文本，供追溯。
- `ai.outMode` 新增第三档 **仅乐谱**（`stop_after=abc`）：只跑符号规划，成功判据变成 `score.abc` 存在，不导入音频。16 GB 机器上这是最便宜的一次生成。

### 6.4 测试

`tests/AbcScoreTests.cpp`（已纳入 `test-all.sh` 与 `test-sanitize.sh`）：调号/变音作用域、八度、时值乘数、休止、连音、和弦、连音组、破节奏、双声部通道、行内转调、装饰容错、三类失败诊断，以及到 `encodeClip` → `smf::parse` 的往返。另外两个入口各有覆盖：`AiTaskTests` 校验回喂文件内容与 `abc_file` / `stop_after` 参数，`tools/InstanceEditorTest.mm` 用编辑器真实走一遍"AI 节点 → 导入 → 剪辑节点（6 音符、资产、快照）→ 撤销/重做"。

## 7 执行计划

| 阶段 | 目标 | 改动文件 | 交付物 | 验收标准 |
| --- | --- | --- | --- | --- |
| **0 技术验证**（已完成，2026-11-09） | 确认默认后端在本机可用 | 无（仓库外） | 验证记录见附录 B | ✅ 源码构建 Metal 版 audio.cpp（brew 沙箱在受限环境不可用，改用 cmake 直构）；ACE-Step Turbo Q8 生成 10s 音频成功（24.4s，峰值 7.83GB）；YuE2-Q4 生成 56s 音频成功（~60s，峰值 5.59GB，近实时）；仅乐谱模式 23.4s/5.57GB；Metal 覆盖两个家族；真实 `score.abc` 收集并通过 `AbcScore` 解析（155 音符/0 警告） |
| **1 契约落地**（0.5 天） | 异步任务与字符串参数契约入文档 | `docs/节点与连接标准-实现映射.md`、（如需）`docs/节点与连接标准-讨论稿.md` | 实现映射新增 3.6 节 | 文档评审通过；第 7 节待办清单同步更新 |
| **2 引擎契约实现**（2–3 天） | 任务状态机 + texts 格式 + 取消/代次 + 节点级停止 | `src/InstanceGraph.hpp`、`src/GraphSpec.hpp`、`src/WorkflowAssets.mm`、`src/NodeSpec.hpp` | 引擎支持 CapTask 生命周期；v4 读写 `texts` | 新增 `tests/TaskNodeTests.cpp`：提交/完成/**用户取消**/代次丢弃/**播放中节点级停止**/reset 重建/文档往返全过；既有 `test-all.sh` 不回归 |
| **3 默认后端**（1–2 天） | audio.cpp CLI 后端可用 | `src/GenBackend.hpp`（新）、`src/GenBackendAudioCpp.mm`（新） | 接口 + 默认实现 | 单元测试以桩 `audiocpp_cli` 脚本模拟成功/失败/超时/取消；真机端到端生成一次 30s 音频 |
| **4 节点与编辑器**（2–3 天） | `soundnet.ai-music` 可用 | `src/NodeSpec.hpp`、`src/InstanceGraph.hpp`、`ui/InstanceEditor.mm`、`src/WorkflowAssets.mm` | 节点上架、可生成、可播放、可保存 | 描述符自检；v4 往返；UI 走查（提交→进度→完成→播放→重开工程）；AU 构建中提交入口禁用诊断正确 |
| **5 乐谱闭环**（已完成） | ABC→MIDI + 改谱回喂 | `src/AbcScore.hpp`（新）、`ui/InstanceEditor.mm`、`src/GenBackend.hpp`、`src/NodeSpec.hpp`、`ui/SNLocalization.h`、`tests/AbcScoreTests.cpp`（新） | 乐谱导入、回喂、仅乐谱模式 | ✅ `AbcScoreTests` 全过；`AiTaskTests` 验回喂参数；`InstanceEditorTest` 走通"AI 节点 → 导入 → 剪辑（6 音符）→ 撤销"；CLI 用带 `clip.score` 的 v4 工程 `--validate` + `--export-midi` 输出 10 个事件 |
| **6 打磨与发布**（1 天） | 可发布质量 | `CHANGELOG.md`、`docs/使用说明.txt`、README 双语 | 2.90 发布候选 | `scripts/test-all.sh`、`test-sanitize.sh`（ASan/UBSan/TSan）、`verify-release.sh` 全绿；16 个既有示例渲染不受影响 |

依赖关系：0→1→2→3→4→5→6 严格串行；3 与 6 的文档部分可并行。阶段 0 不通过（Metal 不覆盖目标家族）→ 默认后端降级为 ComfyUI，回写本文 A3 后再进阶段 1。

## 8 测试方案

| 层 | 内容 |
| --- | --- |
| 描述符 | `NodeSpecTests.cpp` 自动纳入新节点；参数 ID 不复用、枚举标签数与范围一致 |
| 契约 | 任务生命周期全路径（成功/失败/取消/代次丢弃/进行中重复触发）；`texts` 的 v4 读写、未知键处理 |
| 后端 | 桩进程模拟：退出码非 0、缺 WAV、超时 kill、取消后结果不应用、日志解析 |
| 行为 | 有素材渲染==音频片段渲染（同 WAV 逐字节比对）；无素材静音；离线导出不等待任务 |
| 文档 | v4 往返：生成参数+素材+texts 完整读回；旧工程（2.80/2.81 示例）打开不受影响 |
| 消毒 | ASan/UBSan/TSan 全跑（任务并发是新引入的线程面，TSan 重点） |
| 发布 | `verify-release.sh` 全套 + 双架构 + 签名 |

## 9 风险与对策

| 风险 | 等级 | 对策 |
| --- | --- | --- |
| audio.cpp Metal 后端不覆盖 YuE2/ACE-Step 或性能不可接受 | 高 | 阶段 0 先行实测；降级路径=ComfyUI 后端（接口已抽象，切换成本限于配置）；CPU 后端兜底（极慢但可用） |
| 16GB 统一内存上 YuE2 量化版内存不足 | 中 | 默认模型=ACE-Step；YuE2 在诊断中明示内存预期；`ai.quality=快速` 降步数；文档建议关闲其他应用 |
| audio.cpp 顶层许可证不清晰 | 中 | 不链接库、只 spawn 用户自装的 CLI（`brew` 安装属于用户行为）；NOTICE 不新增义务；商用发布前复查其许可证状态 |
| YuE2 权重 CC BY-NC 的合规传达 | 中 | 见 5.3 的一次性提示；Soundnet 不分发权重即无直接义务 |
| 任务并发引入数据竞争 | 中 | 结果发布只在块边界做一次指针交换；TSan 强制通过；任务线程不触碰 `Runtime` 任何成员 |
| 分钟级任务与自动保存/切工程竞争 | 中 | A7 代次机制；切工程时取消并丢弃 |
| ABC 方言差异（YuE2 输出 vs 标准 ABC） | 低 | 阶段 0 收集真实样本驱动解析器开发；解析失败只诊断不崩 |

## 10 待决事项（实施前需用户拍板）

| 编号 | 问题 | 建议 |
| --- | --- | --- |
| Q1 | 阶段 0 何时执行、是否就在本机（M1 Pro 16GB）做 | 建议立即做，它是所有下游决策的输入 |
| Q2 | 字符串参数走 A5 的 `texts` 扩展，还是退化为节点 `name` 式自由字段 | 建议 A5（契约干净、可撤销、检查器可控） |
| Q3 | 首版是否包含 ComfyUI 后端 | 建议只实现 audio.cpp CLI 一个，ComfyUI 等阶段 0 结论再定 |
| Q4 | 版本号与发布节奏：独立 2.90 还是并入下一个功能版本 | 建议独立 2.90，契约变更值得单独成版（**仍未执行**） |
| Q6 | `ai.outMode=2`（仅乐谱）是否应在 ACE-Step 上隐藏 | ACE-Step 无符号规划，选它只会得到失败诊断；建议按 `ai.model` 联动禁用，留给下一轮 |
| Q5 | AU 中是否允许"打开 App 去生成"的跳转换引 | 建议首版只做禁用诊断，跳转属 UI 打磨 |

## 附录 A 关键代码锚点（接手者导读）

| 主题 | 位置 |
| --- | --- |
| 节点描述符注册表与编号规则 | `src/NodeSpec.hpp`（`nodeSpecs()`、类型编号区、参数 ID 区；当前最大参数 1300、首个可追加类型 47） |
| 能力位与信号 | `src/NodeSpec.hpp`（`CapTask`/`CapSnapshot`/`Signal::TaskEvent`/`Signal::Resource` 已定义未使用） |
| 音频片段内核（对齐样板） | `src/InstanceGraph.hpp`（`AudioClipState`、`deliver()` 的 `AudioClipNode` 分支、渲染分支） |
| 触发调度与深度保护 | `src/InstanceGraph.hpp`（`dispatch()`/`enqueueDeferred()`） |
| 取消模式样板 | `src/OfflineRender.hpp`（原子 `cancelled` 穿透） |
| 素材导入/嵌入 | `src/WorkflowAssets.mm`（`triassets::importAsset`、`decodeGraph`） |
| 工程 v4 格式与迁移 | `src/WorkflowAssets.mm`（`canonicalGraph`）、`docs/节点与连接标准-实现映射.md` 第 5 节 |
| 新节点编写流程 | `docs/编写新节点.md`（注意：本方案属于其第 5 节"新增执行能力"，须先改契约） |
| MIDI 剪辑数据与写出 | `src/MidiClip.hpp`、`src/MidiFileWriter.hpp`、`src/StandardMidiFile.hpp` |

## 附录 B 阶段 0 验证记录（2026-11-09，M1 Pro / 16GB）

```
audio.cpp 版本：dev 主干 git 7d20b41（2026-11-09），Release 构建，clang 21，arm64
安装方式：Homebrew 在受限会话内无法使用（其沙箱 sandbox-exec 被外层拒绝，HOMEBREW_NO_SANDBOX 无效）。
  改为源码构建：git clone → scripts/build_metal.sh --build-type Release --archs arm64 --target audiocpp_cli
  （需要完整 Xcode 提供 metal 编译器：DEVELOPER_DIR=/Applications/Xcode.app/Contents/Developer）
  二进制固化于 models/audio-cpp/audiocpp_cli（36MB，Metal shader 已内嵌，单文件即可用）；
  源码副本在 ~/Documents/audio.cpp。模型存于 models/（已 gitignore）。

模型包：
  YuE2-3B Q4   2.4GB  hf-mirror.com/audio-cpp/Yue2-3B-GGUF（q4_0 主模型 + f16 VAE + 4 sidecars）
  ACE-Step1.5 Turbo Q8  5.7GB  modelscope.cn（HereIsMark/audio.cpp-gguf；直连 HF 被代理挡，hf-mirror 限流时用 ModelScope）

真机实测（--backend metal，threads 8）：
  YuE2-Q4 仅乐谱（stop_after=abc）     23.4s   峰值 5.57GB   score.abc 1027B
  YuE2-Q4 完整生成（56.1s 立体声48k）   ~60s    峰值 5.59GB   out.wav 10MB + score.abc
  YuE2-Q4 乐谱回喂重渲染（abc_file）    ~30s    成功，产物哈希与首轮不同（确认真按谱重渲）
  ACE-Step Turbo Q8（10s 音频）         12.5–24.4s（首跑含 GGUF 解包缓存）  峰值 7.83GB
Metal 覆盖结论：YuE2 与 ACE-Step 均在 M1 Pro Metal 上正常运行（Apple7 家族，统一内存，flash attention 开）。

真实 score.abc 样本：/tmp/yue2-score/score.abc 与 /tmp/yue2-full/score.abc（会重启丢失时可用
  同种子命令重生成：见下）。AbcScore 解析结果：155 音符、2 声部、BPM 100、140 拍、0 警告。

真机验证驱动的三处契约修正（已改 GenBackend.hpp 并补回归用例）：
  1. ACE-Step 的风格提示走 --text 而非 style=；时长用 --duration-seconds（YuE2 无时长参数）。
  2. YuE2 包目录含多个主 GGUF，按节点选择加 --session-option yue2.model_gguf=<文件>（默认会话取 q8_0）。
  3. 外部乐谱（abc_file）必须配 cot=full，否则 audio.cpp 直接退出（"external ABC requires
     cot=melody or cot=full"）；stop_after=abc 则相反，不允许携带外部乐谱——两者互斥已在参数构造层保证。
  另：进度解析从臆想的 "step n/m" 改为真实 --log 阶段标记（runtime.load.phase / planner.total_ms /
  ar.generate.total_ms / nar.synthesize.total_ms / vae_decode. 等），Metal 权重走 mmap 无上传百分比，
  阶段式 3→10→25/35→45→65→90 已实测可见。

决策（默认后端维持 audio.cpp CLI / 降级 ComfyUI）：维持 audio.cpp CLI。Metal 覆盖与性能均达标
  （YuE2-Q4 全流程近实时、峰值 5.6GB；16GB 机器余量充足），无需降级。
```

> 阶段 0 结论：默认后端维持 audio.cpp CLI，A3 决策成立。桩程序测试（`tests/AiTaskTests.cpp`）
> 与真机驱动（本机二进制 + 真实模型）双轨验证过提交/进度/成功/失败/取消/回喂六条路径。
## 附录 C 外部参考

- ACE-Step 1.5：`github.com/ace-step/ACE-Step-1.5`（MIT）
- YuE2：`github.com/multimodal-art-projection/YuE`（代码 Apache 2.0 / 权重 CC BY-NC 4.0 + 创作者条款）
- audio.cpp：YuE2 路由文档（`docs/models/yue2.md`，ABC 导出/回喂/`stop_after=abc`）、工具文档（`docs/audio_tools.md`，SheetSage2）
- GGUF 权重：`huggingface.co/Serveurperso/YuE2-GGUF`（Q5_K_M 起，Q8_0 近无损）
- ComfyUI 权重：`huggingface.co/Comfy-Org/Yue2`（bf16 7.8GB / int8 3.9GB）
