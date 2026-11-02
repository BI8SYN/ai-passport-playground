# BLE 签名验证固件

本应用是 RF Link 远程签名的独立实验，未实现 RF Link 网络/音频协议，不申请证书。Web 页面持有明文测试私钥；Passport 只保存一个签名端公钥，在本机生成挑战并用 libsodium 对返回的 Ed25519 签名验签。它用于验证传输、应用层单身份绑定和故障处理，不证明防克隆、安全配对或正式服务兼容性。

## 启动测试环境

仓库根目录运行：

```sh
python3 tools/serve_ble_lab.py
```

电脑 Chrome 打开 <http://localhost:8765>。服务器仅监听 `127.0.0.1`，只提供 `tools/ble-signer-lab`，无第三方脚本、上传或正式服务请求。页面启动时执行 RFC8032 标准向量自检。测试种子公开在页面及源码中；不要导入正式私钥。支持输入另一把 32 字节种子或生成新测试身份，新身份不持久保存；刷新页面恢复默认公开身份。

```sh
node tools/ble-signer-lab/test.mjs
./tools/dev test
./tools/dev build ble-signer-lab
./tools/dev gate
```

工具链、SDK 和编译仅在既有 Docker 入口运行。`espressif/libsodium` 固定版本及依赖锁与应用一起维护，不修改上游。构建归档位于 `artifacts/ble-signer-lab/<SHA256>/`，生成数据不提交。

## 已授权的容器设备入口

用户须明确授权写入；构建成功及设备连接本身不构成授权。本次会话用户已授权所有BLE签名验证迭代写入，并明确无需备份。以下入口限定本实验归档、发现的USB串口及`0x10000`应用偏移；运行时再次验证归档，并先读取分区表，匹配才写入：

```sh
./tools/dev ble-lab-device inspect --port /dev/cu.usbmodem1201 --archive artifacts/ble-signer-lab/<SHA256>
./tools/dev ble-lab-device flash --port /dev/cu.usbmodem1201 --archive artifacts/ble-signer-lab/<SHA256>
./tools/dev ble-lab-device monitor --port /dev/cu.usbmodem1201 --archive artifacts/ble-signer-lab/<SHA256>
```

此入口只为本次命令向SDK容器映射单个串口；默认Compose仍为build-only。flash只写应用，并独立执行verify_flash；monitor不发送主动复位命令、20秒后关闭串口；但实测OrbStack串口映射/打开过程可能触发USB_UART_CHIP_RESET，因此应在BLE测试前观察，不能与BLE测量并行。读出的分区表用于布局核对，不是固件或用户数据备份。不写bootloader/NVS/分区表，不执行整片擦除。

## 设备交互与验收

1. 烧录须另行确认端口、精确归档及数据影响，不可因设备已连接自动写入。
2. 固件广播 `Passport Sign Lab`。网页点击连接，浏览器选择该设备。
3. 点击“申请绑定当前身份”。设备显示公钥前16个十六进制字符；对照网页，按设备 OK 确认。UP 取消。候选身份只在本连接有效。
4. 网页点击“请求一次签名”或按设备 OK。设备生成新随机挑战，网页自动签名，设备验签后增加 PASS 并显示往返耗时。
5. 连续验证10次，查看日志和设备 RTT；它包含浏览器调度、传输及设备验签，不等于 Ed25519 算法耗时。设备发出状态更新通知，网页读回完整请求，并以500ms轮询作为兜底。
6. 重放上次响应，REJECT 应增加；勾选错误签名后发起新请求，也应被拒绝，随后等待该请求超时。关闭自动回复并请求，10秒后 TIMEOUT 应增加；开启自动回复后发起新请求应恢复。
7. 断开并重连，原公钥应保持；请求应能继续。正在进行的请求在断连时作废。设备断电后绑定仍应保持。
8. 生成另一测试身份并申请绑定，应被拒绝；恢复原种子无需重新绑定。这里唯一的是绑定公钥，公开相同测试种子的两个网页并不是两个独立身份。
9. 设备长按 DOWN 进入解绑确认，再按 OK，只删除本应用的绑定公钥；UP 取消。解绑成功后才能绑定其他公钥。

