---
last-reviewed: 2026-11-02
status: adpcm-only
---

# RF Link App ↔ Passport 手咪对接指南

当前入口为[仅ADPCM音频合同](adpcm-only.md)：Info只声明format=2，20ms/160采样/100字节，不回退PCM，不支持Opus。基础GATT/PTT/资料规则见[基础协议](protocol-v1.md)。历史iOS路径仅作最初设计基线，不代表当前手机App代码；本轮未修改App。

## 1. 先建立正确的产品边界

- App继续作为唯一网络客户端，持原身份/证书/私钥，照常运行现有MQTT/SAS/APRS/QSO/Opus/RADPCM。Passport只替代音频I/O和提供PTT按键/状态显示。
- 不新增设备证书申请、Passport业务私钥、BLE签名代理、Wi-Fi配置和外部盒子协议。
- 这是自定义Core Bluetooth配件，不会出现在系统音频路由中充当标准耳机。App必须显式实现手咪音频路由。
- 只修改本项目文档并不代表授权修改App仓库；本轮未修改App代码，未启动模拟器或进行App构建。

## 2. 当前iOS代码对应位置

本次核对目录为 `/Volumes/DataVolume/Development/projects/FMO-Link`。基线提交 `97de094e141cac7dcf5703e6b60d39a40fe886dd`，工作区干净。接口按当前源码核对，实施时重新核实，不把历史行号当稳定API。

| 现有相对源码路径 | 当前职责 | 对接变化 |
| --- | --- | --- |
| `FMOLink/Features/Voice/UI/DirectVoiceSessionModel.swift` | PTT、路由、音频流任务和UI状态 | 新增外部手咪路由，集中处理按压代次/取消/故障 |
| `FMOLink/Features/Voice/Audio/DirectVoiceAudioEngine.swift` | 8kHz PCM播放、手机麦克风采集、提示音 | 保留手机路由；手咪路由不启动手机采集/播放，明确提示音应在何端播放 |
| `FMOLink/Features/Voice/Session/DirectVoiceSession.swift` | 授权、话权、编码、RAW发布及解码PCM输出 | 复用 `beginTransmit()`、`appendCapturedSamples(_:)`、`endTransmit(completed:)`、`playback()` 和 `states()` |

当前 `DirectVoiceSessionModel.beginTransmit()` 会申请手机麦克风权限并启动本机AVAudioEngine，**不能直接从BLE按键调用这一UI方法**。应拆出共享话权/任务编排，以外部PCM流绕过本机采集路径；是否需要系统/PushToTalk音频授权仍由实际使用的框架决定，不能据此承诺任何权限都不需要。

建议增加职责边界（名称可由App团队决定）：

- BLE连接/配对/订阅和版本协商。
- 控制分片、请求ID/响应匹配、超时/重试和状态镜像。
- PTT会话协调：物理按压→网络话权→设备采集；取消代次贯穿异步操作。
- 音频适配：8kHz采样FIFO，按协商格式切片、编解码、调度、处理Flow信用与时效。固定160采样/20ms的IMA。
- 外部音频路由生命周期：与手机路由互斥，网络/身份变化时统一停止。

这些是设计职责，不是要求复制某一套类名，也不要求重写网络协议。

## 3. 连接和版本协商清单

1. 按正式Service UUID扫描，先读Info确认 `product/major/formats`，不能只按设备名。
2. 用户在设备本地打开配对窗口，处理系统Passkey配对。使用受保护GATT前确认加密/受信bond；不要实现MAC白名单替代SMP。
3. 订阅Control TX（indicate）、Mic Audio（notify）、Device Flow（notify）；确认 `didUpdateNotificationStateFor` 成功。
4. 查询 `maximumWriteValueLength(for: .withoutResponse)`，传入HELLO。不要把 `.withResponse` 的长写上限当作无响应写容量。
5. HELLO及其响应按20字节GATT值上限分片，协商成功后控制片才可扩大到M；即使Core Bluetooth支持属性长写，也不绕过应用分片合同。
6. 确认Info包含format=2，以minor≥1/format=2发送HELLO；`audio_ready`且M≥100后同步radio状态，再把UI路由显示为可用。否则明确显示“不支持当前音频包大小”，不能只显示连接成功。
7. 在流开始前创建sid/stream上下文和固定上限队列，等待接收者Flow信用，再按HELLO协商的20ms节拍发送。
8. 所有状态绑定本次连接generation。断线/恢复后新建协议会话，不使用旧sid/请求/stream/按住状态。

