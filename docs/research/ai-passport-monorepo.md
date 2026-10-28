---
last-reviewed: 2026-10-28
status: proposal
---

# AI Passport 固件 monorepo 调研与建议

适合采用 monorepo。建议组织为“多个独立 ESP-IDF 固件工程，共享 BSP、通用组件、资料和工具”，每款应用分别编译、发布和刷写。以下以未来维护多款固件为假设；若只做一个应用，保留上游单工程结构即可，暂时没有必要增加应用层目录。

本文保留实施前的架构调研。用户随后确认采用 monorepo + OrbStack Docker；实际目录和使用方式以 [架构概览](../architecture/overview.md) 与 [容器开发指南](../development/docker.md) 为准。

## 上游现状

调研基线：[FoloToy/ai-passport main，006c2279852e352a8433b2c21e5e09be2ca36f77](https://github.com/FoloToy/ai-passport/tree/006c2279852e352a8433b2c21e5e09be2ca36f77)。后续同步应显式记录新 SHA。

| 事实 | 对组织方式的影响 |
| --- | --- |
| ESP32-C3、8 MB Flash、无 PSRAM、240×320 屏幕、三键输入 | 仓库能容纳多个产品，但设备的 Flash/RAM 预算仍按每款固件计算 |
| 根 CMakeLists.txt 是一个 ESP-IDF 工程 | 多产品应有各自的工程入口，避免共用一个 main 和构建目录 |
| components/bsp 是已有的 ESP-IDF 组件 | 多个工程可以直接引用同一份驱动，不必复制 |
| BSP manifest 要求 IDF >=5.5.3,<5.6.0；CI 和锁文件使用 5.5.3 | 初期统一固定 ESP-IDF 5.5.3，不因目录调整升级框架 |
| 当前锁定 LVGL 9.5.0、esp_lvgl_port 2.9.0、button 4.2.0、esp_codec_dev 1.6.2 | 应用各自持有解析后的 dependencies.lock，升级时一起检查兼容性 |
| main 是硬件测试应用；不同成品示例在 demo/* 分支 | 示例适合作为参考来源，不能直接把分支整体合并为共享平台 |
| 默认分区只有 nvs、phy_init、factory | 默认没有双槽 OTA；OTA 需要另行设计分区并验证容量和升级流程 |

来源：[项目说明](https://github.com/FoloToy/ai-passport/blob/006c2279852e352a8433b2c21e5e09be2ca36f77/docs/README.zh_CN.md)、[BSP manifest](https://github.com/FoloToy/ai-passport/blob/006c2279852e352a8433b2c21e5e09be2ca36f77/components/bsp/idf_component.yml)、[依赖锁](https://github.com/FoloToy/ai-passport/blob/006c2279852e352a8433b2c21e5e09be2ca36f77/dependencies.lock)、[分区表](https://github.com/FoloToy/ai-passport/blob/006c2279852e352a8433b2c21e5e09be2ca36f77/partitions.csv)。

## 组织方式比较

| 方案 | 优点 | 代价与适用范围 |
| --- | --- | --- |
| 官方 fork，每款应用放长期 feature 分支 | 接近上游推荐流程，单个应用起步简单 | 共享修复要逐分支传播；难在同一提交中更新共享组件和全部应用 |
| 自有 monorepo，通过 subtree 导入上游 | 普通 clone 即有源码；本地组件和应用可以在同一提交变更；保留上游来源便于同步 | 需要维护导入流程和本地 CI；频繁修改导入目录会增加合并成本 |
| 外层仓库加上游 submodule | 上游目录结构保留完整，版本指针清楚 | 外层只记录内部提交指针，修改 BSP 需要内部仓库提交和外层指针更新；协作时要处理递归拉取 |

推荐第二种，初期完整导入上游到 vendor/ai-passport，优先保持导入目录原样。它包含源码和资料，有意保留上游目录关系，避免先拆出 BSP 导致补丁和链接难以同步。submodule 适合把上游视作基本不修改的外部 SDK 的团队；如果要经常一起修改 BSP 和应用，subtree 更符合单仓协作目的。

上游采用“main 同步基线、feature/* 长期保存应用”的工作流，详见 [fork 指南](https://github.com/FoloToy/ai-passport/blob/006c2279852e352a8433b2c21e5e09be2ca36f77/docs/fork-guide.zh_CN.md)。本建议的外层仓库是独立产品仓库，自己的 main 保存所有应用；不能把上游同步 workflow 直接安装在外层 main 上。

## 建议的目标结构

下列目录是调研时的目标结构。pomodoro 和 companion 只是应用名称示例；本次先建立引用原始测试源码的 hardware-test 工程，实际结构见架构概览。

```text
ai-passport-playground/
├── AGENTS.md                      # 本仓库入口及上下文路由
├── README.md
├── docs/
│   ├── project-brief.md
│   ├── research/                  # 调研证据及候选方案
│   ├── spec/                      # 仓库范围的产品与技术约束
│   ├── adr/                       # 已确认的架构决策
│   ├── architecture/
│   ├── plans/
│   ├── hardware/                  # 本地实测、勘误及上游资料索引
│   └── development/               # 本仓库构建、同步、验收方法
├── vendor/
│   ├── ai-passport/               # git subtree 导入的完整上游树
│   └── ai-passport-source.json    # URL、ref、完整 SHA、导入方式及本地补丁记录
├── firmware/
│   ├── pomodoro/
│   │   ├── CMakeLists.txt
│   │   ├── main/                  # app_main、页面、状态机和应用任务
│   │   ├── components/            # 只属于本应用的组件
│   │   ├── sdkconfig.defaults
│   │   ├── dependencies.lock
│   │   ├── partitions.csv
│   │   ├── assets/                # 本应用素材
│   │   ├── tests/
│   │   └── docs/                  # 应用需求、操作说明和验收记录
│   └── companion/                # 第二个完整 ESP-IDF 工程
├── components/                    # 已有实际复用需求的本地共享组件
├── config/
│   └── passport.defaults          # 最小板级公共配置
├── assets/                        # 共享字体/图像/音效的源素材及授权说明
├── tools/                         # 构建、资源转换、测试和镜像验证入口
└── .github/workflows/             # 本仓库自己的 CI
```

一份 BSP 源码位于 vendor/ai-passport/components/bsp，不再同时复制到根 components/bsp。需要硬件修复时，在这个受跟踪的导入目录修改并记录补丁；通用修复可以另行向上游提交。不要复制 GPIO、ADC 阈值等常量到各应用；上游 bsp_pins.h 仍是相应板卡定义的代码来源。

根 components/ 按需增加，例如持久化封装、协议解析和通用控件。应用之间禁止直接包含另一个应用的 main/；有第二个实际使用者后再抽取共享接口，避免提前建立庞大通用框架。

## 构建与依赖隔离

ESP-IDF 原生提供 EXTRA_COMPONENT_DIRS，可在多个工程中引用同一组件目录；SDKCONFIG_DEFAULTS 可按顺序组合公共配置与应用配置。见 [ESP-IDF 5.5.3 构建系统](https://docs.espressif.com/projects/esp-idf/en/v5.5.3/esp32c3/api-guides/build-system.html) 和 [多配置示例](https://github.com/espressif/esp-idf/blob/v5.5.3/examples/build_system/cmake/multi_config/README.md)。无需为了 monorepo 引入 Node workspace、Nx、Turborepo 或改换固件构建系统。

示意：firmware/pomodoro/CMakeLists.txt，在对应目录已经建立后使用；这是待验证配置，不代表已通过编译。

```cmake
cmake_minimum_required(VERSION 3.16)
get_filename_component(REPO_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)

set(EXTRA_COMPONENT_DIRS
    "${REPO_ROOT}/vendor/ai-passport/components/bsp"
    "${REPO_ROOT}/components"
)
set(SDKCONFIG_DEFAULTS
    "${REPO_ROOT}/config/passport.defaults"
    "${CMAKE_CURRENT_LIST_DIR}/sdkconfig.defaults"
)
set(SDKCONFIG "${CMAKE_BINARY_DIR}/sdkconfig")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(passport_pomodoro)
```

入口组件在 main/CMakeLists.txt 中通过 REQUIRES bsp 等显式声明依赖。components/ 路径需存在并放置有效组件；若暂时没有共享组件，就省略该搜索路径。

激活固定的 ESP-IDF 5.5.3 环境后，每个应用独立构建，示意：

```sh
idf.py -C firmware/pomodoro -B build/pomodoro -DIDF_TARGET=esp32c3 build
idf.py -C firmware/companion -B build/companion -DIDF_TARGET=esp32c3 build
```

必须分别隔离 build、sdkconfig、dependencies.lock 和 managed_components。dependencies.lock 由组件管理器生成后按应用提交，不手写；managed_components 和构建缓存不提交。初期各应用采用同一工具链和兼容的核心依赖政策，锁文件记录各自实际解析结果。

公共 defaults 只放板级必要配置，例如芯片、Flash、USB 控制台、必要的 LVGL 基线；网络栈、BLE、UI 字体和内存预算由各应用决定。不要把上游硬件测试应用开启的全部功能原封不动变成每款产品的强制配置。

后面的 defaults 可以覆盖前面的默认值，但已有 sdkconfig 不会因 defaults 修改就自动完整重置。CI 应生成新的隔离 sdkconfig；开发环境修改配置策略后需显式迁移或重新生成。每个应用保留自己的分区文件，分区路径按应用工程解析。不要靠切换一份全局 sdkconfig 在应用之间来回构建。

## 上游同步与资料管理

1. 第一次导入固定 SHA，记录 URL、SHA、来源和许可；保留上游 LICENSE 与第三方授权。
2. 导入后先对原始上游工程执行其既有验证，建立可比较的基线；不要同时重构 BSP 和目录。
3. 外层应用只引用导入树中的 BSP；上游 main/ 仍可作为硬件测试工程，不能作为新产品 UI 的起点。
4. 同步时在短期分支执行 subtree 更新，审查 BSP API、pins、依赖、配置和分区变化；同步源码与来源记录在同一提交。
5. 运行所有受影响应用的构建、host tests 和硬件回归，确认后合回自己的 main。无需自动追踪上游最新提交。

subtree 的具体命令和是否压缩历史在实施时确定；若采用 --squash，后续更新应延续同一策略，并在来源清单保留完整上游 SHA。

上游硬件资料留在导入目录，本地 docs/hardware/ 维护索引、板卡版本和实测补充，不手工维护第二套上游全文。共享素材存源文件、生成脚本、用途和授权；只把目标应用实际用到的资源编入 Flash。大型二进制设计稿可按需要使用 LFS 或外部附件，发布固件通过制品系统留存。

导入目录中的 AGENTS/skills 描述其上游工程流程，外层 AGENTS 应明确本地多应用构建和路径规则。不能在外层机械套用上游的长期功能分支、双语配对或根文件检查要求；修改导入树或向上游贡献时，再遵循对应规范。开始实际固件开发前复核上游要求的五个 passport 技能与硬件指南。

## 测试与发布

上游 tools/validate.sh、tools/check_repo.py 以及 workflow 假设单工程根目录、固定的 main/components 路径和 FoloToy-AI-Passport 产物名。不能仅将现有 CI 复制到外层；应保留上游工具以验证导入树，再为自有应用提供带应用路径、构建目录和产物名参数的入口。镜像校验和归档工具是否可直接复用，需要实施时单独验证。

| 变化范围 | 运行检查 |
| --- | --- |
| 仅应用代码 | 应用 host tests、固件构建与镜像检查 |
| 共享组件、BSP、公共配置、工具链或依赖政策 | 所有消费这些内容的应用；初期可简单地全部构建 |
| 仅文档 | 链接与格式检查，无需全量固件编译 |
| 显示、按键、音频、无线或休眠行为 | 额外执行对应真机验收 |

纯状态机、协议和计时逻辑尽量不依赖 LVGL/ESP-IDF，可在主机测试。硬件资源由 BSP/应用生命周期统一管理，不能因为共享了源码就重复初始化 I2C、ADC 或显示驱动。LVGL 多任务访问和慢操作任务隔离需继续满足上游硬件约束。

每款固件独立版本，例如 pomodoro/v0.1.0。发布记录包含应用名、仓库 SHA、上游 SHA、工具链、配置/依赖身份、分区布局、镜像哈希和测试状态；留存同次构建的 ELF/MAP 以便定位崩溃。验证后的 full.bin 用于明确的完整镜像交付，保留 NVS 的增量开发刷写须检查分区兼容性。烧录会修改设备，实施前需用户授权；编译通过不能写成硬件通过。

来源：[上游构建与验证](https://github.com/FoloToy/ai-passport/blob/006c2279852e352a8433b2c21e5e09be2ca36f77/docs/development/engineering/build-and-test.zh_CN.md)、[验证脚本](https://github.com/FoloToy/ai-passport/blob/006c2279852e352a8433b2c21e5e09be2ca36f77/tools/validate.sh)、[发布 workflow](https://github.com/FoloToy/ai-passport/blob/006c2279852e352a8433b2c21e5e09be2ca36f77/.github/workflows/build-firmware.yml)。

## 建议实施顺序

1. 固定并导入上游基线，记录来源；在原样上游工程验证环境和构建。
2. 建立第一款独立应用，引用共享 BSP，实现独立 UI，隔离配置、依赖和构建产物。
3. 增加第二款应用，验证共享 BSP 的实际复用，再抽取确有复用需求的组件。
4. 建立统一验证入口、应用 CI matrix 和独立发布流程。
5. 根据真实需求补充配网、OTA、桌面端或服务端；这些都不是采用 monorepo 的前置条件。

完成标准：全新 clone 可按固定版本构建两款应用；修改共享 BSP 后两款应用都被验证；构建不会覆盖彼此配置与产物；每次发布可追溯其来源和设备验收结果。
