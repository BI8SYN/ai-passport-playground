---
last-reviewed: 2026-11-01
status: implemented-and-flashed
---
# RF Link UI v2 实施验证

## 2026-11-02：起播计时修复（已构建，未烧录）

用户确认先修正式固件。首个有效PCM仍等待60ms；同一stream内discontinuity前跳及高水位裁剪不重新等待60ms。原消费时刻落后时提升到当前单调时钟，避免追赶历史时间槽；标记未导致前跳时保留消费时刻；新stream重新执行一次起播等待。PCM格式、BLE字段保持原样，协议消费时钟文字同步更新。

新增纯C回归先在原代码确认失败，修复后通过。覆盖首次起播、重复断续标记、连续前跳、当前游标标记、过期时钟不连续追赶、高水位在起播前/过期后的两种处理、旧序号拒收、换流起播和300ms超时。

- `./tools/dev test`：PASS，32项，含新增纯C回归。
- `./tools/dev build rf-link-handmic`：PASS；最终源文件整理后重新构建，分区、镜像、匹配ELF及导出归档验证通过。
- `./tools/dev gate`：PASS，上游完整门禁，与正式产品构建分开。
- 真机：NOT RUN。只读发现未见`cu.usbmodem*`；未打开串口、复位或烧录。未重新构建/重启正在运行的Web模拟器，也未覆盖已交付桌面ZIP。

完整镜像SHA256：`bfcb7d8de22c7d61107cc845af7453cf965871e2253f95c834e4a432f7e0d5d2`。
匹配ELF SHA256：`400c91632f5ba8d15371af7a2314fa2454f9313827e58155d8e135286d9a31a4`。
归档：`artifacts/rf-link-handmic/bfcb7d8de22c7d61107cc845af7453cf965871e2253f95c834e4a432f7e0d5d2/`。
应用区镜像SHA256：`29b3eb4f77e5c1de7c2671c56cd61f9e11711c41789412e61acbacb6d94837dc`，2113552字节，配置偏移0x10000。

待真机使用当前App复测讲话、断续恢复与停止。逻辑回归及构建不证明实际BLE吞吐，也不证明所有断续均已解决。后续写入前核对设备与实际分区，兼容时可仅更新应用区保留NVS。

用户确认[产品设计](../superpowers/specs/2026-11-01-rf-link-ui-v2-design.md)，并明确授权开发、测试及烧录本次固件。保留原feature checkout和已有未提交应用，不修改vendor或iOS App，不提交或发布。

## 已实现范围

- RF Link命名与黑底荧光屏；荧光绿、琥珀橙、冰蓝、柔白四主题，预览/保存/取消及重启读取。
- 守听最近3位、通话时上一位、完整5位历史；有效PCM触发、身份去重、相对时间，换服务器/断会话/重启清除RAM记录，无语音录制。
- 256点Hann窗FFT、16频带、固定电平地板、快起慢落与峰值；I2S任务只复制有界窗口，UI任务锁外分析，非随机动画。
- 设置、电台资料、音量、手机连接、更换/移除确认、连接引导、主题预览、设备信息及故障页面。
- 音量快捷延迟合并保存；编辑预览/保存/取消/失败回滚；远程volume.set busy在缓存查找之后判断，成功重试不因本机页面占用改变结果。
- 本机NVS错误不自动擦除；单绑定/LESC/本机60秒窗口及实际PTT释放代次门禁保持。

## 验证过程

纯逻辑测试先观察RED再实现GREEN，覆盖频率分离/DC/混音、频谱衰减、历史、主题/音量事务、真实松键事件、600ms/800ms边界、远程音量缓存重试。BSP的延迟单击/双击不能直接作为产品短按，现由实际RELEASE合成一次短按，重复RELEASE无操作。

真实LVGL新快照/页面预期先在旧UI上失败，再实现。固定58控件复用；原32KiB池在复杂页面分配不足，改共享样式并设40KiB，原生预览最大使用约26KiB。150帧对象身份稳定及100次页面/主题切换检查通过；包含长呼号、中文字形、未知电量、0距离、前导零口令和故障页。主机池监控不等于真机可用堆。

上游完整`./tools/dev gate`本轮PASS；初次产品编译缺esp_app_format依赖，补齐后PASS。最终只读审查发现四项运行状态映射问题（网络离线、App静音、拒绝/时限提示、失败连接残留口令），已统一修复并补充真实core command/tick到LVGL的回归；修复前镜像未烧录。最终结果与精确产物将在下节记录。

## 最终结果

