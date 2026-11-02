---
last-reviewed: 2026-10-31
status: research
---

# RF Link 协议、身份与数据边界

源码基线及本轮未运行事项见[入口](README.md)。以下是iOS当前实现，不是服务器全部规则或Passport验收结果。根证书、密码、实际用户材料及精确位置不复制到本文。

## 1. 名称和基本约定

RF Link是当前产品品牌，代码工程/类名仍多为FMOLink、FMO。线路的`FMO`、`FMO/RAW`、`APFMO4`等是协议标识，不能按产品改名随意替换。当前生产入口使用自有身份，不提取或复用外部盒子私钥，不冒充注册硬件厂商。

### 身份与编码

- 普通Ed25519：公钥32字节、签名64字节，对原始TBS直接签名，不是Ed25519ph。
- 证书体系为自定义JSON/确定性CBOR PKI，不是X.509；根→中间→用户。
- CBOR只使用无符号整数、bytes、UTF-8文本、定长数组、bool；最短编码，拒绝map/tag/float/负数/不定长/尾随字节和超限输入。
- 自己构造的签名TBS使用`DeterministicCBOR.Limits.signing`；解析不可信输入的默认64项等限制不能误用于合法大型CRL TBS。
- Base64URL不带padding；证书/签名/摘要的原始bytes与编码文本不能混淆。

证据：`Features/Voice/Identity/DirectVoiceIdentity.swift:302,541,609`；`Features/APRS/DeterministicCBOR.swift:3,21,106`。

## 2. 密钥、证书与正式信任

当前Keychain命名空间中，同一规范化呼号复用一把Ed25519私钥；签名时从Keychain读取seed并执行，不是硬件不可导出密钥。生产组合允许首次解锁后后台访问，ThisDeviceOnly避免向其他设备同步/备份；不能因此断言卸载就一定清除了所有Keychain材料。

证书稳定ID为`Base64URL(SHA256(root TBS)):uid`；同根同UID的新证书替换旧记录，跨根同UID分离。当前UI只有一个活跃身份。**这不支持直接把同一呼号的手机与多台Passport视作独立密钥实例**，新伴侣能力需要单独命名空间/实例标识和申请记录，不能借用现有App呼号私钥。

用户证书TBS：

```text
["FMO", 4, "userCert", issuerSn, uppercaseCallsign, uid, publicKeyBytes, iat, exp]
```

APRS线路CERT是在该数组末尾追加CA签名的十元CBOR数组。用户证书指纹为TBS的SHA-256；APRS报文绑定的是完整线路CERT blob的SHA-256，二者不是同一个摘要。

正式身份激活/恢复/选择/业务签名需检查：字段和长度→固定正式Root/Intermediate指纹→根自签/中间签名/用户签名→有效期嵌套与pathLen→UID范围、呼号及本地公钥→双CRL。申请持钥证明另走`signEnrollment`，不能要求尚未领证者先持有效用户证书。

- Root CRL按中间证书`(sn,fingerprint)`吊销；Intermediate CRL按用户`(uid,fingerprint)`吊销。
- CRL验证`thisUpdate≤now<nextUpdate`，缓存按签发者身份隔离，号码/时间/同号内容摘要防回滚；正式域HTTP失败只能使用仍有效缓存。
- 正式`ProductionTrustManifestStore`要求version=1，以thisUpdate及TBS摘要防回滚，只引用已授权根，过期不用于授权。不能照搬旧通用清单“自增根/陈旧继续授权”的描述。
- 同一呼号能否获多张独立设备证书、UID如何分配、撤销是否可逐实例处理，需要核对CA/服务端政策；仅看iOS源码不能确认。

证据：`DirectVoiceIdentity.swift:116,192,228,302,397`；`Enrollment/EnrollmentIdentityValidation.swift:54,89,157`；`ValidatedEnrollmentIdentityProvider.swift:14`；`ProductionTrustManifestStore.swift:63`；`APRS/FMOV4CRL.swift:267,336`。

## 3. 申请协议

```text
冻结requestId、呼号、公钥、声明及材料摘要
→ 创建申请（此操作本身不签名）
→ 每份材料：取独立challenge→验证challenge→签持钥证明→上传
→ submit：独立challenge与签名
→ status轮询：每次独立challenge与签名
→ 批准后identity领取：独立challenge与签名
→ 身份包与双CRL校验通过→激活
```

