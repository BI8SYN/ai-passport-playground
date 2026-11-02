# RF Link UI v3与音频调度优化验收（2026-11-02）

用户确认无频谱布局并要求设置保留开关。本轮完成实现、主机回归、产品构建与归档；未访问串口、未复位/烧录设备、未修改App。

## 变更

- [UI v3](../superpowers/specs/2026-11-02-rf-link-ui-v3-design.md)：默认关闭频谱，呼号/位置重排、秒级讲话计时、最近通话与断连引导。设置七项、五行滚动，频谱短按OK切换并立即保存；NVS缺失值为关闭，保存失败保留原值并提示。Web后端同步开关与持久化，已运行实例及桌面ZIP未覆盖。
- 真正停止关闭模式的分析窗口维护、复制、FFT与柱/峰步进；重启分析和换流清除旧窗口。开启保留原频谱布局，作为同条件对照。
- NimBLE音频/Flow回调不等待core锁，仅做校验和短临界区有界复制。工作任务3KiB栈、优先级5；音频入口6帧/120ms，满队列拒绝并计数；Flow单槽覆盖优先。处理时复核连接代次、授权、sid/stream与年龄，超时检查先执行，租约保留回调到达时间。
- UI字符串/位置距离格式化移出core锁；静态页仅因可见字段变化重绘，音频计数和频谱变化不再驱动设置页重绘。
- [audio.stats诊断合同](../spec/rf-link-handmic/audio-diagnostics.md)：锁等待、队列溢出/过期/拒绝、最大驻留、到达/播放间隔与Codec写入耗时。区分开机累计和最近流，正常结束排空也可能产生underrun，不当成链路丢包率。
- 固件Info fw1.4.0；major1/minor1、format2、100字节/160采样/20ms、60ms起播、核心32信用、积压裁剪、PTT与超时参数不变。audio.stats为可选扩展，不需要App更改音频协议。

## 验证

| 检查 | 结果 | 范围 |
| --- | --- | --- |
| `./tools/dev test` | PASS，36项 | 产品设置事务/回滚/PTT保护、IMA与核心、实际hm_audio.c入口/锁竞争/Flow优先/超时租约/频谱停止测试，核心ASan/UBSan |
| `./tools/dev preview rf-link-handmic` | PASS | 真实LVGL，两模式×四主题×全部页面、滚动菜单、长呼号、位置缺失、离线身份清理及字形；58固定对象、40KiB池峰值26296字节 |
| 静态页刷新回归 | PASS，800帧 | 设置/主题/资料/历史×频谱两模式×100帧；忽略非可见音频变化，保留讲话人/提示/分钟年龄等可见变化刷新 |
| `./tools/dev simulator-build` | PASS | 同源核心/UI/字体与后端构建 |
| 容器backendtest.py | PASS，10项 | ADPCM双向、PTT、配对/状态、时钟、流边界与新频谱开关缺省/双向保存/重启载入 |
| AudioWorklet回归 | PASS | 短尾、清空及8kHz麦克风帧 |
| `./tools/dev gate` | PASS | 上游完整18项host tests及固件门禁 |
| `./tools/dev build rf-link-handmic` | PASS | ESP-IDF5.5.3/C3，分区、镜像与匹配ELF归档校验；独立重验归档PASS |
| 独立复核 | APPROVE | 修复静态页过度重绘后复审，无Important/Critical问题 |
| 本版设备测试 | NOT RUN | 未烧录；任务栈、空闲堆、实体按键/NVS断电保存、持续通话效果及频谱对照待实机 |

测试桩验证实际音频C代码行为，但不模拟ESP32实时调度、RF或Codec。Web使用本机时钟/音频，并未提供伪造的实体audio.stats数据。主机测试不能证明真实卡顿已经解决。

## 精确产物

归档：`artifacts/rf-link-handmic/f242b593050d16f290376d5a77ab2954e008ada7053305ee8dda4a85c6743c8a/`，包含匹配full/app/bootloader/分区表/ELF/MAP/flash_args。

- full SHA256：`f242b593050d16f290376d5a77ab2954e008ada7053305ee8dda4a85c6743c8a`。
- ELF SHA256：`f28ab84698282e56066aeca27abf0032646fb7d192e61ac8a405208f871fc608`。
- app：2121360字节，SHA256 `9d8d9f9376c4f25f8124a79a86151297f676e1911ee59274b6f01ca3be84251f`。

`full.bin`为构建默认factory布局、0x0合并镜像；当前实机已有双OTA布局，后续获准写入必须重新核对设备分区/活动槽，并按实际应用区分段更新。不得把默认factory应用偏移0x10000套用到当前设备；不可用本版整包覆盖现有NVS/分区。当前设备仍是上一版77cb3a06，仅ADPCM；本版没有烧录。

证据：`artifacts/rf-link-ui-v3-validation/`保存测试、构建、预览、门禁、后端测试、归档重验及源码摘要；产物不提交。

## 后续真机对照

1. 明确授权该版写入后，核对SSH设备与OTA活动槽；保留绑定、音量和主题，验证写入与启动。
2. 确认频谱默认关闭；设置内双向切换、重启持久化、菜单/长呼号/四主题显示和PTT释放保护。
3. 同一App/网络/讲话素材/音量，分别开启和关闭频谱，保持音频合同不变；每条流开始和结束以新请求id采集rx与playback，记录sid/stream、结束原因、听感及启动堆/任务栈证据。
4. 比较队列等待、溢出、过期、到达节拍与播放节拍；区分正常排空欠载。若仍卡顿，继续依据统计分离App到达与设备播放调度，不直接归因于频谱。

## 同日后续：用户授权“烧录”

已通过SSH主机10.11.26.88、唯一USB端口`/dev/cu.usbmodem11101`写入上述精确归档。远端Docker校验完整包、现场读取设备分区表与otadata，确认分区表SHA256为`3493ccd429f3b91fcb65ea3ba5823d18626ff03344c756c2c15a4dd6a99cb80a`，活动ota_0位于0x20000、大小0x3C0000、有效序列1。只写入2121360字节应用区，独立verify_flash摘要匹配PASS。未备份、未全片擦除、未写eFuse，NVS、配对、音量/主题、分区表和otadata均未改写。

20秒有界启动观察确认从0x20000加载，ELF前缀f28ab8469匹配，Codec 8kHz/16bit/单声道、BLE与UIv3 Ready。现场已存音量100、主题phosphor_green得到保留，新增频谱缺省关闭（spectrum=0）。10秒健康快照heap=45624字节，主任务栈余量7600字节；此为启动观察，不是持续音频负载或入口/录音/播放任务栈峰值测量。观察窗口无panic/异常复位，串口已关闭。

写入、独立校验、启动观察PASS；真实App重连、显示/按键、开关断电保存与语音改善仍待用户验收。当前设备已更新为本版f242b593，不再是上文优化前77cb3a06。原始证据见`artifacts/rf-link-ui-v3-device/`的flash.log、flash-receipt.json与startup.log。