| 项目 | 结果 | 证据范围 |
| --- | --- | --- |
| `./tools/dev test` | PASS，26项 | 最终运行0.628s；包含纯C产品、协议、频谱与原有工具回归 |
| `./tools/dev gate` | PASS | 上游完整门禁，独立于产品构建 |
| `./tools/dev preview rf-link-handmic` | PASS | 全页面/四主题/字形/边界；150帧固定58对象，真实计数改变时快速路径149/150；100次页面切换 |
| `./tools/dev build rf-link-handmic` | PASS | 最终编译无warning/error，镜像/分区/ELF匹配验证 |
| 独立复核 | APPROVE | 先前4项P2均已修复，修复范围无新Critical/Important |
| 实际分区比较 | PASS | `/dev/cu.usbmodem1201`，C3rev1.1、8MB，MAC4c:11:ae:2f:e7:a8 |
| 应用区写入/独立verify_flash | PASS | 0x10000，2113472字节，摘要匹配 |
| 新固件启动观察 | PASS | 20秒无崩溃/看门狗；显示/音频/BLE初始化正常、Ready |
| 真机显示与设置操作 | 用户确认PASS | 按屏幕/主题/音量检查步骤，用户反馈“显示和操作正常”；不扩展为全页面/配对/收发验收 |

最终完整镜像SHA256：`e17f10ab01fd81b12a42e461e31a22154422fc0e086384b4274212e620271284`。匹配ELF SHA256：`9ec4d919e27fa7b274f4ebcf936d57dc3d625939213969ec44651fde105d5843`。应用SHA256：`3aded8ff8338e01d21193705a52c4c940c2d27dd3f5a48925f0f1aebcc5f0811`。归档：[manifest](../../artifacts/rf-link-handmic/e17f10ab01fd81b12a42e461e31a22154422fc0e086384b4274212e620271284/manifest.json)，[完整镜像](../../artifacts/rf-link-handmic/e17f10ab01fd81b12a42e461e31a22154422fc0e086384b4274212e620271284/rf-link-handmic-full.bin)，[应用镜像](../../artifacts/rf-link-handmic/e17f10ab01fd81b12a42e461e31a22154422fc0e086384b4274212e620271284/rf-link-handmic.bin)，[ELF](../../artifacts/rf-link-handmic/e17f10ab01fd81b12a42e461e31a22154422fc0e086384b4274212e620271284/rf-link-handmic.elf)。本地保存，未发布。

实际只擦写`0x10000–0x213fff`应用区，未写NVS/bootloader/分区表/eFuse，未整片擦除。启动报告`Ready RF Link UIv2 theme=phosphor_green volume=90`；10秒健康记录heap45944、main stack7888字节。初次页面创建在UI计时窗口约70.9ms，此值包含启动首帧，不能当持续频谱帧耗时或端到端延迟。

BLE线协议1.0不变；`stop_reason`只在内部快照保存，未进入wire Status。拒绝/时限提示与UI映射由真实协议命令/tick测试，不仅是手工造图。口令随连接代次失效，系统已提交的绑定保留。

原始日志、回执和源码文件指纹保存在本地`artifacts/rf-link-handmic-ui-v2-validation/`；未提交。

## 设备与待验收

当前只读发现端口`/dev/cu.usbmodem1201`。写入前重新确认端口、实际分区与精确归档；仅应用区0x10000，不写分区表/bootloader/NVS，不修改eFuse、不整片擦除。无固件备份前置要求。

真实BLE安全配对与App音频互通、实体菜单/主题保存取消、断电主题保留、长期音频连续性、实际电声停止和I2S尾音须分别验收；构建和串口Ready不代替这些结果。App尚未完成，不声称已实现手机端联调。

补充串口边界：第二次打开监控时观察到`rst:0x15 (USB_UART_CHIP_RESET)`，与USB串口访问时间一致，不是软件崩溃/看门狗。此后正常重新启动，heap45944/main stack7888；两次20秒窗口均未见panic或WDT。复位前缓冲日志报告`ui_max_us=57123`，此指标包含任务抢占/锁等待/完整界面刷新，不能当FFT净耗时、稳定帧率或音频延迟。真实收发与长时UI性能仍待App互通实测。已释放串口，不持续干扰用户操作。

用户现场反馈：收到屏幕、主题与音量设置操作检查请求后明确回复“显示和操作正常”。记为本轮视觉与设置交互验收通过；尚未证明真实App配对、PTT网络许可、持续音频、动态频谱性能及断电保存全部通过。