申请操作TBS：

```text
["FMO-PKI", 1, "applicantAccess", origin, purpose, requestId,
 enrollmentDigest, operationDigest, challengeId, nonce, expiresAt]
```

客户端重建摘要并检查origin/用途/目标/未过期；当前没有固定最大challenge TTL的额外本地检查。回执丢失先查状态，只补传缺失材料。批准、签发处理中、delivered、下载和本机激活不应混成一个“成功”。

轮询逐步等待约5、10、20、40秒并带1–1.15抖动；随后上限约60–69秒，服务端更长等待优先，错误路径有尝试上限。后台与材料编辑暂停对应轮询。对Passport建议由手机/Web承接材料及申请业务，但这属于后续分工决策，不代表当前App已支持设备代理申请。

证据：`Enrollment/EnrollmentProtocol.swift:72,126`；`NativeEnrollmentModel.swift:44,104`；`EnrollmentClient.swift`及`FMOLinkTests/Voice/EnrollmentNetworkTests.swift`。

## 4. SAS认证与MQTT会话

每次CONNECT（包括重连/换服）重新生成并签名proof：

```text
["FMO", 4, "serverAuthorizerReqHttp",
 serverUID, uppercaseServerCallsign, serverUID, role,
 targetHost, mqttPort, serverCertificateFingerprintBytes,
 timestampSeconds, userTBSFingerprintBytes]
```

两次serverUID是当前实现的一部分，不能擅自去重。拨号地址`dialHost`、鉴权目标`targetHost`与TLS SNI是不同角色，不应合并成一个自由字符串。

| CONNECT字段 | 当前实现 |
| --- | --- |
| clientID | `fmoc-ios-<uid>-<installSuffix>`；Passport需定义自己的稳定实例后缀/客户端前缀 |
| username | 规范化呼号 |
| password | 无padding Base64URL(JSON)，含Intermediate/User证书包、目标身份/地址/端口/role、指纹、秒时间戳及proof签名 |
| TLS | 按Profile显式配置；SNI取targetHost；不自动把SAS失败降级为匿名 |
| 匿名模式 | 用户显式选择；username/password均nil，不签CONNECT，但仍要求有效本机身份 |
| MQTT | 3.1.1、cleanSession=true、keepalive30s、ping15s，连接/操作超时各10s |

服务端proof时间窗在旧计划写±120秒，但本轮只读客户端不能独立证明服务器当前强制规则。不能缓存一份proof供多次重连长期复用。当前会话每至多300秒或更早的授权截止点刷新身份；失败断开。PTT仅检查已有授权截止，不在实时链路等待CRL下载。

| 通道 | SUB/PUB与行为 |
| --- | --- |
| RAW | `FMO/RAW`，QoS0，发送non-retained；RAW接收没有同其他通道一样显式拒绝retained |
| QSO | SUB `FMO/QSO/UID/<本机十进制UID>`，PUB到对端UID；QoS0/non-retained |
| 资料 | `FMO/TELE`、`FMO/USER_RIG`，QoS0/non-retained |
| 队列 | RAW newest4；QSO oldest32，溢出停用QSO；每种资料newest32 |
| SUBACK | RAW成功才进入守听；附加通道订阅失败不停止RAW |

证据：`Authentication/DirectVoiceAuthentication.swift:348,369`；`Transport/FMOMQTTTransport.swift:53,108`；`Session/DirectVoiceSession.swift:74,105,219,430`。

## 5. RAW音频包

全体多字节整数为小端，头64字节，整包≤1400字节。

| 偏移 | 字段 |
| --- | --- |
| 0 | version u16=1 |
| 2 | vendor u32；接收0…0x2FFF，iOS发送默认0x2000、限定软件范围 |
| 6 | UID u32 |
| 10 | callsign[12]，大写ASCII字母/数字/`-`，零填充，可占满12字节 |
| 22 / 26 | streamBeginUTC / timestamp，分别u32，UTC毫秒低32位 |
| 30 | 完整包长度u32 |
| 34 | frameCount u16且>0 |
| 36 | 帧区域IEEE CRC32 u32 |
| 40 / 41 | signalMeter u8 / serverUID u32 |
| 45 | reserved[19] |

