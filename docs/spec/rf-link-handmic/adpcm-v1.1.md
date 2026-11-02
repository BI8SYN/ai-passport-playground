# RF Link BLE 1.1：IMA-ADPCM 对接说明

> 历史合同/试验记录：当前固件已收敛为[仅IMA-ADPCM](adpcm-only.md)，不再支持BLE PCM或Opus。本文旧模式、回退和测试构建说明不用于当前版本。

2026-11-02：用户确认增加压缩。本文为[1.0协议](protocol-v1.md)的可选能力扩展；选择format=2时以本文音频参数为准，GATT UUID、JSON分片、身份、配对、PTT、流生命周期、TELE/RIG等保持原合同。实现与主机验证不等于真机互通验收。

## 1. 两种音频模式

| 项目 | format=1，兼容旧App | format=2，推荐新App |
| --- | --- | --- |
| 编码 | PCM S16LE | IMA/DVI ADPCM，4bit |
| 采样 | 8000Hz，单声道 | 8000Hz，单声道 |
| 每帧解码采样数 | 80 | 160 |
| 每帧时长 | 10ms | 20ms |
| 每个GATT音频值 | 176字节 | 100字节 |
| 音频体 | 160字节PCM | 84字节独立ADPCM块 |
| 含16字节头的应用层码率 | 140.8kbps | 40kbps |
| 每秒包数 | 100 | 50 |

以上码率不含ATT、BLE链路、Flow和控制消息。84字节块本身为33.6kbps，32kbps是仅计算4bit采样的近似。只影响手机与手咪之间的音频；网络Opus/RADPCM策略仍由App决定。

下行：网络解码→8kHz PCM→App IMA编码→BLE→Passport IMA解码→PCM播放。
上行：Passport采集PCM→IMA编码→BLE→App IMA解码→现有网络编码器。不能把BLE IMA块直接当作网络RADPCM包。

## 2. 能力发现与协商

新固件Info声明`minor=1,formats=[1,2],fw="1.1.0"`；major仍为1，JSON外层v和音频头major也仍为1。Info中的旧`frame_samples=80,audio_bytes=176`是PCM基线/最大包长，不是format=2参数。实际模式以HELLO响应为准，不从Info这两个旧字段推断。

订阅3个上行特征后发送（初始控制分片仍≤20字节）：

```json
{"v":1,"id":1,"sid":0,"op":"hello","p":{"major":1,"minor":1,"format":2,"write_max":244}}
```

成功响应示例：

```json
{"v":1,"id":1,"sid":305419896,"ok":true,"r":{"major":1,"minor":1,"format":2,"rate":8000,"samples":160,"frame_ms":20,"audio_bytes":100,"value_max":244,"credit_window":32,"audio_ready":true}}
```

`value_max`仍为`min(App write_max,ATT_MTU-3,244)`；format=2要求≥100，format=1要求≥176。100字节是整个GATT值，不是音频块长度；无应用层音频分片。`frame_ms/audio_bytes`是新增响应字段，旧App应忽略未知字段。

- 只有minor≥1且format=2才可选择压缩；minor=0请求format=2报`unsupported_format`。
- App先查Info，支持2时优先申请2；只有1时申请1。若压缩被明确拒绝，可用递增的新请求id重做PCM HELLO，重新同步radio状态；不能把格式错误、断连、超时都当成可静默回退的信号。
- 同id同内容重试仍返回原sid/结果；切换格式必须新HELLO，停止旧流、清FIFO、重建sid。会话中两方向使用同一格式，不支持逐包或逐stream自行切换。
- 收到不符合协商格式的音频，即使其长度在另一格式下合法也必须丢弃。

## 3. format=2音频头：16字节

| 偏移 | 类型 | 内容 |
| --- | --- | --- |
| 0 | uint8 | major=1 |
| 1 | uint8 | flags bit0=discontinuity，其余为0 |
| 2 | uint8 | format=2 |
| 3 | uint8 | sample_count=160，即0xA0 |
| 4–7 | uint32 LE | 当前sid |
| 8–11 | uint32 LE | 当前stream_id |
| 12–15 | uint32 LE | seq，每20ms采样帧递增1，从0开始 |
| 16–99 | bytes[84] | 本包独立IMA块 |

注意：seq为`0,1,2,…`，不是`0,2,4,…`。采样帧被主动丢弃也占用seq；同一流不能为旧音频重新编号。首包带discontinuity；后续仅在丢弃积压时置位。不能每包都用标记代替正常消费时钟。

## 4. 84字节块精确定义

以下偏移相对于音频体起点，即GATT偏移16：

| 块偏移 | 内容 |
| --- | --- |
| 0–1 | int16 LE初始predictor，也是解码输出的第0个采样 |
| 2 | uint8初始step_index，范围0–88 |
| 3 | reserved=0 |
| 4–83 | 159个4bit码，低半字节先、高半字节后；最后一个字节仅低半字节有效，高半字节必须0 |

