---
last-reviewed: 2026-11-01
---
# RF Link BLE手咪1.0开发验证

用户确认方案定稿并开始开发。交付独立`firmware/rf-link-handmic`应用，保留旧BLE实验，不修改iOS源码与上游BSP。本记录不代表正式产品真机通过。

| 检查 | 结果 | 范围 |
| --- | --- | --- |
| `./tools/dev test` | PASS，24项 | 协议核心、分片/CCCD、释放代次和时间边界、现有工具及产品回归 |
| `./tools/dev gate` | PASS | 上游原始门禁、全部host tests、隔离构建、镜像/ELF归档 |
| `./tools/dev build rf-link-handmic` | PASS | IDF5.5.3、ESP32-C3、8MB、产品镜像及配套ELF验证；最终增量重建同哈希 |
| `./tools/dev preview rf-link-handmic` | PASS | 真实LVGL页面、全部显示字形、100次页面切换 |
| 独立源码复核 | 已修复6项重要问题 | 旧时间戳下溢、快速松键重按、BLE帧龄、MTU后更新、协商分片、解绑确认键 |
| 正式产品设备验收 | NOT RUN | 未打开串口、复位或烧录；只读端口枚举未发现`/dev/cu.usbmodem*` |

已实现单bond LESC/六位口令、60秒本机窗口、实体确认重配、PCM双向流/信用、PTT许可/释放/超时、服务器/讲话人/TELE/RIG/位置距离、独立中文UI和持久化音量。App继续处理网络身份及编解码。字库覆盖GB2312汉字和界面标点，字库外动态字符显示问号和限制提示。

松键立即关闭独立发送许可并增加释放代次；只有当前按压对应的新MIC流可以重新打开许可。BLE实际提交前再查许可和帧代次，已被NimBLE/控制器接受的帧无法撤回。`transport_boot_submitted`只计本地NimBLE接受，不代表手机接收。

## 精确交付

- 完整镜像SHA256：`42e59f5d795c2171bc821648c2f2f991b5fccf865a20291422457f3040964e83`
- 匹配ELF SHA256：`0f5b7c01ed1cbea3baa62ee2f1cb68b3fc365513def1164fff1543583c9ab502`
- 应用镜像SHA256：`4eff94bfa2842734401f5ca976daf67e82788c62b7633a4cd3774a4013366684`
- 应用大小：2092176字节；偏移`0x10000`。
- [验证归档](../../artifacts/rf-link-handmic/42e59f5d795c2171bc821648c2f2f991b5fccf865a20291422457f3040964e83/manifest.json)、[完整镜像](../../artifacts/rf-link-handmic/42e59f5d795c2171bc821648c2f2f991b5fccf865a20291422457f3040964e83/rf-link-handmic-full.bin)、[匹配ELF](../../artifacts/rf-link-handmic/42e59f5d795c2171bc821648c2f2f991b5fccf865a20291422457f3040964e83/rf-link-handmic.elf)。产物本地保留、Git忽略，未发布。
- 上游`006c2279852e352a8433b2c21e5e09be2ca36f77`；仓库`ac11db25e422f52527a426f49ceb2433e45deca5`加当前未提交源码。来源记录为同级`.build.json`，不声称干净提交或跨机逐字节复现。

## 真机前提与待验收

重新发现设备端口，核对归档与实际分区后，建议仅写应用区，保留NVS、分区表及引导程序；启动会新增`rf-handmic`配置和NimBLE绑定数据，不删除旧实验数据、不写eFuse、不整片擦除。完整合并镜像不能当应用镜像写到`0x10000`。正式产品写入需按仓库AGENTS取得该次明确授权，本轮没有使用旧实验授权自动写入。

待验收：首次及超时配对、手机忽略后的恢复、重配确认、断电保留、三订阅权限、断线/信用丢失停止、快速PTT及60秒截止、长时间音频切换、实际端到端延迟、I2S DMA尾音、手机后台行为及真机中文/按键。旧Web实验的听感通过不能替代这些正式协议结果。

## 用户授权后的首次正式烧录

用户明确授权后，于`/dev/cu.usbmodem1201`发现ESP32-C3 rev1.1、8MB，MAC`4c:11:ae:2f:e7:a8`。通过Docker核验全部归档文件，并读取实际分区表与归档逐字节比较：PASS。仅写入应用镜像到`0x10000`，2092176字节，擦除范围`0x10000–0x20efff`；esptool写入摘要及独立verify_flash均PASS。未整片擦除，未写NVS/分区表/引导程序/eFuse，无固件备份。

启动观察确认显示/LVGL、电池、ES8311与8000Hz/16bit/mono I2S、按键和NimBLE初始化正常，正式产品打印Ready并开始广播。首个10秒健康记录heap=63676、main_stack_min=8244。串口观察不代表屏幕视觉、真实BLE安全配对、PTT、端到端音频或手机后台验收通过。写入回执和原始监控日志保存在本地`artifacts/rf-link-handmic-validation/`，不提交。前述NOT RUN为烧录前状态；本次只将分区比较、写入/验证和启动检查标记PASS。