App应在 `centralManagerDidUpdateState`、连接失败、特征订阅失败、服务失效和配对删除时进入同一清理路径。OS保存的peripheral标识适合查找已授权设备，但不是本协议的硬件认证证据。

## 4. 音频与背压

BLE采样固定8000/16bit/mono；网络编码仍由App当前策略决定。Int16数组与字节的关系必须显式小端处理，不依赖Swift结构体内存布局/本机对齐。

- 上行：收到Mic Audio→按协商格式验证长度、头、sid/stream/seq→独立解码84字节块为160个采样→有界FIFO→调用 `session.appendCapturedSamples(samples)`。既有编码器再合并为自身所需采样数；不能每包新建网络话音流。
- 下行：订阅 `session.playback()` 的解码PCM→原网络缓冲→按160采样编码84字节块→20ms节拍→Speaker Audio。不要把网络聚合包中200/240ms的全部PCM瞬间灌给BLE。
- 只在协议信用允许且Core Bluetooth无响应写就绪时提交音频；`writeValue(...,.withoutResponse)`没有业务完成回调，返回不等于扬声器已播放。
- `canSendWriteWithoutResponse=false` 时停止提交，等 `peripheralIsReady(toSendWriteWithoutResponse:)`，期间不得无限积压。恢复时丢掉超过120ms的本地待发音频，保留seq缺口/设置discontinuity。
- Flow应高于PCM排队优先级，控制stop/PTT释放高于普通状态更新。系统背压时不能让大量音频挡住松键/租约处理。
- Web的 `writeValueWithoutResponse()` Promise也不替代协议信用。Web用于开发辅助，实际iOS回调与后台必须单独验证。

示意调度，不是可直接粘贴的完整Swift实现：

```text
每20ms的单调时钟tick：
  先处理取消/租约/紧急stop
  刷新需要发送的Flow
  若路由/会话/流无效：丢弃待发PCM
  丢弃seq < peer.next_seq或超过本地时效的帧
  若无信用或Core Bluetooth写队列不可用：不提交PCM
  否则提交一帧100字节；最多补发一帧仍新鲜的积压
```

设备扬声器默认Codec90只作为起点；测试正弦峰值12000并不表示所有语音都乘5.45。语音RMS、峰值、削波和增益须单独调试。mute/volume0应数字静音，不依赖Codec最小档完全无声。

## 5. PTT异步竞态必须处理

- 保存 `{connectionGeneration,sid,press_id,stream_id}`，每次await返回都核对仍是同一次按压/路由/网络会话。
- 获取话权期间松键：取消pending；若 `beginTransmit()` 晚成功，立即 `endTransmit(completed:false)`，不发设备start。
- 设备拒绝start（已松键/不就绪）、控制响应超时、Flow租约过期或mic断流：取消采集输入并结束网络TX，不能只改按钮颜色。
- device release不等待发送stop的业务ACK；先停网络发布/采集消费，再幂等停止设备流。
- 原系统PTT/屏幕PTT与设备按键不能竞争同一mic流。1.0手咪路由要求物理按键授权，手机入口提示使用手咪；切回手机路由再运行原逻辑。
- 不静默切换到手机外放或手机麦克风；断线明确提示配件不可用，恢复后重新协商并要求释放后重按。
- 业务呼号、网络服务器和接收者由App状态决定。设备按键不直接构造网络身份或决定网络授权。

## 6. 背景运行与系统音频

需要评估 `bluetooth-central`、Core Bluetooth状态恢复、既有PushToTalk/音频会话与真实业务的配合。**仅声明蓝牙后台模式不能保证持续音频任务永远运行。** 禁止用未被认可的静音循环伪装后台能力。

