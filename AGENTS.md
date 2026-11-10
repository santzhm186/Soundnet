# Soundnet 开发入口

修改前阅读 [项目宪法](docs/项目宪法.md) 和 [节点开发指南](docs/编写新节点.md)。用户当前明确要求优先，不依据旧讨论稿恢复取消的设计。

- 当前平台合同 **3.0.0 / build 100，SDK 2，包 ABI 2，磁盘工程 v6**。App 与 AU 使用同一节点接口、公共服务和包加载器。
- Soundnet 提供画布、约束、通信和工程资源规则；节点独立开发/编译/发布。合规节点包刷新不重编译或重启整个平台；到达代码保留预算等明确限制时除外。
- 节点源码位于 `nodes/<slug>/`，只依赖本目录、`platform/` 和携带的算法依赖；禁止包含其他节点、`src/`、`ui/`、宿主或 `_legacy/`。`legacyAdapter` 不再允许。
- `Node.hpp` 声明描述符和工厂，`Kernel.hpp`/`Task.hpp` 实现行为，`Editor.hpp` 可选，`Tests.cpp` 独立测试。自动生成的注册/编辑器表用于测试发现，禁止手改。
- 独立包 `<nodeKey>.soundnode/` 含 `manifest.json`、`Node.dylib`、可选 `Resources/`，入口 `soundnet_node_v2`。SDK、ABI、clang/libc++ 标记及结构尺寸必须匹配；包修订与节点行为版本分开。
- 常规节点不得新增中央按类型分支。需要公共能力时先记录合同与影响，再一起实现和验证；已有授权覆盖时直接推进。
- 节点 key 带命名空间；类型与参数 ID 用 SDK 派生，重复/冲突必须报错。`nodeVersion/stateVersion` 精确匹配，不自动迁移私有状态。
- 完整工程固定时间码与项目设定各一个。没有独立节奏节点或全局 BPM，各相关节点使用自身节奏与公共时间基准。
- SDK 2 图内支持音频、MIDI、触发、时间码；`Control/Resource/TaskEvent` 图端口尚未实现。触发 `PortFeedback` 允许参与反馈；仅实际环内的允许反馈边延迟当前处理块长度并保留帧偏移，无环边同块传递。
- 工程根和保存规则归平台。v6 使用同名 `.assets` 与相对、自描述引用；历史素材使用 `ProjectResources.h` 的资源表/集合，不把绝对路径藏进文本。`project.folder` 为未保存默认位置/实际工程根。
- 音频线程只处理已准备状态和有界缓冲，不分配、读写文件/网络、等待任务或加阻塞锁。输入只读；每个实例独占可变状态，资源通过不可变 `KernelConfig.audio/clip/model` 提供。
- UI 走 `NodeEditorContext` 编辑、创建节点、试听及设备服务；后台任务走 `TaskService`，控制线程经有效归属与事务提交。工作线程不修改工程。
- 刷新整批校验后原子发布；旧代码和 Objective-C 类不热卸载。二进制镜像、包资源快照及注册目录修订各有 512 个的进程预算，达到预算须重启；加载后发现接口无效的镜像也计入预算。
- 修改前检查工作区。并行只动负责文件，公共接口变化及时同步，不覆盖未提交工作。
- 节点修改运行本目录与相关集成测试；平台修改运行 `scripts/test-all.sh` 并按影响运行消毒器/并发检查；发布运行 `scripts/verify-release.sh`。文档标准不代表发布验收已完成。
- 署名 **Santzhm186**，保留 README/NOTICE 中 hehongze88@gmail.com 的贡献致谢；自有代码 MIT，第三方许可独立保留。
