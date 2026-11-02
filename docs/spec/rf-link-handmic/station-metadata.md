---
last-reviewed: 2026-11-01
status: frozen-1.0
---

# 首版服务器、讲话人、TELE／RIG与位置摘要

这是[协议1.0](protocol-v1.md)的必选组成部分，使用同一Control RX/TX及JSON分片，不新增音频格式或GATT特征。用户已要求首版包含这些信息；字段和时效值已纳入2026-11-01定稿合同，实际验证边界另见验收记录。

## 1. 来源和分工

App负责订阅、解析、校验、关联和缓存现有网络数据，再给设备发送有界的展示摘要。设备不连接MQTT、不解析原始TELE/RIG/APRS、不请求地图/定位服务，也不计算地理距离。

| 设备首版信息 | 来源及限制 |
| --- | --- |
| 当前服务器 | App选中且实际连接的服务器，沿用radio.update.server显示名；可选server_uid（非零uint32）、server_callsign（合法完整呼号≤24字节），仅作识别；server展示名≤48 UTF-8字节，App对更长原名构建明确的短展示名 |
| 当前讲话人呼号/UID | App当前有效RAW话音流/DirectVoiceSession状态；不得从最近一条TELE猜讲话人 |
| TELE | frequencyA/B与呼号、UID、时间；**不含经纬度** |
| RIG | 六位Maidenhead网格、型号、频率、天线、天线高度；**没有网络原始时间戳**，age按App接收时间计算 |
| 对方位置 | 可关联且仍有效的APRS位置，或RIG网格中心近似位置；无数据时unknown |
| 与我的距离 | App用有效手机定位与对方位置计算的地表直线距离；不是道路/无线电传播距离 |

当前源码基线 `97de094e141cac7dcf5703e6b60d39a40fe886dd`：`Protocol/FMOStationPackets.swift`、`Session/FMOStationCache.swift`、`Session/FMOStationExchange.swift`。网络TELE/RIG的CRC和UID关联不是密码学验身，不能在设备上据此标记“已实名认证”。既有共享开关默认关；此协议接收展示已有资料，**不自动打开App对外共享或广播位置**。

## 2. 防止串人、串服务器

- `context_id`：App在每次新BLE sid内从1递增，连接/切换网络服务器时换新值，不复用。所有radio与摘要操作必带。首次radio.update必须context_id=1；之后设备仅接受当前context_id的替换或更大的新context_id，较小值返回stale_context；其他资料仅接受当前context。计数耗尽前重建sid。radio.update的context变化必须先停止旧流、清当前讲话人与资料，要求PTT释放后再用新上下文。
- `turn_id`：App为每次网络接收话音轮次递增分配非零uint32，同sid内不复用；同人下一次讲话也换新值。守听空闲无当前轮次时为0。临时静音/重新开speaker流可以继续使用同一未结束的turn。
- 每条资料以 `{sid,context_id,turn_id,uid,callsign}` 完整匹配当前讲话人，不只按呼号或UID查最近缓存。SID验证沿用主协议，**资料消息**的context不符返回 `stale_context`，radio.update允许以下明确的推进例外，轮次/UID/完整呼号不符返回 `stale_talker`。错误不修改显示、不延长流租约。
- App先发radio.update，再发talker.update(active=true)，再start speaker音频；完整TELE/RIG/位置可晚到，不能为等资料阻塞第一段语音。speaker start新增必选turn_id且必须匹配当前active讲话轮次；mic start的turn_id=0。
- talker.update(active=false)必须携带当前轮次和身份，立即结束当前显示、清其TELE/RIG/位置并停止相关speaker流；它不得结束新轮次。若要保留“最近讲话人”，UI必须单独标成历史，不作为当前讲话人或当前距离。
- 新talker.update(active=true)只接受递增的新turn_id，或与当前turn_id/身份完全相同的幂等重复；不能回退到旧轮次或在同轮次换人。更换讲话人时先停止旧speaker流，再提交新轮次。
- 同一网络服务器重连也换context_id；否则TELE/RIG缓存应按App既有连接代次失效规则清除。配件不保留跨BLE会话资料。

## 3. 控制操作与类型

下表操作均为App→设备请求，业务响应 `r={}` 表示已应用；使用主协议id/缓存/重试规则。每条是**完整替换快照**，必选键不得省略；未知值用null，不沿用上一条已知值。

| op | p必选字段 |
| --- | --- |
| talker.update | context_id、turn_id、uid、callsign、active |
| station.tele | context_id、turn_id、uid、callsign、frequency_a_hz、frequency_b_hz、age_s、valid_for_ms |
| station.rig | context_id、turn_id、uid、callsign、grid、model、frequency_hz、antenna、height_m、age_s、valid_for_ms |
| station.position | context_id、turn_id、uid、callsign、source、lat_e7、lon_e7、accuracy_m、age_s、valid_for_ms、distance_m、ref_age_s、approx |

公共字段：context_id/turn_id/uid为非零uint32，callsign是当前话音已校验的完整呼号、1–24个ASCII大写字母/数字/连字符/斜线，不是任意自由文本。active/approx为bool。frequency_*_hz是正uint32或null；App把现有MHz浮点值乘1000000并四舍五入为Hz，拒绝非有限/越界值，不能直接把145.5写进Hz字段。grid为合法大写六位网格或null；model/antenna各最多15 UTF-8字节或null；height_m为uint32或null，表示天线高度，**不是海拔**。

没有可用TELE/RIG时可发送对应所有业务字段为null、age_s=null、valid_for_ms=0来清除；一份有效TELE须有两个频率，一份有效RIG须有完整grid/model/frequency/antenna/height，沿用App当前验证器；不应把不完整网络包拼成完整资料。App可对各来源分别清除，不必假造频率0。talker结束时设备自动清除所有资料，不要求额外三条清除请求。

