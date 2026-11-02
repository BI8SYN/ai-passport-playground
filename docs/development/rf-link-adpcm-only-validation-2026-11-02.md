# RF Link 仅ADPCM版本验证（2026-11-02）

用户决定只使用ADPCM并移除原PCM。当前[ADR0004](../adr/0004-adpcm-only-audio.md)与[App合同](../spec/rf-link-handmic/adpcm-only.md)已更新。本轮实现、测试和构建，不修改App，不提交/发布，不烧录。

## 变更

- 固件标识1.3.0，协议major1/minor1，Info只声明formats=[2]，frame_samples=160、audio_bytes=100。HELLO拒绝PCM/Opus和minor0的format2；原ADPCM字节格式不变。
- BLE音频严格100字节，20ms/160采样，接收槽只存编码包，播放任务解码；内部I2S/Web Audio仍是PCM。
- 删除Opus源文件、库依赖、codec.get、启动压力逻辑及RF Link benchmark构建入口。压力测量与旧源码留存于研究文档及本地`artifacts/experiments/opus-source-2026-11-02.tgz`，不参与产品构建。
- 两音频任务恢复4KiB，设备与Mac桥mic传输队列收敛为6帧/120ms。60ms一次起播、信用、PTT、资料和NVS行为保持。
- Web后端/服务端/Mac桥只支持ADPCM，内部浏览器80采样小块依然合并为160采样后编码。已发桌面ZIP未覆盖，运行实例未重启。
- 当前公开向量更新为ADPCM，原PCM向量保留为历史文件；App对接入口不再宣传回退或Opus能力。历史协议与实验文档增加退出当前产品的标记。

## 验证

| 检查 | 结果 | 范围 |
| --- | --- | --- |
| 能力收敛用例在旧代码执行 | 预期失败 | 原Info声明多格式，不满足仅[2]约束 |
| `./tools/dev test` | PASS，34项 | 含IMA编解码/向量、PCM/Opus拒绝、20ms时钟/信用/PTT和资料回归；核心C测试启用AddressSanitizer与UndefinedBehaviorSanitizer |
| `./tools/dev simulator-build` | PASS | 当前核心、IMA、真实LVGL构建 |
| 容器内backendtest.py | PASS，9项 | 全部改为ADPCM，含双向音频、状态、配对、时钟和历史 |
| Mac桥编译及签名校验 | PASS | macOS13 arm64，输出adpcm-only目录，未启动 |
| `./tools/dev preview rf-link-handmic` | PASS | 全页面/字形/四主题、150帧固定对象、100次页面切换；预览入口协商同步format2 |
| `./tools/dev gate` | PASS | 上游完整门禁 |
| `./tools/dev build rf-link-handmic` | PASS | C3最终构建、分区/镜像/匹配ELF/归档验证；锁文件不再含esp_audio_codec |
| 独立复核 | APPROVE | 无Important/Critical；核对包长、采样、旧流检查和模拟器迁移 |
| 真机 | NOT RUN | 本版未烧录，4KiB任务栈及实际音频仍待验证 |

核心测试输入缓冲已按320字节PCM统一，移除80采样旧假设；ASan/UBSan通过。这里的PCM仅为编码器输入，不是保留BLE PCM模式。

## 精确产物与设备状态

归档：`artifacts/rf-link-handmic/77cb3a0613fc14574e6ad7ba6e1c92fdd46da80c9d25b4726b27c68f383823b6/`。

- full SHA256：`77cb3a0613fc14574e6ad7ba6e1c92fdd46da80c9d25b4726b27c68f383823b6`。
- ELF SHA256：`a4a8bdc3967cc7f5f58d8d5e41dced4deee1d0a903facce5acb9d459d955c1af`。
- 应用2115184字节，SHA256 `b2bb283a5021b3ae82dbd5cb738bc08f243034dc401855a0d5d722d8a9855d60`。

设备仍为上轮测试后恢复的5b231cc6 ADPCM/PCM兼容镜像，本版尚未写入。后续获准写入时须重新核对SSH设备的OTA分区及启动槽，保留配对/设置，不得把默认factory偏移0x10000用于当前双OTA设备。未测本版空闲堆，不将源代码回收量当成真机RAM实测。

## 同日后续：用户授权烧录

用户明确要求“烧录一下”，已通过原SSH主机10.11.26.88的`/dev/cu.usbmodem11101`写入上述精确镜像。远端已有Docker工具链验证整个归档、重新核对分区表SHA及有效OTA元数据后，仅更新ota_0的0x20000，2115184字节，独立verify_flash摘要比对PASS。不备份、不全片擦除，NVS、配对/主题/音量、分区表及otadata未改写。

启动观察确认从0x20000加载，ELF前缀a4a8bdc39与本版一致，Codec 8kHz/16bit/mono、BLE初始化及UI Ready正常，主题phosphor_green、音量90。10秒健康值heap=50036字节、主栈余量7332字节；观察窗口未见panic/异常复位。相比此前兼容版同类启动快照41200字节增加8836字节，此为无实际通话负载的启动空闲堆，不代表持续音频余量。

当前设备已是仅ADPCM版；烧录/校验/启动检查PASS，真实App协商与音频、4KiB音频任务栈峰值仍待验收。本地证据位于`artifacts/rf-link-adpcm-only-device/`，包括Flash日志、写入回执和启动日志。串口观察结束后释放端口。
