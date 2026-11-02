# RF Link 当前音频合同：仅 IMA-ADPCM

2026-11-02用户决定只使用ADPCM，移除BLE PCM及Opus。当前固件版本1.3.0，线路协议仍为major=1/minor=1，保留原format=2的字节格式；已接入ADPCM的App无需更改音频格式。只支持PCM的旧App不能使用此版，必须更新；不提供静默回退。

## 协商

GATT UUID、系统配对、Control分片、PTT、状态及TELE/RIG沿用[基础业务合同](protocol-v1.md)，音频以本文为准。Info关键字段：

```json
{"product":"rf-link-handmic","fw":"1.3.0","major":[1],"minor":1,"formats":[2],"sample_rate":8000,"frame_samples":160,"audio_bytes":100,"credit_window":32}
```

Info另含device_id、control_bytes、pairing。App订阅Control TX、Mic Audio、Device Flow后发送：

```json
{"v":1,"id":1,"sid":0,"op":"hello","p":{"major":1,"minor":1,"format":2,"write_max":244}}
```

返回新的sid，结果r包含`major=1,minor=1,format=2,rate=8000,samples=160,frame_ms=20,audio_bytes=100,value_max=M,credit_window=32,audio_ready=true`。M=min(App write_max, ATT_MTU-3,244)，须≥100；控制HELLO及响应仍先用≤20字节的分片。

format=1/3或未知格式均返回unsupported_format；format=2但minor<1也拒绝。App minor>1时协商结果仍为1。GATT音频值只接受100字节，PCM176字节和Opus56字节均拒收，不刷新有效音频租约。

## 每包100字节

8kHz、单声道、每包160采样/20ms，独立IMA/DVI 4bit块。正常50包/秒，含音频头应用层码率40kbps；不含Flow/ATT/BLE链路开销。

| GATT偏移 | 字段 |
| --- | --- |
| 0 | uint8 major=1 |
| 1 | uint8 flags，bit0 discontinuity，其余0 |
| 2 | uint8 format=2 |
| 3 | uint8 sample_count=160，即0xA0 |
| 4–7 | uint32 LE sid |
| 8–11 | uint32 LE stream_id |
| 12–15 | uint32 LE seq，从0开始每20ms递增1 |
| 16–17 | int16 LE predictor，也是输出第0个采样 |
| 18 | uint8 step_index，0–88 |
| 19 | reserved=0 |
| 20–99 | 159个4bit码，低半字节先；最后字节仅低半字节有效，高半字节必须0 |

每包重新加载预测值和索引，解出恰好160采样。丢包不会污染下一包状态。非法index/reserved/填充、重复、过期或旧sid/stream包拒收。不是Apple ima4、MS ADPCM或WAV；无CRC/Base64/JSON音频包装，也不做应用层音频分片。

标准step/index表、逐项移位算法及初始索引选择见[参考编解码器](../../../tools/handmic-simulator/adpcm.py)。[adpcm-vectors.json](adpcm-vectors.json)含输入PCM/84字节块/期望解码PCM；[vectors.json](vectors.json)现已更新为当前HELLO、100字节音频及Flow公开向量。旧PCM向量仅作历史保留在vectors-v1.0-pcm.json。

## 流控和时钟

- seq、Flow的next_seq/grant_end均以20ms帧为单位。窗口最大32帧，上界exclusive，重复Flow不累加信用；收到首个信用前不产生该流采样槽。
- 按20ms单调时钟平滑发送；每推进2帧主动更新信用，活动/待批准PTT时双方每100ms保活，全空闲500ms。
- 每条stream只在首个有效音频到达时建立一次60ms起播等待。断续前跳和积压裁剪不重设60ms；过期消费时刻拉到now，避免追赶旧时钟。
- 超过6帧积压时丢弃最旧音频，保留最新3帧。32帧只是授权/存储上限，不是要求缓存640ms。发送端待发音频最多120ms，不追发过期语音。
- 缺包按每20ms补160个零并推进信用；首包/有效音频300ms超时、mic信用500ms、会话3秒、最长TX60秒规则保持。
- 正常末尾不足160采样可在PCM端补零再编码；松键、断连、紧急停止立即丢弃待发/不足帧。新流首包置discontinuity，不能每包都置位。

## App接入和产品边界

下行：网络解码→8kHz PCM FIFO→每160采样编码84字节→拼16字节头→每20ms依信用提交100字节。上行：校验100字节包→独立解码160采样→现有网络编码器。不能将现有网络RADPCM包直接透传到BLE。

内部I2S、Web Audio和网络编码器入口仍使用PCM；移除的是BLE上的PCM传输模式。App仍负责网络身份、认证及网络编解码，设备不持业务证书/私钥。

固件不再包含Opus依赖、实例、压力测试启动逻辑或codec.get接口；原stats.get继续提供流统计。两音频任务恢复4KiB栈，接收槽只存100字节编码包，在播放任务解码。

Web模拟器及Mac桥源码同步只声明[2]、只收发100字节音频。浏览器采集仍以80采样PCM小块输入模拟器，由后端合并为160采样后编码；试听均为解码后的PCM。已发出的旧ZIP不会自动更新，需用新源码重建并重启后端/桥，刷新网页不足以更新二进制。