第一阶段至少分别验收：App前台、锁屏接收、锁屏设备PTT、切其他App、来电/系统音频打断、系统回收/用户强退、蓝牙关闭/恢复。若系统约束导致不能达到某项，应明确支持范围，不能在协议文件中预先宣称后台通过。

## 7. 联调顺序及验收矩阵

先用公开虚构向量做序列化互通，再接正式测试固件；不要直接连生产网络排查基本字节错误。

| 阶段 | 至少通过的检查 |
| --- | --- |
| 协议向量 | 控制分片20/244字节边界、逐字节PCM小端、Flow exclusive窗口、错误长度/flag/sid拒绝 |
| GATT与配对 | 首配确认、单bond、旧手机不能抢绑、解绑/重连后新sid；实验UUID拒绝 |
| 持续音频 | 双向轮流ADPCM，10/30分钟；丢包/过期/欠载/队列/堆曲线；不同实际MTU/背压 |
| PTT正常 | 按住/松开、话权拒绝、提示、上行编码、远端结束与半双工切换 |
| PTT异常 | 按下后立刻松开、grant晚到、切服/吊销/掉网、断蓝牙、进菜单、订阅取消、credit丢失/回退 |
| 手机路由 | 手机音频和配件互斥，静音恢复不播旧音，断线不突然外放 |
| 系统生命周期 | 上节所有后台/打断/恢复场景；硬件定时器停止与App恢复清理分别记录 |
| 音频质量 | 真人语音、输入增益、输出响度、失真/啸叫、实际声学延迟；数据RTT不能代替 |

验收记录包含App提交/构建、设备镜像哈希/ELF、手机型号/iOS版本、MTU或可写长度、信号环境、场景、持续时间与原始计数。数据缺失不填PASS。前台通过、后台通过、服务端互通和音质分别报告。

## 8. 官方参考和现有证据

- [Apple：maximumWriteValueLength(for:)](https://developer.apple.com/documentation/corebluetooth/cbperipheral/maximumwritevaluelength(for:))：按写入类型查询单次可写长度。
- [Apple：canSendWriteWithoutResponse](https://developer.apple.com/documentation/corebluetooth/cbperipheral/cansendwritewithoutresponse) 与 [ready回调](https://developer.apple.com/documentation/corebluetooth/cbperipheraldelegate/peripheralisready(tosendwritewithoutresponse:))：无响应写提交背压。
- [Apple：writeValue](https://developer.apple.com/documentation/corebluetooth/cbperipheral/writevalue(_:for:type:))：withoutResponse不保证写成功且无失败原因回调。
- [Apple：Core Bluetooth后台执行](https://developer.apple.com/library/archive/documentation/NetworkingInternetWeb/Conceptual/CoreBluetooth_concepts/CoreBluetoothBackgroundProcessingForIOSApps/PerformingTasksWhileYourAppIsInTheBackground.html)：事件唤醒/恢复与系统终止边界。
- [本仓库Web音频实验](../../development/ble-audio-lab-validation-2026-10-31.md)：测试基线及用户试听；与正式协议未实现项严格区分。

## 9. 首版TELE/RIG与讲话人资料

必须一并实现[资料传输合同](station-metadata.md)：radio.update同步当前服务器和context_id；talker.update同步当前话音身份和turn_id；station.tele/rig/position分别传频率、电台、位置/距离。speaker start带turn_id，避免资料和音频串人。App解析/关联/计算，设备只显示；消息不能等待完整RIG才开启音频。时效、位置权限拒绝和旧资料清除也是首版验收项。

## UI v2音量编辑约束（设计，尚未实现）

设备本地音量页存在未确认编辑或快捷调节事务未结算时，App的`volume.set`可返回合同已有的`busy`，App不得把失败响应当作已生效；恢复后读取Status或按业务需要重新发起请求。已成功的请求仍遵守幂等缓存，不能因页面占用而改写同一请求的历史响应。主题和最近通话缓存均为设备本地功能，无需新增GATT特征或消息。
