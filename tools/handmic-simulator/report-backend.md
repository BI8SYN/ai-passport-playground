# Passport 诊断模拟器 C 后端验收

日期：2026-11-01。范围仅限本目录的 `backend.c`、`CMakeLists.txt`、`lv_conf.h`、`build_backend.py`、`backendtest.py` 和本记录。未修改产品固件、上游、iOS、实体 USB 或物理绑定，未提交或烧录。

## 实现

SDK 容器原生 C 程序直接链接当前正式产品的 `hm_core.c`、`hm_product.c`、`hm_ui.c`、`hm_spectrum.c`、`hm_fragment.c`、`hm_font_16.c`，以及已有 managed LVGL 和 ESP-IDF cJSON；这些来源保持只读。生成 `/build/handmic-simulator/backend/passport_simulator`。

输入/输出遵循 `CONTRACT.md` 的 NDJSON，stdout 无普通日志。启动即发送 Info/Status、初始广播意图、真实 240×320 RGB565 像素和状态，供服务器缓存后交给新连接。LVGL 40KiB 配置、实际产品全部页面、主题、设置事务、历史、按键500/800ms行为及资料格式均复用产品实现。显示最多20Hz且像素不变不发送。

单线程 monotonic 固定10ms时钟；stdin 使用非阻塞读取和每轮64行上限。控制分片经过正式重组器，业务响应和事件经过正式核心；HELLO响应20B分片，后续按协商值分片。错误 JSON、非法十六进制、超长行、坏控制/音频/流控均有计数并继续运行。

播放路径复现32槽、核心验包、首帧60ms预缓冲、序号/重复拒绝、缓存裁剪、10ms消费、缺帧补零和会话/音频租约；历史只在接受 speaker PCM 后更新。播放时钟在SID/stream/direction改变时，先发送`audio_boundary`（`source: processed`及新身份），随后才发送相应第一帧PCM，包括停止后的首帧零音频，使Web能按确定顺序清除旧段。麦克风只处理浏览器实际送来的80样本，不生成假麦克风；由核心检查PTT、信用、序号和租约。频谱采用实际消费/送出的PCM。DeviceFlow按固件100ms活跃/500ms空闲、4帧信用推进、会话/流/PTT改变刷新。

模拟器单独保存音量、主题、虚拟 central ID。JSON设置采用同目录临时文件、0600权限、fsync及原子rename；初次虚拟绑定只有在产品60秒窗口内授权连接、全部订阅并成功HELLO后提交。移除虚拟绑定通知原生桥重建链路，保留实体设备和系统已有绑定。窗口取消立即关闭未提交链路。

## 已运行验证

```text
docker compose run --rm --no-deps -T sdk python3 tools/handmic-simulator/build_backend.py
docker compose run --rm --no-deps -T sdk python3 tools/handmic-simulator/backendtest.py
```

容器 CMake/Ninja 构建 PASS，后端以 `-Wall -Wextra -Werror` 编译。LVGL提示缺少可选pkg-config，实际链接完成，不依赖该工具。

黑盒集成8项 PASS（最终运行10.287秒）：

1. 正式分片HELLO/radio命令，真实非空153600字节帧，坏NDJSON/十六进制/控制JSON后的恢复，0600设置与虚拟绑定重启重连。
2. 真按键长短按、音量95保存、琥珀主题保存、菜单进入/返回、虚拟解绑和reset_link。
3. Speaker实际PCM输出、重复拒绝、缺帧零填、有效PCM历史与300ms音频超时。
4. PTT→mic流→AppFlow信用→实际麦克风包，松手停止；未产生额外假录音。
5. 未授权central拒绝、订阅丢失关闭核心、恢复订阅后新HELLO、3000条坏消息积压仍继续10ms播放时钟。
6. 配对窗口内连接但未HELLO，用户取消后立即关闭，不保存绑定。
7. 两段speaker流间隔550ms、桥时间戳相隔600ms，第二流只统计流内10ms间隔，旧stream延迟包不污染；新SID清空全部到达间隔统计。
8. speaker开始后第一条播放相关事件必为processed边界；收到真实PCM并停止后，先发新身份边界，再发停止零音频。

## 诊断边界

这里是实际产品模型和协议的宿主运行时；模拟Codec写入及调度，不是ESP任务/I2S/BSP时延复现。输出PCM仅体现数字静音，未模拟实体Codec音量曲线或扬声器声学效果。stdout/容器/宿主忙碌可能影响模拟节拍，`clock_overruns`可见。

Info保留产品协议字段以兼容App；不能据其中`pairing: sc_passkey`声称模拟了ESP安全级别。启动事件和metrics明确 `security: virtual_binding_only`、`native_security_os_managed: true`、`physical_passkey_reproduced: false`。没有编造六位口令，未验证硬件SC/MITM。

本后端不落盘音频。输入到达间隔来自桥携带的monotonic `arrival_ns`，`rx_interval_scope: latest_speaker_stream` 明确按当前SID最近一条speaker流统计，`rx_arrival_sid`、`rx_arrival_stream_id`标明归属。新SID或新speaker流会清空全部聚合量；流停止时清掉相邻包基线并保留已完成流的统计供查看，绝不把段间静默计作抖动。只有与活跃speaker流SID/stream匹配、头部足以解析身份的ATT写入参与（仍在核心接受前统计），旧流/错会话包只计入拒绝数。没有到达时间戳时指标保持零。服务器需另行提供原始入口PCM对照，后端播放仅包含核心接受/缓存/消费结果。

本次未进行实体固件编译或烧录，也未以该测试代替CoreBluetooth实际广播、iOS连接/音频或真机验收；这些验证由集成阶段分别报告。
