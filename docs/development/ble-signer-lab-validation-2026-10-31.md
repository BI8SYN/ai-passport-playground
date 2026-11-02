# BLE Web 代签实验验收记录（2026-10-31）

目标：使用电脑 Chrome Web Bluetooth 验证 AI Passport 通过 BLE 请求远端 Ed25519 签名。先只要求正常操作下的单公钥绑定，不研究恶意克隆。用户允许测试私钥在网页中明文出现，不作安全隔离。

## 最终结果

核心真机链路PASS：用户已在设备上核对公钥并按OK完成绑定，Chrome经BLE代签、设备本地Ed25519验签成功。连续10次全部通过，端到端672–718ms，平均700.4ms；错误签名/重放/其他公钥绑定均拒绝，无回复10秒超时，超时后恢复、待处理请求断连取消及重连后恢复均通过。最终统计12次通过、3次预期拒绝、2次预期超时。并非正式RF Link客户端验收。

当前安装的是修正通知竞争后的归档`d0851a67c5fa6fe481b63fa1ea0139b5e1a8423f8c58834785f42d6033b61ce9`；以下首版构建与首次烧录记录保留为历史。

## 实现与实际验证

- 独立 `firmware/ble-signer-lab`，复用上游 BSP，不修改 vendor，不复制测试菜单。
- Web 本地服务 `python3 tools/serve_ble_lab.py`，监听127.0.0.1:8765；Chrome页面已实际打开，自检及切换测试身份/恢复固定种子通过。明文种子为公开RFC8032测试向量，非正式证书。
- 单公钥NVS绑定、设备OK确认、UP取消、长按DOWN再OK解绑；断连清除候选/挑战，保留绑定；没有自动擦NVS、eFuse写入或锁机操作。
- 设备生成随机挑战，网页使用Web Crypto Ed25519签名，固件使用固定版本libsodium验签；20字节写入分片和有界重组，1字节变化通知+完整状态读取，500ms兜底轮询。
- 原始 `./tools/dev gate`：PASS（仓库检查、全部上游主机测试、原始固件编译及归档验证）。这不是新应用的真机证明。
- `./tools/dev test`：22项PASS，包括本实验的绑定、替换拒绝、请求覆盖拒绝、错误结果、重放、超时、断连和hex边界状态机检查。
- `node tools/ble-signer-lab/test.mjs`：PASS（RFC8032公钥/签名、正确验签/篡改拒绝、分片重组与报文上限）。`node --check app.mjs`：PASS。
- `./tools/dev build ble-signer-lab`：PASS，合并镜像、分区范围、应用身份、ELF哈希及内容寻址归档验证通过。独立主机重新计算所有归档文件SHA256，与manifest一致。
- 代码审阅发现默认3584字节main task栈不足以执行libsodium验签，已显式设置12288字节，增加周期堆和栈水位日志。真机栈水位待测。

## 精确产物

| 项目 | 值 |
| --- | --- |
| 归档目录 | `artifacts/ble-signer-lab/7d5e015a02b5e5621043836a0ffb72be0c959b911ac84b86756827348cd7dfb2/` |
| 合并镜像SHA256 | `7d5e015a02b5e5621043836a0ffb72be0c959b911ac84b86756827348cd7dfb2` |
| 应用镜像SHA256 | `85596acc0c4aa9a3ee64530aa69ace0fad2c9ff7edd12c940576791df9580a70` |
| 应用ELF SHA256 | `68f8b66997552376b8ababeec88d5c66c5c0133b1197fb02327722f65678a5e1` |
| 应用镜像大小 | 1154352字节 |
| 应用写入偏移 | `0x10000`，结束于`0x129d30`（不含），Flash扇区影响到`0x12a000`（不含） |
| SDK | 容器ESP-IDF 5.5.3 |
| 签名库 | `espressif/libsodium` 1.0.22；其余组件见应用依赖锁 |

## 设备状态与未验证项目

只读发现：`/dev/cu.usbmodem1201`，Espressif USB JTAG_serial debug unit，VID:PID `303A:1001`。尚未打开串口、复位、读取分区或烧录。

计划取得该次明确授权后，先读取实物分区表并与归档匹配；匹配时仅更新应用区，保留bootloader、分区表、NVS及练耳设置/成绩。若不匹配则停止，不擅自改分区或使用合并镜像。新应用使用独立NVS命名空间，运行后仅保存自己的绑定公钥。

