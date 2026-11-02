# RF Link BLE 手咪首版实施计划

> **For agentic workers:** 使用 superpowers:subagent-driven-development 按职责实施与复核；用户已明确要求定稿并开始设计开发，连续执行，不停在计划交付。

**Goal:** 交付独立可构建的正式1.0手咪固件，支持单手机配对、PCM/PTT、服务器/讲话人/TELE/RIG/位置距离展示。
**Architecture:** 独立 `firmware/rf-link-handmic` 共享上游BSP；纯C协议核心、NimBLE安全传输、LVGL自主UI、FreeRTOS音频/控制运行时分离。App仍承担网络/身份/编解码。
**Tech Stack:** ESP-IDF5.5.3、NimBLE、cJSON、LVGL9、ES8311/I2S；一切IDF构建经`./tools/dev` Docker。
**Spec:** `docs/spec/rf-link-handmic/` 全部1.0合同及配对流程。

## Global Constraints

- PCM8000Hz/16bit/mono，176字节帧；Flow24字节；控制消息≤512字节；正式UUID与实验不同。
- 一设备一bond，LESC/Passkey，仅本机确认开启60秒配对/重新配对；不写eFuse、不复制上游UI、不改vendor。
- 现有dirty feature分支包含所需实验和文档基础，原地新增，不stash/reset/切换/自动提交。
- 用户授权本轮开发；正式产品烧录需具体产物另行确认。旧实验写入授权不扩为自动发布/烧录产品。
- App工程保持只读，无生产服务访问，无模拟器。

## Review Focus

- 迟到/重放/缓存淘汰后的请求不得复活旧PTT、sid或讲话人。
- 松键、断连、订阅失效、信用饥饿、租约与60秒截止必须独立停止。
- SMP不得在无配对窗口抢绑或自动淘汰原bond。
- 消费游标按单调时钟推进；音频/资料队列有界，不能积压旧语音。
- 空/过期/歧义资料不能串人或显示0距离，中文不缺字。

## Task 1：协议与配对定稿/测试向量（主代理）
- [x] 标记1.0 frozen，补配对界面/60秒超时/重连/本机重配流程；保留实验尚未实现的边界。
- [x] 固件设计：主屏OK PRESS/RELEASE讲话，上下音量；空闲长按UP进菜单，菜单含资料/配对/重新配对/返回；本机二次确认解绑。配对窗口取消关闭不清已存在bond。
- [x] 定义模块接口，冻结公开向量并以工具校验。

## Task 2：可移植协议核心（专职实现者）
- [x] 先写主机失败测试：HELLO幂等、请求重放、PTT晚到、sid、Flow/信用、音频、context/turn、TTL与负坐标。
- [x] `hm_core.h/.c` 处理JSON命令、状态/计时、元数据和24/176字节数据；不含ESP/LVGL依赖。
- [x] Python测试经`./tools/dev test`编译纯C核心与容器cJSON并运行；读vectors逐字节互通。

## Task 3：NimBLE安全传输（专职实现者）
- [x] `hm_ble.h/.c`：正式GATT、8字节控制分片/队列、订阅与连接代次、indicate发送、LESC单bond窗口/Passkey与本机解绑。
- [x] 主机测试分片异常/过期；ESP32编译检验实际API。不得把编译当SMP真机通过。

## Task 4：独立中文UI（专职实现者）
- [x] `hm_ui.h/.c`：自主主屏/详情/配对/确认页，电池，当前服务器/呼号/频率/型号/网格/距离/状态。
- [x] 独立中文字体子集和可复用预览入口；状态快照驱动，不碰协议/音频/配对存储。

## Task 5：运行时、音频和工程集成（主代理）
- [x] `main.c/hm_audio.c` 控制工作队列、会话锁、物理按键、音频时钟/信用、NVS音量和deviceID、订阅失败安全停止。
- [x] 新CMake/defaults/partitions、容器managed volume、构建归档target/工具入口，保留其他应用。
- [x] 主机测试、原始gate、产品build/归档、UI预览及独立代码复核；修复范围内失败。

## Task 6：交付（主代理与独立复核）
- [x] 更新验证记录/项目简述，报告构建、主机、设备测试各自状态；列精确镜像/ELF哈希。
- [x] 提出正式固件具体端口/产物/数据影响的烧录请求，不自动写入。

## 当前进度与裁定

2026-11-01：用户批准协议/配对方案并要求开始开发。上述UI细节由实现方在三键限制下决定；后续用户反馈可调整，不借此更改线路协议。未获产品写入授权，开发/构建先完成。

开发交付检查已完成；真机验收保持NOT RUN，不包含在上述完成声明内。
