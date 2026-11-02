# Simulator 集成只读复核

范围：CONTRACT、Python HTTP/WS、Web/AudioWorklet、Swift BLE bridge、C backend，以及复用 core/audio 的对应行为。未运行构建、未访问实体设备、未改实现。

## 发现

1. **P1 — backend 退出后仍接受 BLE 写入并静默丢弃。** `server.py:230-231` 后端 stdout EOF 仅广播事件；`server.py:287-288` 忽略 `backend()` 返回 false。Native 的 WS 仍连接，`BLEBridge.swift:394-423` 继续 ACK 控制写入；服务、已有 central 和 Info/Status 也继续可见。复现：虚拟会话建立后 C/Docker 后端意外退出，App 后续写入仍被 native 接受，但无业务回复或播放。建议统一标记 backend 不可用、停止广告并重置虚拟链路，禁止在 backend 未恢复时重新发布可用会话，清除陈旧状态。

2. **P2 — 原始试听把短音频永久积压到下个通话，并把不同通话拼接。** `audio-worklet.js:17` 必须有1600样本才启动；`index.html:36` 的 raw 消息仅含 PCM，无流结束/间隔信息。复现：原始试听收到10帧100ms音频后停5秒，毫无声音；下一通话再来10帧即把两通话连续播放。`index.html:29` 关闭/重开试听也不清缓冲，可能重播上次残留音频。建议传递流/会话边界或依据空闲定时排出短尾，并在关闭/重开时清空。

3. **P2 — 桥接断开后 UI 可持续显示“App 已连接”。** `server.py:333-336` 清 bridge_state 却保留 native_metrics；`index.html:34` 依据旧 native.central_id/三项 subscriptions 再次覆盖 disconnected 徽章。复现：连接状态的 native bridge 进程退出，接下来每500ms诊断刷新都会重新显示 App 已连接。建议 detach 时清 native_metrics，前端连接状态同时要求桥接在线。

4. **P2 — C 端到达抖动把正常通话间隔计入。** `backend.c:177-181` 的 arrival_last_ns 从不按 SID/stream/断开重置；Python `server.py:89-90` 会按(SID, stream)重置。复现：两通话间正常等待10秒，第二流首包使 C rx_interval_max_ms 增至约10000、stddev陡升，与Python统计矛盾。建议只累计同一活跃流内间隔，或明确区分 idle gap 和 stream jitter。

## 其余检查

未发现明确的 GATT UUID/属性、控制分片、WebSocket目标路由、真实 UI按键/设置持久化、核心信用消费的跨组件不匹配。Host/Origin检查阻止外站浏览器直接访问WS；native桥接无Origin在预期本地信任范围。Codec增益、实体SMP/口令和ESP时序仍属于已明确声明的模拟边界，不据模拟器表现推断实体或iOS责任。

## 定向复审（四项修复后，源码检查）

未连接测试端、未注入音频/控制、未重启任何服务，保留当前 iOS 联调状态。以下测试结果来自主协调者提供，复审未重跑：server 31项、backend 8项、AudioWorklet短尾/flush/mic通过。

- **#1 PARTIALLY ADDRESSED，仍需关闭 EOF 排空竞态。** 新增 backend_failed 清缓存/关闭native WS、失败状态和 `/bridge` 503能覆盖通常退出；但 `backend_failed()` 的 `failure_reported` 早退与 `output(metrics)` 无条件恢复 `backend_alive=True` 冲突。如果输入线程先检测进程退出、reader随后排出 stdout 缓冲中的末尾metrics，finally的第二次 backend_failed 早退，后端永久被误标在线。应在失败后拒绝所有陈旧backend输出，健康状态更新在锁内受同一失败闩锁约束。源码 `server.py:218-226,255-256,286-289`。
- **#2 原始音频问题 ADDRESSED；新增 processed 正常结束截尾需修正。** raw按(SID,stream)/300ms空闲发边界、Worklet50ms短尾启动、试听开关flush已覆盖原报告复现。限制：网页预缓冲仍会改变听感，原始PCM不代表硬件播放时钟。新增问题：C `backend.c:315-318` 每次正常 stream/direction 变化都发 processed 边界，前端 `index.html:37` 一律flush；processed连续10ms PCM流被200ms预缓冲，stream.end边界会丢掉末尾约200ms音频，短于200ms的通话甚至全丢。正常连续播放时钟的start/end应保留已排队PCM，仅故障/断链/用户切换时清空，或使用不丢队列的流标记。
- **#3 ADDRESSED。** native detach清 native_metrics，backend不可用优先覆盖连接徽章。限制：正常通知/订阅状态仍有一秒native指标刷新延迟；并不代表实时无线连接事件。
- **#4 ADDRESSED。** C按最新speaker流隔离统计，切SID/流重置、非speaker清最后时间，并用rx_interval_scope明确范围。限制：Python到达窗口与C“最新流”聚合口径不同；数值无需逐项相等，均不证明实体ESP调度或iOS因果责任。

## 最终定向复审

**APPROVE：本报告四项问题及复审发现的两个残留项均 ADDRESSED。**

- 后端死亡状态：`output()`入口检查失败闩锁，metrics/cache写入在锁内再次检查；stdout排空无法恢复backend_available或失效缓存。新增 buffered-stdout 回归覆盖对应路径。
- 正常通话尾音：前端仅对 `source=all` 故障/断链边界及显式试听/切源操作flush；普通raw/processed通话边界不再清空排队PCM。50ms短尾排出保留，正常processed结束不再截掉预缓冲尾音。

本次仅只读检查上述代码和回归内容，并追加报告；未重跑测试、未注入流量、未重启服务、未操作实体或当前iOS连接。主协调者报告最终工具套件32项、backend8项与Node短尾检查PASS；真机与iOS故障归因仍需各自实际证据。