网页导出 JSON 测试记录，不包含种子。设备英文诊断页显示连接、绑定、公钥前缀、PASS/REJECT/TIMEOUT、RTT、电量和按钮提示，未复用硬件测试菜单。

## 协议 v1

Web 为 BLE central，Passport 为 peripheral/GATT server，最多1条连接：

| UUID | 用途 |
| --- | --- |
| `3fc10001-8b80-4f3a-9b22-4a4700000001` | 服务 |
| `3fc10002-8b80-4f3a-9b22-4a4700000001` | RX，write with response |
| `3fc10003-8b80-4f3a-9b22-4a4700000001` | TX，read |
| `3fc10004-8b80-4f3a-9b22-4a4700000001` | 独立变化提示，notify |

RX 是 UTF-8 JSON 加换行符，每次写最多20字节，一条报文连同换行最多511字节；固件有界重组，超长帧丢弃到下一个换行。独立通知特征只有1字节变化提示（不与状态特征共用，避免浏览器缓存竞争），网页随后 read 读取完整状态（最多512字节），避免依赖大 MTU 或长通知。

请求：`{"op":"bind","key":"<32-byte hex>"}`、`{"op":"request"}`、`{"op":"signature","id":1,"sig":"<64-byte hex>"}`。

状态含 `v`、`bound`、`key`、`candidate`、`pending`、`id`、`message`、`passed`、`rejected`、`timeouts`、`rtt_ms`、`note`。网页对 `message` 的原始 UTF-8 字节执行普通 Ed25519，不执行 Ed25519ph，不额外预哈希。设备验证当前请求编号、签名和10秒有效期；一次成功后清除 pending。随机挑战在 BLE 射频初始化后生成，断连或超时清除待处理请求。

## 数据与边界

分区沿用现有基线：NVS `0x9000/0x6000`，PHY `0xf000/0x1000`，factory app `0x10000/0x7f0000`。仅在确认实物分区匹配后才可用 app-only 更新来保留原数据；合并镜像会覆盖间隙，可能清除 NVS。

绑定存于 NVS 命名空间 `ble-sign-lab` 的 `public` 键。不会擦整片 NVS，不访问 `ear-lab`，不写 eFuse，不启用安全启动或 Flash 加密。NVS 初始化失败即停止，不自动 erase。初始化/解绑后的空闲页可重新绑定。

这版是应用层公钥绑定，未启用 BLE SMP/bonding/链路加密；不把设备名或MAC作为授权依据。公开测试私钥、任意测试消息签名均只用于实验。生产版本仍需结构化业务授权、BLE链路保护、每登记实例独立证书及手机端密钥生命周期。尚未验证iOS/Android后台行为、Wi-Fi/BLE共存、正式 SAS/APRS 格式或实体防克隆。


## BLE 音频实验增量

`http://localhost:8765/audio.html` 追加 8 kHz/16 位单声道 PCM 音频链路实验。沿用相同设备和服务，在 UUID 后缀 5/6/7/8 上分别提供模式控制、下行 PCM、上行 PCM 通知和统计。与签名测试分开运行。需要 ATT MTU ≥171，偏好247；不沿用 JSON 小包协议。详细协议、真实测量边界和待验收项见[音频验收记录](../../docs/development/ble-audio-lab-validation-2026-10-31.md)。

固件初始化 Codec 并保持 8 kHz I2S；没有 Web 音频会话时仅输出静音。输出音量90（根据真机低响度反馈由55调整），测试音保持原低幅度便于对照。模式停止和断连清空音频队列，不修改持久绑定。音频数据只在 RAM/网页内，默认不落盘、不上传。
