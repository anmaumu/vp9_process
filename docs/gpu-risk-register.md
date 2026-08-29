# GPU-resident Implementation Risk Register

GPU decode→process→encode、native handle export、DLPackを実装する際の主要リスクと必須検証を管理する。

| Risk | Failure mode / impact | Required mitigation | Verification |
|---|---|---|---|
| `RISK-GPU-001` | completion前のsurface再利用による画像破損・use-after-free | producer/consumer completionとexternal leaseを再利用条件にする | TEST-GPU-001, TEST-GPU-002 |
| `RISK-GPU-002` | release、unmap、unregisterの二重実行または循環参照 | generation、single-owner cleanup、idempotent terminal state | TEST-GPU-002, TEST-GPU-012 |
| `RISK-GPU-003` | CUDA context/device、D3D11 device、VA display不一致 | device identityをdescriptorに含めsubmit/export時に検証 | TEST-GPU-003 |
| `RISK-GPU-004` | producer未完了のresourceを別stream/APIが読むrace | completion dependencyをconsumer stream/fenceへ挿入 | TEST-GPU-004, TEST-GPU-009 |
| `RISK-GPU-005` | DLPack deleterとPython GC競合、interpreter shutdown crash | deleterがnative leaseのみ保持しPython callbackへ依存しない | TEST-GPU-009, TEST-GPU-010 |
| `RISK-GPU-006` | texture/VA surfaceをlinear USM/CUDA pointerと誤表現 | memory type別descriptor、表現不能なDLPack exportを拒否 | TEST-GPU-005, TEST-GPU-009 |
| `RISK-GPU-007` | NVDEC mapped frameを早期unmap、NVENC登録resourceを早期解除 | completion付きslotにmap/register lifetimeを束縛 | TEST-GPU-007 |
| `RISK-GPU-008` | oneVPL surfaceをVPP/encode完了前にRelease | 各stageがsurface refを保持しSyncPoint完了後に解放 | TEST-GPU-006 |
| `RISK-GPU-009` | driver reset/device lostでwaitが永久block | timeout、terminal failure伝播、全waiter wakeup | TEST-GPU-012 |
| `RISK-GPU-010` | hidden CPU readback/fallbackで性能・契約違反 | operation単位のcopy traceとstrict policy gate | TEST-GPU-011, TEST-GPU-013 |
| `RISK-GPU-011` | bounded pool枯渇によるdeadlockまたはVRAM増加 | backpressure、cancel wakeup、固定上限、soak計測 | TEST-GPU-014 |
| `RISK-GPU-012` | pitch、plane offset、P010 alignmentの誤り | format別layout検証とguard領域付きgolden test | TEST-GPU-005, TEST-GPU-008 |
| `RISK-GPU-013` | GPU backend間・process fusion間で色metadata/PTS消失 | immutable metadata伝播とstage境界assert | TEST-GPU-008 |
| `RISK-GPU-014` | native handleを利用者がlease後も保持して破損 | borrowed/export-lease API分離とreleased access拒否 | TEST-GPU-001, TEST-GPU-003 |

Go/No-Go条件は`AC-GPU-001`および`TEST-GPU-001..014`を正とする。性能値だけではzero-copyを証明せず、API trace、CUDA event、oneVPL/D3D11/VA同期、CPU transfer counterを併用する。
