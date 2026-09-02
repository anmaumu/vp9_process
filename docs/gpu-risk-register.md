# GPU-resident Implementation Risk Register

GPU decode→external export→外部処理→import→encode、native handle、DLPackを実装する際の主要リスクと必須検証を管理する。

| Risk | Failure mode / impact | Required mitigation | Verification |
|---|---|---|---|
| `RISK-GPU-001` | completion前のsurface再利用による画像破損・use-after-free | producer/consumer completionとexternal leaseを再利用条件にする | TEST-GPU-001, TEST-GPU-002 |
| `RISK-GPU-002` | release、unmap、unregisterの二重実行または循環参照 | generation、single-owner cleanup、idempotent terminal state | TEST-GPU-002, TEST-GPU-012 |
| `RISK-GPU-003` | CUDA context/device、D3D11 device、VA display不一致 | device identityをdescriptorに含めsubmit/export時に検証 | TEST-GPU-003 |
| `RISK-GPU-004` | producer未完了のresourceを別stream/APIが読むrace | completion dependencyをconsumer stream/fenceへ挿入 | TEST-GPU-004, TEST-GPU-009 |
| `RISK-GPU-005` | DLPack deleterとPython GC競合、interpreter shutdown crash | deleterがnative leaseのみ保持しPython callbackへ依存しない | TEST-GPU-009, TEST-GPU-010 |
| `RISK-GPU-006` | texture/VA surfaceをlinear USM/CUDA pointerと誤表現 | memory type別descriptor、表現不能なDLPack exportを拒否 | TEST-GPU-005, TEST-GPU-009 |
| `RISK-GPU-007` | NVDEC mapped frameを早期unmap、NVENC登録resourceを早期解除 | completion付きslotにmap/register lifetimeを束縛 | TEST-GPU-007 |
| `RISK-GPU-008` | oneVPL decode/import surfaceをexternal/encode完了前にRelease | external leaseとencode SyncPoint完了までsurface refを保持 | TEST-GPU-006, TEST-GPU-019 |
| `RISK-GPU-009` | driver reset/device lostでwaitが永久block | timeout、terminal failure伝播、全waiter wakeup | TEST-GPU-012 |
| `RISK-GPU-010` | hidden CPU readback/fallbackで性能・契約違反 | operation単位のcopy traceとstrict policy gate | TEST-GPU-011, TEST-GPU-013 |
| `RISK-GPU-011` | bounded pool枯渇によるdeadlockまたはVRAM増加 | backpressure、cancel wakeup、固定上限、soak計測。Intel external encoderはdevice lifetimeのため最初のframeをflush/closeまで保持するので、その1 slotをpool容量へ算入する。1-slot poolでの連続decode/importにはframeとは独立したdevice owner設計が必要 | TEST-GPU-014, TEST-GPU-019 |
| `RISK-GPU-012` | pitch、plane offset、P010 alignmentの誤り | format別layout検証とguard領域付きgolden test | TEST-GPU-005, TEST-GPU-008 |
| `RISK-GPU-013` | export/import境界で色metadata/PTS消失 | immutable metadata伝播と境界assert | TEST-GPU-008 |
| `RISK-GPU-014` | native handleを利用者がlease後も保持して破損 | borrowed/export-lease API分離とreleased access拒否 | TEST-GPU-001, TEST-GPU-003 |
| `RISK-GPU-015` | imported resourceのrelease callbackを早期または二重実行 | submissionがownerをencode completionまでretainしsingle-shot cleanup | TEST-GPU-019, TEST-GPU-020 |
| `RISK-GPU-016` | 外部producer completion未指定・誤contextでencodeが未完成resourceを読む | completion必須化、device/context検証、stream/fence dependency挿入 | TEST-GPU-004, TEST-GPU-019, TEST-GPU-020 |
| `RISK-GPU-017` | submission中に外部resourceを変更し画像破損 | mutation禁止期間をcompletionまでとしdebug generation/owner検査 | TEST-GPU-019, TEST-GPU-020 |

Go/No-Go条件は`AC-GPU-001`および`TEST-GPU-001..020`を正とする。性能値だけではzero-copyを証明せず、API trace、CUDA event、oneVPL/D3D11/VA同期、CPU transfer counterを併用する。

## CPU borrowed/import risk

| Risk | Failure mode / impact | Required mitigation | Verification |
|---|---|---|---|
| `RISK-CPUINT-001` | NumPy viewの生存中にdecode slotを再利用しuse-after-free | view base/capsuleがnative owner leaseを保持 | TEST-CPUINT-001, TEST-CPUINT-002 |
| `RISK-CPUINT-002` | async encode完了前にinputを変更・解放 | submission completionまでowner retain、mutation禁止契約 | TEST-CPUINT-003, TEST-CPUINT-004 |
| `RISK-CPUINT-003` | managed arrayの長時間pinningで.NET GC断片化・pause増加 | asyncは固定容量native/pinned poolを使用しpin時間を計測 | TEST-CPUINT-005 |
| `RISK-CPUINT-004` | stride/alignment/format誤認によるout-of-bounds・色破損 | 全plane layout検証、strict拒否、copy fallback明示 | TEST-CPUINT-007 |
| `RISK-CPUINT-005` | GPU→NumPyをzero-copyと誤報 | download/conversion edgeを実測traceしstrict時は拒否 | TEST-CPUINT-006 |
