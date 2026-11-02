# RF Link Opus 压力试验版验证（2026-11-02）

用户要求增加Opus版本以比较压力。本轮实现、测试和归档，不修改iOS、不烧录、不提交或发布。协议与测试步骤见[Opus 1.2](../spec/rf-link-handmic/opus-v1.2.md)，计划见[实施计划](../plans/2026-11-02-handmic-opus.md)。

## 实现

- 固定`espressif/esp_audio_codec==2.4.0`，依赖及锁文件由容器内组件管理器解析；未修改vendor。
- 可选format=3、minor=2，8kHz/mono/20ms/16kbps CBR，原始40字节、BLE完整56字节；PCM/IMA保留。
- TOC、声道、总帧长及分帧/填充边界检查；头合法后仍需实际解码器校验内部码流。
- 音频任务锁外编解码，接收槽改存完整编码包而非160采样PCM；每个方向20ms节拍、60ms一次起播、6→3帧裁剪和信用规则保持。
- 半双工共享一个动态创建的编解码实例；新流/方向切换释放旧实例，旧worker的release不会关闭新stream实例。内存失败或创建后剩余堆小于8KiB时结束匹配流，保留重新选择IMA/PCM路径。
- mic/speaker栈调整为16KiB/8KiB；codec.get分timing/memory查询，串口10秒健康日志也提供指标。所有C3运行资源与栈峰值均待真机确认。
- Web后端继续仅声明PCM/IMA；未把主机libopus测试伪装成Web新增Opus互通能力，未覆盖已交付ZIP。

## 验证结果

| 项目 | 结果 | 证据范围 |
| --- | --- | --- |
| 新能力回归首次在旧代码执行 | 预期失败 | 旧核心缺少Opus能力字段/接口，无法满足新协商用例 |
| `./tools/dev test` | PASS，36项 | 最终0.716秒；包含全部PCM/IMA、Opus协商、长度、20ms消费、信用/PTT和codec.get回归 |
| 实际主机libopus | PASS | 容器已有libopus.so.0，100个连续CBR包均40字节、通过自有TOC验证、解码160采样；非法时长/声道/填充拒收；3条公开向量逐包校验 |
| 适配层生命周期故障注入 | PASS | 使用平台/codec API桩编译真实hm_opus.c；单实例、换流、旧release、分配失败、8KiB门限、异常输出、处理失败及耗时统计。此项不测试C3库实际计算或分配峰值 |
| `./tools/dev simulator-build` | PASS | 共享核心、原PCM/IMA能力及真实LVGL编译 |
| 容器内`backendtest.py` | PASS，9项 | 既有模拟器PCM/IMA收发、时钟、配对、状态和UI回归 |
| `./tools/dev gate` | PASS | 上游完整门禁，独立于产品构建 |
| `./tools/dev build rf-link-handmic` | PASS | 真实C3编译链接、分区、镜像、匹配ELF及导出归档验证；无编译warning/error |
| 真机压力 | NOT RUN | 未写入Opus版本，尚无真实C3编解码耗时/CPU/最低堆/栈/声音数据 |

## 精确产物

归档：`artifacts/rf-link-handmic/262883b960504c3357c212cfed73850f482182308f360c1731131d2e4d26cbd5/`。

- 完整镜像SHA256：`262883b960504c3357c212cfed73850f482182308f360c1731131d2e4d26cbd5`。
- 匹配ELF SHA256：`3705fb0b0756078d320896bddd9e7126edabece4bd91abe54d1a1295110442d8`。
- 应用区镜像：2322912字节，SHA256 `9101baea5d24aa35243a5a53e56c6c9c9a2dfaf71fd176d004c62f3115ab9f4a`。

SHA与大小来自已验证manifest，不代表硬件验收。默认完整镜像为factory布局，不应覆盖当前远端设备已有OTA布局。用户此前授权烧录的是ADPCM构建；新Opus写入需要对应授权，随后重新核对`10.11.26.88`设备端口、分区与启动槽，兼容时只写目标应用槽，保留NVS和otadata。

## 真机待测

先测RX，再短PTT，查看codec.get两种视图。重点看是否能成功分配、enc/dec最大处理时间是否超过20ms、最低堆及最大连续块、音频任务历史剩余栈、是否音频错误结束或异常复位。压力统计是单调时钟经过时间，不是精确CPU占用率；平均处理预算、首次创建成本、端到端延迟需分开判断。Opus目前无PLC/FEC，断续时重置解码器的恢复听感亦待验证。

最终独立复审：发现旧流延迟编解码可能替换新流实例的竞争，已加入codec锁内的sid/stream单调水位检查，拒绝路径不执行实例清理。新增旧encode/decode请求在新流建立后到达的回归，在原实现确认失败，修复后新实例保持且测试通过；复审APPROVE，无剩余Important/Critical。最终镜像已在修复后重新构建并验证。

本轮只读SSH发现尝试遇到1Password SSH agent签名失败，未能确认当前串口；未打开设备或写入。后续烧录需恢复代理登录并重新发现设备。
