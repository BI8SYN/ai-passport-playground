# macOS BLE 诊断桥

原生源码 `BLEBridge.swift` 使用 AppKit/CoreBluetooth 与 Foundation，不依赖主机 ESP-IDF。`build_bridge.py` 仅使用 Python 标准库、现有 `xcrun swiftc` 和 `codesign`。默认产物为 `artifacts/handmic-simulator/RFLinkBLESimulator.app`，构建不会启动 App。

## 已实现

- 使用独立 bundle ID `local.bi8syn.RFLinkBLESimulator`，配备蓝牙用途说明与仅本地 HTTP 的 ATS 配置；App 作为 accessory 运行，无 Dock 窗口。
- 连接 `ws://127.0.0.1:8765/bridge`，重试间隔 0.5–10 秒；接收帧限制 64 KiB、发送队列上限 256 条。连接断开清空旧发送数据、停止广播、重建虚拟 GATT，重连后需要后端重新发送广播授权。
- 精确发布正式 `9c3a0001-7a9b-4d9f-a15f-4d4f4c494e4b` 服务与 0002–0009 特征。Info/Status 均为动态读取，正确响应 offset，启动时提供明确的诊断占位 JSON，随后原样使用后端 JSON 快照。
- Info 无需加密；Status 请求读取加密，控制/音频/流控写入请求写加密，ControlTX 指示和 Mic/DeviceFlow 通知请求加密订阅。ControlTX 仅 `.indicate`，Mic/DeviceFlow 仅 `.notify`。
- 首个合法订阅 central 成为当前虚拟连接，记录系统 UUID 与最大值；额外 central 的写入被拒绝、其订阅不加入会话，也不向其发送数据。控制/麦克风/流控三项订阅齐备才上报 `subscribed=true`。取消任一订阅立即废弃旧队列与就绪状态；全部取消则上报断开。
- 同一 ATT 回调批次先完整校验，再按原序转发；每个完整 ATT 值附 `DispatchTime` 单调纳秒 `arrival_ns`。控制 9–244 字节、扬声器 176 字节、AppFlow 24 字节，校验 offset/当前 central/三项订阅/本地桥状态。每次写请求批次只应答一次；ACK 仅表示本地接收，业务响应异步产生。桥断开或队列满会拒绝，不静默丢弃。
- 控制消息按 8 字节分片头验证，在完整重组后以原分片顺序排队，最多 16 条完整消息；不同 transfer 不插片。异常、超时、队列满重建虚拟会话。Mic 最多 12 帧且 120ms 过期，Flow 合并保留最新值。`updateValue=false` 保留当前队首，仅在 ready 回调后重试；全部数据明确指定当前 central，不依赖默认广播式更新。
- 每秒发送 `bridge_metrics`（队列、丢帧、合并、背压、拒绝写入、订阅与 max value），不写 PCM 到日志或磁盘。每条通知检查 central 最大值，拒绝系统可能进行的隐式截断。
- 广播名称 `RF Link · SIM`，仅后端 `advertise.enabled=true` 且服务添加完成、WebSocket 可用、无当前 central 时启动；不会由本桥自行打开配对窗口。`reset_link` 重建本 App 的 peripheral manager/服务，不触碰实体 Passport 或系统绑定数据库。SIGTERM/SIGINT 停止本 App 广播并退出。

## 安全和真机边界

CoreBluetooth 的加密权限由 macOS 实施。公开 peripheral API 无法复刻 ESP 的 Secure Connections 六位口令、读取 MITM/认证/密钥长度状态或选择实体设备口令；本桥不显示伪造配对码、不声称通过固件安全验收。为现有 App 的协议兼容性，后端 Info 中的 `pairing:"sc_passkey"` 保持原样，但它不能证明 Mac 采用了与实体设备相同的配对方式；Web 必须同时显示诊断限制。

macOS 可以在首次启动时弹出蓝牙权限对话框，用户需允许。手机通常可使用现有客户端扫描同服务 UUID 连接此诊断外设，但 iOS 连接、OS 配对交互、实际包间隔与音频仍需真实验证。Mac 蓝牙连接参数、OS 调度及音频时钟不同于 ESP32，诊断数据不等价于实体 Codec/RF/时序验收。

CoreBluetooth peripheral 没有通用的连接/断开回调和逐个 central 的公开强制断开方法；本桥根据订阅生命周期跟踪虚拟会话，额外 central 已建立的系统链路不能被单独强制踢除。重建 manager 的目标是撤销本 App 的 GATT/虚拟会话，不能承诺删除或重置系统 ACL/OS bond。未订阅只读取 Info 的设备不会被当作已就绪连接。

## 本次验证

2026-11-01 在当前 Mac 使用 Apple Swift 6.3.3、`-swift-version 5 -O -warnings-as-errors` 实际构建成功；本地 ad-hoc 签名与 `codesign --verify --strict` 通过。Info.plist 已由 Python `plistlib` 解析并校验关键字段。

首次由上层尝试启动时发现编译器默认目标为 macOS 28、而主机为 27.0.1，LaunchServices 拒绝启动（-10825）。构建脚本已改为从本机架构和 plist 最低系统版本显式生成 `-target arm64-apple-macosx13.0`，重新构建与严格签名验证通过；`otool` 已确认 Mach-O `LC_BUILD_VERSION minos 13.0`。修正后尚未再次启动，等待服务器恢复。

桥 App **尚未成功启动**；修正部署目标后由上层在服务器就绪后启动。蓝牙服务实际发布、广告、手机连接、加密配对、ATT/通知与背压、断连恢复、虚拟清除绑定：**NOT RUN**。本桥构建结果不代表产品固件构建或实体设备验收。

启动由上层 launcher 在 Web/后端就绪后负责；可使用 `open -n artifacts/handmic-simulator/RFLinkBLESimulator.app`，退出需通过 launcher 发 SIGTERM 或终止该 App 进程。不要因此打开实体 Passport 串口或修改其绑定。