真机验收：NOT RUN。待测：启动/堆及栈水位、屏幕/按键、BLE连接、设备确认绑定、单次/连续10次签名、错误签名/旧响应/超时、断连恢复、断电保留绑定、其他身份拒绝、解绑重绑。正式RF Link SAS/APRS、iOS/Android后台、Wi-Fi共存及正式证书申请均未验证。

完整运行和验收步骤见[实验说明](../../firmware/ble-signer-lab/README.md)。本轮未提交、推送、发布；不作防克隆或硬件安全存储承诺。

## 获授权后的首轮真机进展

用户已明确授权所有BLE签名验证迭代固件写入，并要求无需备份；仍限定本实验，不包含全片擦除或eFuse操作。

- 实物：上述端口确认ESP32-C3 revision v1.1、8MB Flash、USB-Serial/JTAG。
- 先读取`0x8000`起4KiB分区表，前3072字节与归档逐字节一致。随后仅写入上述应用镜像1154352字节，写后哈希及独立verify_flash均PASS（digest matched）。未备份、未写bootloader/分区表/NVS/PHY。
- 20秒有界启动观察：应用名称/ELF匹配；显示、LVGL、ADC三键、电量计初始化；Ed25519 RFC8032自检PASS；BLE广播成功；空闲堆117736字节，main task最小栈余量7796字节。无启动崩溃。原始日志保留于忽略的`artifacts/ble-signer-lab-validation/`。
- Chrome实际发现`Passport Sign Lab`并建立BLE连接，读取状态成功；申请绑定后设备返回`Press OK to bind signer`及预期公钥。
- 当前等待用户在设备上核对公钥前缀并按OK，未绕过实体确认。签名往返、异常处理、重连及持久化尚未验证。


## 最终迭代与真机验收

首次绑定后真实签名已通过（685ms），但Chrome实际出现过`Unexpected token '\u0001'`：单字节变化通知和完整JSON读取共用同一特征，通知可能改变浏览器缓存。已将状态读取和变化通知拆为`...0003`只读及`...0004`通知两个特征，重新构建、应用区写入和独立verify_flash均PASS，保留原绑定。随后完整矩阵未再出现该JSON错误。

最终合并镜像SHA256：`d0851a67c5fa6fe481b63fa1ea0139b5e1a8423f8c58834785f42d6033b61ce9`；应用1154416字节，SHA256 `077c8da2ff22bdf01c8036e36c70123ee3bc8b8ba71eca97f8c83a0b0456ffbf`；ELF SHA256 `42fe16561f9666fd189d3e7ac7e5f7f211ec4e0460a1de51c4ea7ae7552465b0`。

- 用户确认设备公钥前缀并按OK，单身份绑定成功。
- 最终固件启动/USB复位后`bound=1`，绑定保留，无需再次确认；堆117612字节，main task最小栈余量7796字节。
- 10次往返样本（ms）：714、685、714、676、718、717、717、674、717、672。平均700.4ms，中位714ms。耗时包括BLE分片写入、网页调度及设备验签，不能据此推断Ed25519算法本身耗时。
- 重放已完成的id10响应：REJECT由0增至1，PASS保持10。
- id11故意损坏签名：REJECT增至2，PASS保持10；该挑战随后超时。
- id12关闭自动回复：10秒后超时计数增至2；恢复自动回复后id13通过，PASS增至11。
- 网页更换另一公钥并申请绑定：`Different signer rejected`，原公钥保持，REJECT增至3。恢复原种子，无需重绑。
- id14待处理时断连；重连后为空闲、原绑定及计数保持、超时仍为2，旧挑战已取消。新id15验签通过，PASS增至12、RTT716ms。
- 连接观察期间启动串口monitor导致过一次`USB_UART_CHIP_RESET`，并引发Web GATT断连；这不是已证实的固件崩溃。此后关闭串口monitor再测BLE，全程无同类中断；README已注明串口映射/打开可能复位，不应与BLE测量并行。

忽略的本地证据：`artifacts/ble-signer-lab-validation/device-validation.json`、`batch-summary.json`、阶段AX快照、串口日志、`flash-receipt.json`和`verified-web.png`。浏览器页面仍在<http://localhost:8765>，设备保持原公开测试身份绑定。

Build：PASS；Host tests：22项PASS；Web tests：PASS；上述Device tests：PASS。实体解绑/重绑、真正断电保持（目前测过USB复位）、手机后台、正式SAS/APRS和Wi-Fi/BLE共存仍未验证。本轮无Git提交、推送或发布。
