# 031 Mesh 导入输入快照

## 目标

封闭后台 Mesh 导入中的 TOCTOU 窗口：Worker 生成的 CPU Mesh、导入缓存记录、Asset Database 源依赖和最终发布的 Runtime Mesh
必须对应同一组实际文件内容。任一输入在导入或发布期间变化时，候选被丢弃、revision 主动推进并重新调度，而不是等待下一次
文件监视碰巧补救。

## 原问题

已有 revision 验票只能覆盖数据库已经观察并提交的变化。glTF 修改后可能引入一个数据库尚不知道的新外部 `.bin`：

```text
scan glTF -> revision 8
Worker 读取 glTF + new.bin -> CPU candidate
new.bin 再次变化（Database 尚无这条 import dependency）
Owner 登记 new.bin 当前签名
Owner 发布旧 CPU candidate
```

因为登记依赖时使用的是“现在的文件签名”，旧 CPU 数据可能被错误地配上新输入状态，下一次 scan 也看不出差异。Mesh cache
此前又在 `store()` 时独立重新读取文件生成指纹，进一步可能把 stale MeshData 与较新的 cache input record 组合。

## 可复用 ImportInputSnapshot

新增 `asset/import/input_snapshot`，统一表达项目内一次 Importer 输入观察：

```cpp
struct ImportInputFingerprint {
    std::filesystem::path relative_path;
    std::uint64_t size;
    std::uint64_t hash;
};

struct ImportInputSnapshot {
    std::vector<ImportInputFingerprint> files;
};
```

第一个文件固定为主源资产，其余依赖按规范化项目相对路径排序、去重。捕获时对路径做 canonical root 边界校验，拒绝通过 `..`
或 symlink 逃出 `assets/` 的输入；内容使用流式 64 KiB FNV-1a 生成大小和 64-bit hash。它不是加密完整性机制，而是确定性导入
一致性与缓存失效键。

`capture_import_inputs()` 负责建立不可歧义快照，`import_inputs_are_current()` 重新读取同一输入并验证路径顺序、边界和内容。
该结构不依赖 MeshData、fastgltf 或 cache 格式，下一步可直接用于 Texture，未来也可用于 Shader include。

## Importer 前后观察

`MeshImporter::import_with_dependencies(source, asset_root)` 现在执行：

```text
解析 glTF 输入清单 A
  -> 捕获内容快照 A
  -> 执行实际 Mesh import
  -> 再解析输入清单 B
  -> 捕获内容快照 B
  -> A == B：返回 MeshData + dependencies + snapshot
  -> A != B / 后观察失败：返回 inputs_changed_during_import
```

若实际 import 本身抛错，但前后输入一致，仍传播原始解析/校验错误；若 import 失败期间输入同时变化，则优先返回可重试的输入变化，
避免把写入中间态固化为普通失败。当前会增加 cache miss 时的文件读取次数，但发生在 Worker，正确性优先；后续可通过一次解析
同时产出 MeshData 与依赖清单来优化，不改变 snapshot contract。

## Cache 与 Candidate 使用同一快照

MeshImportCache 的序列化字节格式没有改变，仍保存 source/dependency 相对路径、大小和 hash；API 改为直接接收 candidate 的
`ImportInputSnapshot`。写 cache 前验证快照仍是 current，不再自行生成另一份指纹。cache load 验证落盘输入后，也把同一快照
返回给 Mesh candidate，cache hit 与 fresh import 因而走完全相同的 Owner Thread 发布检查。

## Owner Thread 发布门

后台 Mesh completion 在三个有副作用边界检查 input snapshot：

```text
candidate arrives
  -> revision current
  -> inputs current
  -> optional cache store
  -> inputs current
  -> record import dependencies
  -> recoverable GPU Mesh creation
  -> revision current
  -> inputs current
  -> Registry replace
```

输入变化时调用 `AssetDatabase::invalidate_import_inputs()`：先登记 candidate 已发现的依赖和当前源签名，再无条件分配一个新
AssetRevision。旧 Runtime Mesh 保持，当前 candidate 不写 Registry，随后按新 revision 立即调度后台任务。即使变化来自数据库
此前未知的 `.bin`，也不依赖下一次 500ms monitor scan 才重试。

GPU 创建期间发生变化时，新 GPU candidate 会被直接释放；可恢复创建和 UploadManager completion 仍保证其内部资源按既有
生命周期安全回收。

同步首载也在 cache 写入前后及 GPU 创建后验证 inputs；检测变化时返回失败并提示调用方重试，不发布不一致对象。同步函数入口
同时复制 AssetHandle 和相对路径，不再让 `AssetRecord&` 跨越可能重入数据库 scan 的 ResourceFactory 调用点。

## 验证

- ImportInputSnapshot 对依赖排序、去重并保留主源文件在首位；
- 相同大小但内容变化可被 hash 检出；
- 项目根目录外输入被拒绝；
- MeshImporter 返回可再次验证的 source + external buffer 快照；
- MeshImportCache round-trip 恢复同一快照，源或依赖变化继续使 cache miss；
- 确定性测试在 GPU factory 调用期间再次修改外部 buffer：旧 candidate 创建后不发布，数据库 revision 推进并自动调度新任务，
  第二个 candidate 最终替换旧 Mesh；
- 既有“ResourceFactory 回调中 scan 导致 revision 变化”测试继续通过，并验证不再跨重入点使用失效 AssetRecord 引用；
- Debug 全量构建和完整 CTest。

## 下一步

下一步让 Texture 后台 candidate 复用 `ImportInputSnapshot`。Texture 只有单一源文件，不需要 Importer 依赖登记，但同样要在解码
前后捕获、Owner Thread GPU 创建前后复核，并在变化时推进 revision、自动重调度，避免短暂发布旧像素。