位置字段：

| 字段 | 类型与语义 |
| --- | --- |
| source | `aprs`、`rig_grid`、`unknown`；App只用既有校验/关联后仍有效的来源，优先APRS，否则RIG网格中心 |
| lat_e7 | int32或null，纬度×10^7，范围[-900000000,900000000] |
| lon_e7 | int32或null，经度×10^7，范围[-1800000000,1800000000]；这两个字段是主协议默认无符号整数的显式例外 |
| accuracy_m | uint32或null；来源未提供/不能可靠估计则null，不假造GPS精度；grid始终标approx |
| age_s | uint32或null，来源数据当前年龄；TELE/APRS按App验证后的源时间，RIG按App收到时间 |
| valid_for_ms | 0–300000，设备收到后用本地单调时钟倒计时；不能因重发快照延长源数据有效期 |
| distance_m | uint32或null，App计算并四舍五入的米数；0表示真实约0米，null才是未知 |
| ref_age_s | uint32或null，用于本次距离的手机定位年龄；没有有效距离时必须null |
| approx | bool；网格中心、低精度位置或精度未知时必须true，显示“约” |

手机定位默认必须来自App允许使用、未撤销授权、年龄≤60秒且水平精度有效的定位；精度>100米时approx=true。没有有效手机定位时仍可显示对方位置，但distance_m/ref_age_s=null。首版不隐式改用App手动填的固定坐标，不需要向设备发送手机自己的原始经纬度。

所有位置快照都有0–300秒来源展示上限；有效源的valid_for_ms不得大于 `(300-age_s)*1000`，age_s>300则必须清除。TELE/RIG同样遵守此上限；RIG没有源时戳，界面须理解为“资料接收年龄”，不能声称是当前实时GPS。unknown位置要求lat/lon/accuracy/age/distance/ref_age全部null、valid_for_ms=0、approx=true。

有效距离的额外有效期是手机参考定位剩余时间：设备在收到 `station.position` 后，只显示distance最长 `min(valid_for_ms,(60-ref_age_s)*1000)`；到期先清distance/ref_age，即使对方位置仍有效。有效distance要求ref_age_s≤60；设备显示的age随着本地单调时间推进，不能停在收到时的数字。

每个操作都必须保持完整JSON≤512字节（含主协议信封），不能靠截断UTF-8或静默删必选字段适配。把TELE/RIG/位置分开就是为了有界长度及分别过期。公开示例与长度检查见vectors.json。

## 4. 推送时机与控制优先级

- 连接/HELLO完成后发送radio与当前讲话人快照；如果正在RX，再依次同步有用资料，不要求设备主动轮询。
- 服务器/讲话人/发射授权/静音变化及时同步；TELE或RIG更新、关联失效和到期触发相应快照/清除；收到相同数据不重复排队。
- 同一讲话人位置/距离最多每秒更新一次；手机移动、定位更新时由App重算距离。超时清除不受此节流延迟，必须立即处理或设备本地到期清除。
- stop/PTT释放/故障响应优先，talker开始/结束其次，TELE/RIG/位置再次。各类别只保留当前最新一条待发快照；旧turn的排队资料直接丢弃，已开始的控制分片不能插队。
- 这些资料不使用Audio/Flow通道，不逐音频包重复。数据更新不续租PTT，也不作为话权允许证据。
- 不要求设备缓存网络全体用户或显示地图；首版保证当前讲话人摘要。后续列表/历史另加能力，避免把128用户的手机缓存照搬进C3。

## 5. 设备显示与恢复

默认主屏应能表达：当前服务器、自身呼号/网络状态、当前讲话人、已知位置（网格或坐标）、约多少米/公里、可用频率/型号/天线/高度；具体分页/排版后续做UI设计。

资料未知、过期或关联歧义时显示“未提供/未知”，不能用上一位讲话人的值或0公里占位。`rig_grid`距离必须显示“约”，不能呈现到米级精度暗示精确定位。缺RIG但有音频时必须正常播放。

Status增加context_id/turn_id两个索引，**不把三份大资料内嵌进512字节Status**。radio仅回主协议定义的核心字段，可选server_uid/server_callsign不内嵌Status；需要恢复时由App重新推当前完整快照。BLE断线/新sid、context变化、talker结束、本地资料到期分别清理，不能等下一条网络报文才撤掉过期位置。

## 6. App对接与验收新增项

App从 `DirectVoiceSessionSnapshot.currentUID/currentCallsign/phase` 识别讲话轮次；通过 `FMOStationExchange` 的受当前连接约束的缓存查询站点资料，不另外绕开 `FMOStationCache` 的同UID歧义和300秒时效检查。位置从App已有有效APRS/网格逻辑取得；距离在App计算，设备只格式化展示。

必须验证：

- A→B讲话切换时A资料晚到；同呼号不同UID；同UID关联歧义；同人下一次讲话资料不能串轮次。
- 切服/服务器重连、BLE重连、mute/unmute、RX结束及话音间歇后重新开speaker流。
- TELE只有频率无位置；RIG网格是近似位置；无RIG不阻塞音频；位置权限拒绝时仍显示呼号和已有电台资料。
- source年龄和TTL递减、重复快照不能续命、手机定位先过期时距离单独消失、距离0与未知分开。
- 最长中文UTF-8型号/天线/服务器名、最大uint32数值、南纬西经和空值，确保每条JSON≤512字节。
- 音频/PTT持续运行时每秒位置更新，观察控制队列/音频抖动，状态消息不能饿死停止动作。

本增量只更新协议与对接文档，没有新增固件/App实现或真机通过声明。