解码器先输出predictor，再解码159个码，合计**正好160个采样**。不能把初始预测值再作为第一个4bit码解码，也不能将末尾填充高半字节解出第161个采样。非法长度、index>88、reserved非0、非零填充均拒收，不能推进流/刷新有效音频超时。

每包重新加载本包predictor和index，不继承上一包状态。丢失seq=N不影响N+1解码，丢包处按播放时钟补零，不能等重传。

算法采用[IMA原始参考算法](https://www.cs.columbia.edu/~hgs/audio/dvi/IMA_ADPCM.pdf)的整数移位形式：

```text
step = step_table[index]
delta = step >> 3
若code & 4: delta += step
若code & 2: delta += step >> 1
若code & 1: delta += step >> 2
predictor += (code & 8) ? -delta : delta
predictor = clamp(predictor, -32768, 32767)
index = clamp(index + [-1,-1,-1,-1,2,4,6,8][code & 7], 0, 88)
```

不能用一次乘法取整替代各项分别移位：小步长下结果可能不同。完整89项step表及参考编解码见[adpcm.py](../../../tools/handmic-simulator/adpcm.py)，固件实现见[hm_adpcm.c](../../../firmware/rf-link-handmic/main/hm_adpcm.c)。这是项目自定义的独立IMA传输块，**不是Apple ima4、MS ADPCM或完整WAV容器**；不能直接把这些API的默认输出透传。

参考编码器每包取第0个原始采样为predictor；根据第1和第0个采样差的绝对值，选择首个`step>=abs(diff)`的index，无满足项时取88，随后按参考算法逐样本编码。解码器接受0–88任一合法初始index；App可采用其他索引选择策略，但核对编码向量时须使用上述策略。

末尾不足160个采样时在PCM端补零再编码；紧急停止/松键直接丢弃不足一帧及待发帧，不补发旧音频。

## 5. 信用和时钟

Flow格式仍为24字节，`next_seq/grant_end`单位为**协商模式的一帧**。窗口仍最大32帧，exclusive上界，重复Flow不累加信用。压缩模式32帧代表640ms的在途授权上限，绝不是预缓冲要求，也不是允许向系统蓝牙队列一次提交640ms音频。

| 规则 | PCM | IMA |
| --- | --- | --- |
| 首包起播等待 | 60ms | 60ms |
| 消费节拍 | 10ms一帧 | 20ms一帧 |
| 高水位 | 超过12帧 | 超过6帧 |
| 裁剪目标 | 最新6帧 | 最新3帧 |
| 信用推进主动更新 | 每4帧，40ms | 每2帧，40ms |
| 发送端最多积压 | 120ms | 120ms，即6帧 |

任一活动流/待批准PTT时，双方Flow周期仍为100ms；全空闲500ms。初始信用等待300ms、音频中断300ms、mic信用500ms、会话3秒、最长TX60秒均保持真实毫秒，不按帧数直接倍增。

设备内部I2S仍使用8kHz/16bit/mono PCM；压缩仅改变BLE表示。App用单调时钟每20ms发送一帧100字节，正常不突发，最多补发一帧仍新鲜的音频。CoreBluetooth写队列不可用/无信用时丢弃过期音频，恢复时带discontinuity且seq前进，不能为追赶而连续发送所有旧帧。网络200/240ms聚合仍需App解码后重新平滑调度。

此前的起播修复同样适用：每个stream仅首个有效音频建立一次`now+60ms`；断续前跳/高水位裁剪不重新等60ms，过期播放期限拉到now以免追赶。当前游标标记不改变播放期限。

## 6. App接入清单与向量

1. 能力发现/HELLO保存`format,samples,frame_ms,audio_bytes`。不要在解析器、FIFO、计时器里硬编码80/10/176。
2. 下行累积160个8kHz PCM采样，编码84字节，拼16字节头，依信用每20ms提交100字节。
3. 上行按协商模式校验完整100字节包和块状态，独立解出160采样，再合并供现有网络编码器。网络流不随每个BLE包重建。
4. 同一stream的接收消费时钟每20ms推进一帧，缺包补160个零。相同/过期包不消费两次、不延长音频租约；信用按实际消费或丢弃更新。
5. 停流、换sid、换格式、松键、断连时清采样FIFO/不足帧/编码待发队列。新流首包重新带标记。
6. 用[adpcm-vectors.json](adpcm-vectors.json)核对静音、440Hz音、正负极值、半字节顺序及饱和处理。文件含输入PCM、84字节块、期望解码PCM和一条完整100字节音频包。

Python参考用法（仅作开发校验）：

```python
import adpcm  # tools/handmic-simulator/adpcm.py
block = adpcm.encode(pcm_s16le_320_bytes)
assert len(block) == 84
decoded_pcm = adpcm.decode(block)
assert len(decoded_pcm) == 320
```

联合验收至少包括：PCM旧App回归、压缩上下行、丢包后下一块正确解码、重复断续标记不饿死播放、Flow丢失恢复、写队列背压、PTT松开、断流超时、断连/换格式不播放旧音频。主机测试覆盖并不代替iOS及真实Passport的持续听感、堆/任务栈、功耗和重连测试。