每帧为index u16、transportLength u16、reserved u32、codec u8、codedLength u16、reserved[5]、payload。index在每包从1开始；transportLength=16+payload长度，codedLength=8+payload长度。CRC只覆盖offset64以后的帧区，反射多项式0xEDB88320，初末异或0xFFFFFFFF。

**没有通用跨包seq、逐包签名或头CRC。** 当前接收器不按timestamp去重/乱序恢复，包内UID是声明值，自身UID回环忽略，serverUID没有接收匹配验证。不能把CRC、合法UID格式或已连MQTT解释为对端逐包身份认证。

证据：`Protocol/FMORawPacket.swift:23,48,133`；`Protocol/FMOCRC32.swift:3`；`Session/DirectVoiceSession.swift:338,483`。

## 6. Codec、缓冲与仲裁

| 项目 | Opus（流畅） | RADPCM（高保真） |
| --- | --- | --- |
| 采样 | 8kHz、单声道Int16 | 同8kHz输入 |
| 每帧 | 320样本/40ms | 640样本/80ms，编码328字节 |
| 聚合 | 首帧立即，后续至多5帧/200ms且受1400字节约束 | 首帧立即，后续至多3帧/240ms且受包长约束 |
| 发送参数 | VOIP、complexity4、VOICE、AUTO码率、VBR/constrained VBR | 每PTT重置，IMA编码，帧内恢复点/step与u16内部序号 |
| 接收 | 按帧自动识别；需要时重置decoder | 验328字节、step≤88等；内部seq不校验连续性 |

不能把旧“聚合≤200ms”无差别套在RADPCM。Opus bridge设过SUPERWIDEBAND上限也不会使8kHz输入变成高采样率。

接收RADPCM拆成两块40ms；静音仍解码。Session播放缓冲10块，播放器10块/400ms，积压过量清旧播新。没有实际接入的启动水位/跨包丢失检测/PLC链路；存在`concealLoss()`函数不代表已实现丢包补偿。

本地仲裁为单路半双工：约1500ms占用超时；同UID新流可接纳；其他UID按流起始时间（0…2000ms内早者）及同毫秒更小UID仲裁，用有符号差处理32位回绕。发射输给远端则停发进入busy，该到达包不立即解码。约1500ms无新包恢复listening；旧注释1.55s不等于代码计时值。

iOS后台守听失败退避3/6/12/24/48/60秒，最多6次，成功清计数，回前台即时尝试；这不是ESP32的任务/功耗配置。

证据：`Codec/LibOpusCodec.swift:23`；`scripts/opus-bridge/OpusBridge.c:3`；`Codec/FMORADPCMEncoder.swift:3`、`FMORADPCMDecoder.swift:20`；`Session/FMOVoiceRouteArbiter.swift:24`；`DirectVoiceSession.swift:250,338,404`；`Session/BackgroundListeningPolicy.swift:8`。

## 7. APRS-IS与签名广播

- 默认TCP `asia.aprs2.net:14580`；V4网络会话过滤`u/APFMO4/APFMO1`。
- 开广播时登录带公开算法生成的PASSCODE；关闭用`pass -1`。改变权限必须重连，不能只改内存开关，否则服务端可能静默丢包。
- 登录15秒超时，重连退避1/2/4/8/15/30/60秒；严格CRLF，完整行含CRLF≤512字节，出站也必须检查。
- `APFMO4`承载签名V4广播；`APFMO1`为无签名展示文字，不能作为认证依据。消息/远控的`APFMO0`属于遗留独立会话，不是当前首页已开放功能。
- 当前发送ONLINE/CQ/BEACON；不逐PTT发送VOCAL。

ONLINE/CQ的TBS：

```text
["FMO", 4, type, callsign, ssid, latitudeText, longitudeText,
 SHA256(fullCertificateBlob), serverUID, floor(unixSeconds/600)]
```

BEACON在共同字段后依次追加频率，以及存在的高度、设备、天线，最后timeSalt。可选字段顺序不能改变。接收端试当前桶和前后各一桶，不等同严格±600秒。

入站类型包括CQ/OMCQ/VOCAL/ONLINE/BEACON/STATION/JOINT/EVENT；先解析再验Root→Intermediate→User→CRL→报文签名/时间盐及必要JOINT/EVENT关系，最后才入库/展示。通用网络验证器可保留“吊销信息陈旧/不可用”的降级记录；正式身份授权要求有效双CRL，这两者不能混为一种策略。

