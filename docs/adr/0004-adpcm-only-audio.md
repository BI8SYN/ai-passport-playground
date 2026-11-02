# ADR 0004：RF Link BLE 音频仅使用 IMA-ADPCM

- 日期：2026-11-02
- 状态：已接受，用户明确决定“那就用adpcm，之前的pcm也可以去掉了”

## 背景

原PCM需要128kbps音频负载、100包/秒；ADPCM实现在20ms/160采样下每包100字节、50包/秒，含头应用层40kbps。Opus真机测试表明隔离编码计算可落入20ms预算，但完整固件编码器无法初始化，解码时最低空闲堆也仅约8KiB，见[测量记录](../development/rf-link-opus-device-benchmark-2026-11-02.md)。

## 决定

正式产品只保留独立块IMA-ADPCM，沿用既有format=2、major1/minor1的字节格式，固件标识1.3.0。Info只声明[2]，删除BLE PCM/Opus模式、Opus库及启动压力测试，不静默回退。内部I2S、Web Audio和网络编码接口继续使用PCM。

App仍持有网络身份并处理网络Opus/RADPCM；设备负责BLE ADPCM及硬件音频。不改变ADR0003的纯BLE配件方向、配对、PTT或资料合同。

## 影响

已接入ADPCM的App保持100字节/20ms格式；只支持PCM的旧App必须升级，收到unsupported_format不能假装路由已就绪。收发队列、信用和超时仅按固定20ms模式实现，回收Opus任务栈和依赖开销。

历史PCM/Opus文档、向量和实际测量保留并标识为历史记录；当前接入以[ADPCM合同](../spec/rf-link-handmic/adpcm-only.md)为准。新固件须分别完成构建、主机测试和真机验收，不继承旧镜像验收状态。
