# RF Link IMA-ADPCM 实施验证（2026-11-02）

用户要求实施压缩并补充App对接文档。本轮未修改iOS App、未烧录、不提交或发布，保留既有工作区修改。方案见[实施计划](../plans/2026-11-02-handmic-adpcm.md)，接口见[协议1.1补充](../spec/rf-link-handmic/adpcm-v1.1.md)。

## 已实现

- 无动态分配的IMA独立块编解码：160采样/20ms，84字节体，100字节完整音频帧；初始采样/索引、低半字节优先和末尾填充均固定。
- Info声明minor=1、formats=[1,2]；HELLO显式选择format=2，返回160采样/20ms/100字节。minor=0、format=1仍可使用原PCM；混格式、坏状态和错误长度拒收。
- 双向编解码、协商帧长的I2S读写、真实频谱，读取期间sid/stream/generation变更则不发送旧采样。
- 压缩下消费周期20ms、信用仍按帧、每2帧主动更新；高水位6帧、裁剪至3帧。真实毫秒超时及此前起播计时修复保留。
- Web C后端、Python原始试听解析、Mac BLE桥均支持100字节包；公开Python参考实现和5组字节向量供App核对。已发出桌面ZIP未覆盖，现有运行实例未重启；使用新版须重新构建/启动。

## 验证

| 项目 | 结果 | 范围 |
| --- | --- | --- |
| 新协商测试在原代码上运行 | 预期失败 | 原实现拒绝format=2，确认新功能用例能发现能力缺失 |
| `./tools/dev test` | PASS，34项 | IMA固定向量/非法块/极值、100组确定性随机C与Python互通、完整包向量、PCM旧模式及IMA协议/PTT/时钟/信用回归 |
| `./tools/dev simulator-build` | PASS | 同一核心和编解码器与真实LVGL一起编译 |
| 容器内`backendtest.py` | PASS，9项 | 新增压缩speaker PCM输出与mic压缩通知；原8项保留 |
| Mac桥编译/签名校验 | PASS | macOS13 arm64，独立输出到adpcm-validation目录，未启动 |
| `./tools/dev gate` | PASS | 上游完整门禁，与正式产品构建分开 |
| `./tools/dev build rf-link-handmic` | PASS | 最终源文件构建、分区/镜像/匹配ELF及导出归档验证 |
| 真机 | NOT RUN | 未打开串口、复位或写入 |

模拟器首次回归发现按`now+10ms`重设输出期限会因tick抖动降低PCM输出频率；已改为保留PCM每tick输出、IMA按固定期限20ms推进，原时钟压力测试和新增压缩测试通过。

## 精确交付

归档：`artifacts/rf-link-handmic/5b231cc6af0d6c15ad279420e7771303d3477d469939badce965e3fe7a524e67/`。

- 完整镜像SHA256：`5b231cc6af0d6c15ad279420e7771303d3477d469939badce965e3fe7a524e67`。
- 匹配ELF SHA256：`39d0634ff2faa396f90ad4ce478ac59f856197767b82fbe5c37b52c7efb408ac`。
- 镜像/应用区/分区偏移与每文件哈希以同目录`manifest.json`为准。完整镜像用于0x0且可能覆盖NVS；后续获准更新时，应核对设备和实际分区，兼容情况下仅更新应用区保留配对/设置。

尚未验证：真实iOS协商与声音、C3实际CPU/堆/任务栈余量、连续通话、丢包恢复听感、锁屏后台和重连。音频槽扩为160采样会增加约5KB静态RAM；编码器本身不分配堆。不能从主机通过推断真机资源或声学延迟通过。

## 同日：SSH远端烧录与启动检查

用户明确授权通过`liyongsheng@10.11.26.88`烧录本版，无需备份。使用远端已有OrbStack Docker和ESP-IDF镜像，不安装主机工具链。端口`/dev/cu.usbmodem11101`，ESP32-C3 rev1.1、8MB，芯片MAC`4c:11:ae:2f:e7:a8`。

当前设备实际分区已与早先factory布局不同：NVS仍为0x9000/0x6000，PHY为0xF000/0x1000，otadata为0x10000/0x2000，ota_0为0x20000/0x3C0000，ota_1为0x3E0000/0x3C0000。原固定0x10000工具正确拒绝了分区差异，未写入。继续读取启动元数据，确认seq=1、state=VALID、CRC正确，另一个副本擦除，当前启动槽为ota_0。

确认本应用不依赖factory固定偏移后，使用同一已验证应用镜像定点更新0x20000，大小2115264字节，小于槽容量；不写bootloader、分区表、otadata、NVS或另一槽，不全片擦除、不备份、不修改eFuse。应用SHA256为`74ad88cdd038f08ebbf8083a72b194e310ac9a528c6de74a6a2343a541bf5c64`，匹配上文完整镜像与ELF归档。写入及独立verify_flash摘要比对PASS。

启动观察确认从0x20000加载、ELF前缀`39d0634ff`与本版一致，8kHz/16bit/mono Codec初始化成功、BLE广播启动、UI Ready，主题phosphor_green、音量90。10秒健康值heap=41200字节、主栈余量7892字节，观察窗口未见panic/崩溃；这些数值不代表压缩通话负载下余量。写入及启动检查PASS，压缩协商/真实声音/持续收发仍待App联调。

本地证据目录：`artifacts/rf-link-adpcm-remote-validation/`，保留Flash日志、启动日志、写入回执及分区/OTA元数据（不是原固件备份）。**此设备后续应用更新不可沿用0x10000；须重新核对实际分区和启动槽。**