事件缓存默认24小时/200条，pending JOINT 256条/20分钟，seen摘要1024条；这些iOS上限不是Passport默认预算。首页再过滤自有/当前互信服务器，UID和证书指纹共同约束；BEACON按发送者信任域及距离处理。

证据：`APRS/APRSISProtocol.swift:19`、`APRSISLineFramer.swift:11`；`FmoNetworkModel.swift:35,201,261`；`APRSBroadcastSettings.swift:278,382`；`FMOV4Verifier.swift:58,159,175`；`FMOV4NetworkStore.swift:107`；`FMOV4FeedAdmission.swift:5`。

## 8. QSO

线路264字节，小端：版本offset0=1、类型offset8（请求0/确认1）、发送者UID offset12、Unix秒i64 offset16；呼号[16]@24、网格[8]@40、中继[32]@48、管理员[8]@80、评论[64]@88，其余零，文本需留NUL。没有CRC、签名或线路交易ID。

- 单个有效话轮严格>2秒；最近六轮，重叠或间隔>20秒清历史。双方次数/时长资格、末三轮本机—对方—本机及UID规则共同决定自动请求，不能简化成一次PTT即记QSO。
- 自动请求按本机身份与对方呼号抑制UTC当天重复；此日抑制刻意不按Broker分隔。
- 自动确认需要双方最近五分钟各有有效话轮；等待确认60秒，对端UID和完整呼号匹配。
- 入站仅本机topic、非retained、时间±300秒、非自身；最多20待请求、32待保存结果。
- receipt key绑定本机身份/Broker/类型/UID/时间；另用payload摘要判断重复/冲突。记录与receipt同事务保存，删除可见记录后保留receipt维持幂等。
- 断线清等待和资格，不自动重播请求；保存失败只重试本地保存。QoS0下“已确认/保存”不等于双方最终落盘。

证据：`Features/QSO/FMOQSOMessage.swift:13`、`FMOQSOQualification.swift:24`、`FMOQSOExchange.swift:104,138`、`FMOQSOStore.swift:51,58`。

## 9. TELE与USER_RIG

共享开关独立且默认关。TELE有有效频率即可，连接约4秒后首次尝试、随后60秒一次，两频率字段当前填同一单频。RIG在每次PTT开始时异步发布，需频率、六位网格、型号、天线和高度完整；文本≤15 UTF-8字节，超长跳过而非截断，不阻塞首个RAW包。

- TELE：33字节，version2@0、CRC@1、UID@5、呼号[12]@9、秒时间@21、两个Float32@25/29。
- RIG发送56字节，version1@0、CRC@1、UID@5、网格[7]@9、型号[16]@16、频率Float32@32、天线[16]@36、高度u32@52。
- 均小端，CRC覆盖offset5到尾；另接收57字节兼容布局及其额外CRC异或规则，不能仅支持发送布局就声称全接收兼容。
- 缓存当前连接最多128UID/300秒；TELE拒旧时间/同时间冲突，RIG无时间与序号；按近期唯一UID/完整呼号观察关联，不把CRC/关联当验身。

证据：`Protocol/FMOStationPackets.swift:30,105,144`；`Session/FMOStationExchange.swift:23,58,133`；`FMOStationCache.swift:10`。

## 10. 签名频率与固件边界

| 场景 | 客户端业务私钥调用 |
| --- | --- |
| 每次SAS CONNECT/重连/换服 | 1次 |
| ONLINE / 手动CQ / 手动BEACON | 每条1次，条件满足才发送 |
| 申请材料上传/提交/每次状态轮询/领取 | 每操作1次，另有challenge交互 |
| Push登记与偏好更新 | 每请求1次，首次开启通常2次；属于手机业务 |
| PTT/RAW音频帧/心跳/QSO/TELE/RIG | 不逐包做Ed25519签名 |
| 证书链、报文及CRL验证 | 公钥验签，不调用自己的私钥 |

因此BLE代签不在音频实时热路径上；但正式设计仍要处理多个业务请求争用、超时取消、重连新挑战、时间同步、授权截止及手机失联，不能把实验的一条任意文本签名直接视作生产RPC。
