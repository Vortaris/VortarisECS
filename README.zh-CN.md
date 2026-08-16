[English](README.md) | **简体中文**

# VortarisECS

一个为 **Godot 4.7** 打造的现代数据导向 ECS（实体-组件-系统）框架，用 C++ 编写，以 GDExtension（godot-cpp）形式提供。易用、健壮、可扩展，并支持网络同步。

设计参考：[GECS](https://github.com/BlockBreaker-Studios/GECS) —— 其 archetype SoA 存储、分层实体 id、延迟命令缓冲、查询构建器、观察者与分层网络同步模式均移植到了原生 C++。

> **API 权威参考**：[`doc_classes/*.xml`](doc_classes/) 中的类参考是 GDScript API 的事实标准（会编译进编辑器的 F1 帮助）。若本 README 与 `doc_classes/` 不一致，以 `doc_classes/` 为准。

## 特性

- **纯 C++ 数据组件** —— 组件是平凡可拷贝的结构体，存储在缓存友好的 archetype 列（SoA）中。声明式 schema 宏把字段暴露给 GDScript 访问和二进制序列化。
- **轻量实体句柄** —— 64 位分层 id（槽位 + 代数）；O(1) 过期句柄检测；支持网络预分配 id。
- **快速查询** —— archetype 成员判定 O(1)；增量维护的查询缓存；`.changed()` 变更检测。
- **延迟命令缓冲** —— 批量结构性变更，每个实体只做一次 archetype 迁移、每次 flush 只失效一次缓存。
- **C++ 优先的系统** —— 类型化 `world.for_each<Position, Velocity>` 热路径零 Variant 开销；分组调度支持依赖排序、逐系统定时器与 flush 模式；GDScript 系统通过 `_script_process`。
- **观察者 / 事件** —— ADDED / REMOVED / CHANGED / MATCHED / UNMATCHED / 自定义事件，支持重入安全。
- **确定性二进制序列化** —— 小端、定宽、逐字节一致（byte-identical）的快照。
- **可插拔网络同步** —— `VECSSyncStrategy` 抽象 + 默认的服务器权威快照复制（脏检查增量 + 定期对账 + 反幽灵）。传输层是 Godot 的 MultiplayerAPI（RPC），另附进程内直连测试传输。

## 0.3.0 新特性

- **分层项目设置** —— 设置从扁平的 `vortarisecs/verbose` 重组为 `vortarisecs/<分类>/<名称>`，在“项目设置 > VortarisECS”下分组显示：`vortarisecs/general/verbose`（由 0.3.0 的扁平路径迁移而来，仍兼容回退）、`vortarisecs/general/auto_shutdown_on_exit`、`vortarisecs/general/max_snapshot_entities`、`vortarisecs/debug/auto_refresh_interval`、`vortarisecs/network/default_sync_priority`、`vortarisecs/observer/default_throttle_tick` 与 `vortarisecs/serialization/compact_json`。默认值仅在缺失时写入，绝不覆盖用户已有设置。
- **原先硬编码的默认值变为设置项** —— 新组件字段的同步档位、observer 的 CHANGED 节流、编辑器远程监控的实体上限 / 自动刷新间隔、退出时的 `shutdown()` 清理，以及快照 JSON 字符串是否紧凑输出。
- **`VECSWorld.serialize_snapshot_json_string()`** —— 以 String 形式导出 JSON 存档，遵循 `vortarisecs/serialization/compact_json`（紧凑 vs 格式化）。
- **`VECSWorld.is_verbose()`** 现读取持久化设置（含旧路径回退），而非初始化时的快照缓存。

## 0.3.0 新特性

- **运行时远程监控 GUI** —— 游戏运行时，编辑器调试器底部面板新增 **“ECS”** 选项卡，实时显示**运行中**游戏的 ECS 世界：Entities（实体 id → 组件 → 字段）、Components（组件注册表及字段元数据）、Systems（名称/分组/启用状态）与 Stats（世界统计）。原理同 Godot 场景树的 Remote 模式：编辑器经 `EngineDebugger` 发送 `vecs:req_snapshot`，游戏用新的 `VECSWorld.get_snapshot_data()` 回发 `vecs:snapshot`。提供“刷新”按钮与可选 ~1Hz 自动刷新；快照仅按需发送。
- **`VECSWorld.get_snapshot_data()`** —— 返回世界的可 JSON 化 Dictionary（`stats` / `components` / `systems` / `entities`）；供编辑器选项卡使用。实体表按 `vortarisecs/general/max_snapshot_entities`（默认 500）截断并带 `truncated` / `entity_total` 标志，避免 10 万实体的世界每次刷新都序列化出 MB 级数据；经 `serialize_snapshot_json()` 写存档时永不截断。
- 原有检查器 dock（`editor/ecs_inspector_dock.gd`）保留，仍用于查看编辑器侧世界；**运行中**游戏请使用新的调试器选项卡。

## 0.2.1 新特性

- **运行时调试 overlay** —— 编辑器检查器 dock 因进程隔离只能看到编辑器进程的世界（为空），因此新增游戏进程内的 overlay（`addons/vortarisecs/ecs_overlay.gd` + `ecs_overlay.tscn`），实时显示**运行中**世界的统计、实体→组件→字段浏览树，以及 JSON 快照导出/导入。默认关闭，用 `--vortaris-ecs-overlay on` 或游戏内按 **F2** 开关。
- **headless CLI** —— `--vortaris-ecs-stats`、`--vortaris-ecs-snapshot <path>`、`--vortaris-ecs-overlay on|off`（在世界构建完成后解析，报告真实游戏世界）。输出带 `[vortarisecs]` 前缀，便于 AI/脚本读取。
- **分级日志** —— 新增 `vortarisecs/verbose` 项目设置；debug 构建的普通运行日志 + verbose 详细追踪（实体生灭、组件写入、observer 派发、网络包细节、query 执行），release 构建完全编译为空。新 API：`VECSWorld.set_verbose(bool)` / `VECSWorld.is_verbose()`。
- **AI 调试文档** —— 新增 [`docs/AI_DEBUGGING.md`](docs/AI_DEBUGGING.md)：MCP `run_script` 插件 API 示例、CLI 参数表与退出码、编辑器 dock 隔离警告。完整变更日志见 [`RELEASE_NOTES.md`](RELEASE_NOTES.md)。

## 0.2.0 新特性

- **实体查找** —— `world.entity(id)` / `world.has_entity(id)`；`get_component` 在组件未挂载时返回空句柄。
- **字段默认值** —— `get_field` / `getf` 增加 `default` 参数，组件/字段缺失时返回默认值。
- **查询易用性** —— `find_by_components(comps)`、`where(谓词)`、`order_by(comp, field)` 与 `order_by_id()`；默认顺序为 archetype 创建序 + 行序。
- **数组字段** —— `VECSComponent.get_field_count` / `get_array_element` / `set_array_element`，以及 `VECSComponentType.get_field_count` / `get_field_type`（数组返回 `"Array:<type>"`）。
- **id 映射** —— `spawn_from_data_mapped` / `deserialize_snapshot_json_mapped` 返回 `{source_id_or_index: new_id}`，`remap_reference` 改写跨实体引用；`spawn_from_data` 条目支持 `"parent"` 自动父子。
- **实体池化** —— `create_entity_pooled` / `destroy_entity_pooled` / `pool_size` 回收 id 而不 bump 代数（过期句柄保持有效，文档注明取舍）。
- **跨世界拷贝/合并** —— `copy_entity_to` / `merge_world` 返回 id 映射；把世界合并到自身即克隆。
- **事件总线** —— `emit_event` 返回接收者数量；`subscribe_event` / `unsubscribe_event`；按值比较的 `on_field_changed(comp, field, callable)` + `off`。
- **观察者过滤** —— 字段级 CHANGED 订阅（`set_fields`）与变更时钟节流（`set_throttle_tick`）。
- **网络加固** —— 写入前先校验包（截断 / 未知 schema / id 冲突的包被丢弃，无部分状态）；`sync_priority` 现可节流增量发送（REALTIME / HIGH 20 Hz / MEDIUM 10 Hz / LOW 2 Hz）。
- **ChangeView 优化** —— `take()` 通过逐列 max-version 快路径跳过未变化的 archetype，并从世界写日志增量收集（结果一致，快得多）。
- **StringFixed** —— 写入按固定容量在 UTF-8 码点边界截断（只保留完整字符）并告警；`count == 0` 存储空字符串。
- **健壮性** —— `shutdown()` 重置瞬态状态（延迟操作、变更基线、观察者派发、系统调度器），世界可复用；变更时钟扩为 64 位；实体代数回绕守卫；干净退出，消除全部退出期警告/泄漏。
- **工具** —— `get_debug_stats()`、`VECSQueryBuilder.get_last_execution_time_usec()`，以及编辑器检查器 Dock。

## 架构

```
GDScript 层（VECS 前缀类）                C++ 核心（namespace vortaris）
─────────────────────────────────────      ─────────────────────────────
VECSWorld（Node / "VECS" 单例）   ──────────► World
VECSEntity（RefCounted 句柄）     ──────────► Entity（64 位分层 id）
VECSComponent（字段访问器）       ──────────► Archetype（SoA 列）
VECSComponentType（schema 元数据） ──────────► ComponentRegistry / ComponentSchema
VECSQueryBuilder（链式）          ──────────► Query / QueryCache
VECSCommandBuffer                 ──────────► CommandBuffer（延迟操作）
VECSSystem（Node，虚方法）        ──────────► SystemScheduler（分组/拓扑）
VECSObserver（Node，事件钩子）    ──────────► ObserverDispatch
VECSWorld 快照方法                ──────────► BinaryBuffer / snapshot（内部）
VECSNetworkSync（Node，RPC）      ──────────► VECSSyncStrategy（可插拔）
                                                 └─ VECSSnapshotReplication（默认）
```

> 注：二进制/JSON 快照序列化都在 `VECSWorld` 上（`serialize_snapshot()` / `serialize_snapshot_json()` 及其 `deserialize_*` 对应方法），映射到内部的 `vortaris::BinaryBuffer` / snapshot 编解码器——**不存在** `VECSBinaryBuffer` 类。

## 构建（Windows / MSVC）

插件链接 **godot-cpp**（外部依赖）。先获取它（必须匹配 Godot 4.7）：

```bash
git clone -b 4.7 https://github.com/godotengine/godot-cpp.git godot-cpp
pip install scons
```

然后构建静态库与插件。把插件构建指向你的 checkout 用 `godot_cpp_path=<path-to-godot-cpp>`（或 `GODOT_CPP_PATH` 环境变量；SConstruct 也会探测常见兄弟目录）：

```bash
cd godot-cpp
scons platform=windows target=template_debug arch=x86_64
scons platform=windows target=template_release arch=x86_64   # 供 release 导出
cd <此仓库>
scons platform=windows target=template_debug arch=x86_64 build_library=False godot_cpp_path=<path-to-godot-cpp>
```

输出在 `demo/addons/vortarisecs/bin/vortarisecs.windows.*.dll`。第一次在 Godot 中打开 `demo/` 时，编辑器会生成 `.godot/extension_list.cfg`（注册扩展）。

运行 demo（功能 + 性能）：

```
godot --headless --path demo
godot --headless --path demo --script res://scripts/perf_test.gd
```

## 快速上手（GDScript）

一份简短的端到端教程见 [`docs/quickstart.md`](docs/quickstart.md)（可作为 `demo/scripts/quickstart.gd` 运行）。AI 代理调试运行中的游戏时，请参阅 [`docs/AI_DEBUGGING.md`](docs/AI_DEBUGGING.md)（MCP `run_script` 示例、headless CLI 参数、runtime overlay）。**便捷 API** 用几行代码就能完成简单功能：

```gdscript
var world: VECSWorld = VECS.get_world()

world.register_component("Pos", [{"name": "x", "type": "F32"}, {"name": "y", "type": "F32"}])
world.register_component("Vel", [{"name": "x", "type": "F32"}])

var e := world.spawn({"Pos": {"x": 1.0, "y": 2.0}, "Vel": {"x": 0.5}})   # 一行创建实体

world.each(["Pos", "Vel"], func(ent: VECSEntity) -> void:                 # 迭代，不物化 Array
    ent.setf("Pos", "x", ent.getf("Pos", "x") + ent.getf("Vel", "x")))
```

完整 API 用更显式的控制做同样的事——组件访问器、链式查询构建器、命令缓冲，以及类型化 C++ 迭代：

```gdscript
var world: VECSWorld = VECS.get_world()

var e: VECSEntity = world.create_entity()
e.add_component("Position", {"x": 1.0, "y": 2.0, "z": 0.0})
e.add_component("Velocity", {"x": 0.5, "y": 0.0, "z": 0.0})

var pos: VECSComponent = e.get_component("Position")
pos.set_field("x", 3.0)                       # 标记该行已变更

var hits: Array = world.query() \
    .with_all(["Position", "Velocity"]) \
    .enabled() \
    .execute()
```

## 脚本定义组件与系统（无需 C++）

组件可以完全用脚本定义——不需要 C++ 结构体。框架根据字段声明计算内存布局，存进相同的 SoA 列；字段访问、确定性序列化与网络同步都走同一条 schema 反射流水线，因此脚本组件与 C++ 组件行为完全一致。

```gdscript
# 1) 注册 schema 组件（类型：Bool/I8..I64/U8..U64/F32/F64/
#    Vector2..4(i)/Color/Quaternion/Basis/Transform2D/3D/AABB/Rect2/Plane/
#    StringFixed/Blob；可选键：count、sync_priority、networked）
world.register_component("Health", [
    {"name": "amount", "type": "F32"},
    {"name": "max", "type": "F32", "sync_priority": 0},
])

# 2) 与 C++ 组件用法完全一致
var e: VECSEntity = world.create_entity()
e.add_component("Health", {"amount": 100.0, "max": 100.0})
var h: VECSComponent = e.get_component("Health")
h.set_field("amount", 75.0)

# 3) 用 GDScript 写系统：继承 VECSSystem、覆写 _script_process、
#    通过 get_world_node() 拿到世界
var sys = preload("res://scripts/script_system.gd").new()
sys.group = "scripts"
world.add_system(sys)
world.process(0.1, "scripts")
```

两种编写风格共存：脚本系统适合游戏逻辑与快速迭代，C++ 系统（下一节）适合性能敏感的热路径。

## C++ 系统（高性能路径）

组件与系统在 C++ 中定义（本项目中编译进同一个 dll）：

```cpp
// components.h
struct Position { float x = 0, y = 0, z = 0; };
VECS_REGISTER_COMPONENT(Position,
    VECS_FIELD(Position, x, F32),
    VECS_FIELD(Position, y, F32),
    VECS_FIELD(Position, z, F32));

// systems.h
class MoveSystem : public VECSSystem {
    GDCLASS(MoveSystem, VECSSystem)
public:
    void _tick(vortaris::World &w, double delta) override {
        const float dt = float(delta);
        w.for_each<Position, Velocity>([&](vortaris::Entity e, Position &pos, Velocity &vel) {
            pos.x += vel.x * dt;
            pos.y += vel.y * dt;
            pos.z += vel.z * dt;
        });
    }
};
```

## 缓存视图与变更感知迭代

`for_each` 每次调用都会重建查询（开销很小——QueryCache 是增量式的），但想要显式数据契约的系统可以持有 `View`：在 `_setup` 里编译一次查询，之后每帧复用。`ChangeView` 更进一步：它钉住一个基线，`take()` 只返回**自上次 take 以来被写入过**的所关注组件——非常适合稀疏、事件驱动的系统（沙子下落、AI 触发器），不必重新扫描全集。

```cpp
class GravitySystem : public VECSSystem {
    GDCLASS(GravitySystem, VECSSystem)
public:
    void _setup(vortaris::World &w) override {
        view_    = w.view<Position, Velocity>();  // 编译一次
        changes_ = w.changes<GravityBlock>();     // 变更感知
    }
    void _tick(vortaris::World &w, double dt) override {
        view_.each([](vortaris::Entity e, Position &p, Velocity &v) { /* ... */ });
        for (vortaris::Entity e : changes_.take()) { /* 只处理变更行 */ }
    }
private:
    vortaris::View<Position, Velocity> view_;
    vortaris::ChangeView<GravityBlock> changes_;
};
```

脚本侧的等价物是**活跃集**：事件/观察者给实体加一个标记组件（如 `Falling`），系统只查询携带该标记的实体。完整沙子下落示例见 `demo/scripts/falling_system.gd` + `sand_observer.gd`。

## 迭代契约

**迭代期间绝不**发起结构性变更（增删组件、销毁实体）——`for_each`、`View::each` 与 `VECSWorld::each` 正在遍历存活的 archetype 行，结构性变更会让行在遍历器脚下移动。把它们延迟到命令缓冲（`world.commands()`），或在循环之外做：

```cpp
w.for_each<Position>([&](vortaris::Entity e, Position &pos) {
    // 读取/写入组件值没问题……
});
// ……但循环内增删组件、销毁实体是不允许的。请延迟：
// w.commands().add_component(...) 然后统一 flush。
```

框架会强制执行：迭代期间发起结构性变更会被**报错拒绝**，而不是静默破坏迭代（过去会导致随机跳过 / 读到过期行）。

## JSON 存档与数据表

与 Godot 自带的 `JSON` 类深度集成——传入/传出普通的 `Dictionary` / `Array` / `String`，用引擎负责 stringify/parse：

```gdscript
# 世界存档：序列化 → stringify → 写文件
var text: String = world.serialize_snapshot_json_string()  # 遵循 vortarisecs/serialization/compact_json
# ……或手动 stringify：JSON.stringify(world.serialize_snapshot_json(), "\t")
FileAccess.open("user://save.json", FileAccess.WRITE).store_string(text)

# 读档：读文件 → 直接把 JSON 字符串喂回去
var ok: bool = world.deserialize_snapshot_json(FileAccess.get_file_as_string("user://save.json"))
```

```gdscript
# 数据表（例如卡牌）：批量注册组件 schema、按数据表批量生成实体
world.register_components({
    "Card":   [{"name": "title", "type": "StringFixed", "count": 64}],
    "Effect": [{"name": "damage", "type": "F32"}, {"name": "kind", "type": "I32"}],
    "Cost":   [{"name": "mana", "type": "I32"}],
})
var deck: Array = world.spawn_from_data([
    {"components": {"Card": {"title": "火球术"}, "Effect": {"damage": 15.0}, "Cost": {"mana": 3}}},
    # ... 通常由 CSV→JSON 流水线产生
])
var exported: Array = world.entities_to_data()   # [{ "id", "components": {...} }, ...]
```

`deserialize_snapshot_json` 接受 `Dictionary`（已解析）或 JSON `String`；未知组件跳过并告警，实体 id 保留（preassigned），存档带版本号。脚本（schema-only）组件与 C++ 组件序列化完全一致。

## 网络同步

```gdscript
var server_ns: VECSNetworkSync = VECSNetworkSync.new()
server_ns.set_server(true)
server_ns.bind_world(world)              # 服务器世界

var client_ns: VECSNetworkSync = VECSNetworkSync.new()
client_ns.bind_world(client_world)       # 客户端世界
server_ns.set_direct_peer(client_ns)     # 测试传输（真实模式走 RPC）

server_ns.tick(delta)                    # 服务器每帧调用
```

组件自动联网：至少含一个联网组件（默认）的实体会被生成（spawn），其脏字段作为增量（delta）推送，被销毁的实体取消生成（despawn）。定期对账广播全量状态（反幽灵）。

## 性能（Windows x64，10 万实体）

| 操作 | 时间 |
|---|---|
| `for_each<Position, Velocity>`（C++） | **0.43 ms** |
| Query count | 0.02 ms |
| Create（经 GDScript API） | 219 ms |
| 快照序列化 / 反序列化 | 13 / 39 ms |

（本仓库机器实测基线；此前数字为 0.37 ms / 3.85 ms，这是移除迭代热路径中每行的 `is_alive` 查找之后的结果。）

## 目录结构

```
src/core/          纯 C++ ECS 核心（无 Godot 对象）
src/reflect/       组件 schema 宏 + Variant 转换
src/serialization/ 确定性二进制缓冲、组件编解码、快照
src/network/       VECSNetworkSync + 可插拔同步策略
src/gdscript/      GDScript 侧类（VECS 前缀）
src/demo/          编译进 dll 的示例组件/系统
demo/              Godot 项目（验收场景 + 性能测试）
```
