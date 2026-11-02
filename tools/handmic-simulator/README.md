# RF Link Web Passport

本地BLE外设模拟器，用于iOS App与Passport音频链路对照。Mac原生CoreBluetooth发布正式GATT，Web像素屏幕直接复用正式hm_core/hm_product/hm_ui/hm_spectrum源码，支持三键、PTT/麦克风、音量、四主题、频谱开关、最近通话、电台资料和虚拟手机连接管理。

启动：

```sh
./tools/dev simulator
```

打开 http://127.0.0.1:8765 。首次在模拟屏幕按OK开放60秒连接窗口，iOS选择“RF Link · SIM”；系统权限/配对提示按系统界面完成。后续保存的虚拟连接可重连，不需清除实体Passport绑定。长按上进入设置；上下选择，OK确认，长按OK返回。浏览器需点击“开启试听”；麦克风也需单独点击授权。比较BLE解码后的PCM与复用设备缓冲逻辑后的PCM，网页额外预缓冲约200ms且短片段会自动排空，不能拿此延迟测真实电声延迟。请只保留一个开启试听的网页，避免多页同时发声。

`Ctrl-C`停止Web/后端；退出“RF Link BLE Simulator”结束本机虚拟广播。源码不包含iOS或生产网络连接代码，不访问实体USB串口，不刷写设备。模拟设置保存在`artifacts/handmic-simulator/settings.json`，只含虚拟连接ID、音量、主题、频谱开关；音频不自动保存。诊断导出只含统计和事件，不含PCM录音。

## 构建和测试

- `./tools/dev simulator-build`：Docker构建C后端（LVGL依赖复用已解析managed组件），无主机ESP-IDF。
- `python3 tools/handmic-simulator/build_bridge.py`：本机现有Swift编译器构建MacOS桥，deployment target13.0，ad-hoc签名验证。
- `./tools/dev test`：本地主机测试含WebSocket、包统计、像素转换、后端失效和断链状态。
- `node tools/handmic-simulator/test_audio_worklet.js`：短尾、清空和8kHz麦克风帧。
- 后端黑盒回归见`backendtest.py`和`report-backend.md`，通过真实碎片命令/PCM验证核心、流控、渲染、保存取消、解绑、时钟及跨流统计。

协议接口、端口和NDJSON见[CONTRACT.md](CONTRACT.md)。所有服务仅监听loopback，浏览器WebSocket校验Host/Origin；后端退出撤销原生桥接会话，不继续ACK并丢弃音频。页面截图、日志、构建依赖和本机App产物在artifacts中，Git忽略。

## 对照边界

同源码模拟器保留协议核心行为，不能据此排除共同核心缺陷；原始PCM路径独立于核心接收判定，有助于区分输入质量与缓冲后结果。Mac蓝牙连接参数/CPU/声音时钟不同于ESP32；I2S/Codec/RF不被等效模拟。CoreBluetooth的加密由OS管理，不能复刻或验证ESP六位口令/MITM。Info安全字段为App兼容保留，不能当作安全认证证据。

到达序号缺口可能来自发送端主动丢弃或链路行为，不能直接称为BLE空口丢包率。全局audio_accepted/rejected与最后一条stream的rx_packets/late/underrun统计范围不同。导出时若已无人讲话，packets_per_second=0、stream_id=0是正常空闲，不能据此认定连接故障。

## 当前音频格式

当前固件和模拟器仅支持IMA-ADPCM，Info声明formats=[2]，20ms/160采样/100字节，无PCM/Opus回退。C后端、Mac桥和Web解析保持一致。Web Audio内部仍使用PCM：采集每80采样输入，由后端合并为160采样后编码；两路试听分别是BLE解码后的PCM和Passport缓冲后的PCM。

请用新源码重新构建后端及桥并重启，单独刷新网页不会更新二进制，已发桌面ZIP也不会自动更新。[当前App合同](../../docs/spec/rf-link-handmic/adpcm-only.md)及本目录adpcm.py为接入依据。模拟器不证明真实BLE吞吐或电声延迟。

UI v3默认关闭频谱，设置第六项可切换并跨后端重启保存；关闭后后端也停止分析窗口维护和FFT。实体固件新增的audio.stats调度/I2S统计不在本机模拟，详见[诊断合同](../../docs/spec/rf-link-handmic/audio-diagnostics.md)。本轮未重启已运行的Web/桥，也未覆盖已发出的桌面包。
