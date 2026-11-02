# RF Link BLE 手咪：当前对接入口

当前正式方向为 **仅 IMA-ADPCM**。固件1.4.0继续只声明format=2，移除BLE PCM与Opus，不兼容只支持PCM的旧App。已接入原ADPCM格式的App仍按minor=1/format=2、20ms、100字节接入。

| 文件 | 用途 |
| --- | --- |
| [当前音频合同](adpcm-only.md) | 唯一音频模式、Info/HELLO、包格式、流控、时钟及迁移 |
| [音频诊断](audio-diagnostics.md) | 可选audio.stats、计数范围及同条件频谱对照 |
| [App对接指南](app-integration.md) | 配对、业务编排、音频路由和生命周期 |
| [基础业务合同](protocol-v1.md) | GATT、JSON分片、请求/事件、PTT、状态与错误；其历史PCM音频部分已由当前合同替代 |
| [服务器/电台资料](station-metadata.md) | 服务器、讲话人、TELE/RIG、位置/距离、代次与过期 |
| [公开协议向量](vectors.json) | 当前HELLO、100字节ADPCM及24字节Flow |
| [ADPCM编解码向量](adpcm-vectors.json) | 输入PCM、独立块及期望解码结果 |
| [参考编解码器](../../../tools/handmic-simulator/adpcm.py) | 字节顺序、索引表、初始状态和末尾填充 |

产品界面与按键见[UI v3](../../superpowers/specs/2026-11-02-rf-link-ui-v3-design.md)。发给App开发者时提供本目录文件及参考实现；旧实验UUID/168字节包不适用。

历史材料：[ADPCM初始扩展](adpcm-v1.1.md)、[Opus试验](opus-v1.2.md)、[Opus真机测量](../../development/rf-link-opus-device-benchmark-2026-11-02.md)保留研究记录，不代表当前固件继续支持PCM或Opus。
