# RF Link


专用BLE通话配件固件。App负责网络连接、证书/私钥、发言许可和网络音频编解码；Passport负责8kHz单声道采集播放、实体PTT和电台资料。BLE只用独立20ms IMA-ADPCM（format=2），每包100字节，不保留PCM/Opus传输模式。内部I2S仍为16bit PCM。不启用Wi-Fi，不申请设备证书，不写eFuse。

当前[音频合同](../../docs/spec/rf-link-handmic/adpcm-only.md)与[App入口](../../docs/spec/rf-link-handmic/README.md)为对接依据。Opus压力实验已结束，测量结果保留在历史文档；编解码依赖、启动自测及压力查询代码已从产品移除。

## 产品界面v3

采用黑底荧光屏仪表，默认荧光绿，可切换琥珀橙、冰蓝和柔白。默认关闭频谱，收听突出呼号和距离，讲话显示秒级大计时；设置可开启真实音频频谱。守听显示最近3位，通话时底部显示上一位，最近通话页保留5位。历史为本次服务器会话的RAM记录，不保存语音，断开手机/换服务器/重启后清空。

主屏按住OK请求讲话，松开立即结束；上下短按调音量。未请求或发送时长按上500ms打开设置，收听不中断。设置包含最近通话、电台资料、音量、手机连接、主题、频谱与返回。设置里OK只用于导航；长按OK800ms回主屏，松开后下一次按下才能讲话。

音量页上下实时试听原有播放音量，OK保存，长按OK取消；不会自动播放测试音。主题页上下预览、OK保存、长按OK取消。更换/移除手机均二次确认，默认取消。首次使用在设备上按OK开放60秒连接窗口，手机输入设备显示的六位口令。

完整设计见[产品规范](../../docs/superpowers/specs/2026-11-02-rf-link-ui-v3-design.md)，App互通见[协议包](../../docs/spec/rf-link-handmic/README.md)。旧ble-signer-lab使用旧UUID/帧头，不能直接作为正式协议客户端。

## 开发与模块

```sh
./tools/dev test
./tools/dev build rf-link-handmic
./tools/dev preview rf-link-handmic
```

`hm_core`为可移植BLE业务核心；`hm_product`负责页面、历史和设置事务；`hm_spectrum`为256点FFT与16频带分析；`hm_ble`负责安全绑定/GATT/分片/有界队列；`hm_audio`管理有界BLE音频入口工作任务、I2S与按需分析窗口；`hm_ui`复用LVGL控件并局部刷新，`main`处理线程协调和NVS。FFT在UI分析阶段运行，不在I2S采集/播放任务中执行。

NVS命名空间`rf-handmic`保存device_id、音量、主题稳定ID及频谱开关；NimBLE独立保存单一bond。主题未知/缺失回退荧光绿，设置故障明确提示且不擦NVS。音量/主题取消恢复原值，保存失败回滚并提示。

CCCD注册包装仅支持固定IDF5.5.3，对订阅读权限施加与业务特征一致的认证要求，门禁不满足时拒绝广播。松键即时关闭独立音频许可并增加释放代次，旧排队帧不能因快速重按恢复；已被控制器接受的帧无法撤回。

构建、主机测试和真实LVGL预览不代表正式BLE配对、真实音频延迟或实体按键验收。首版结果见[首版记录](../../docs/development/rf-link-handmic-validation-2026-11-01.md)；v2的测试与实际写入/启动结果见[v2验证记录](../../docs/development/rf-link-handmic-ui-v2-validation-2026-11-01.md)，不沿用首版通过结论。

本轮UI/音频调度优化与精确产物见[v3验收记录](../../docs/development/rf-link-ui-v3-audio-validation-2026-11-02.md)，可选诊断见[audio.stats](../../docs/spec/rf-link-handmic/audio-diagnostics.md)。
