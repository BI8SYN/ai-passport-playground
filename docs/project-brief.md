---
last-reviewed: 2026-10-28
---

# AI Passport Playground 项目简述

用于开发 FoloToy AI Passport 固件，并共享硬件资料、开发经验、驱动和通用代码。

用户已确认采用 monorepo + OrbStack Docker。上游已通过 subtree 完整导入，来源固定至 006c2279852e352a8433b2c21e5e09be2ca36f77。已建立容器、缓存及共享源码的 hardware-test 工程；当前正在执行首次镜像下载与实际构建验证。首款产品应用需求尚未确定。

已接受决策见 [ADR 0001](adr/0001-monorepo-container-build.md)：多个独立 ESP-IDF 工程共享一份上游 BSP；固定 ESP-IDF 5.5.3，各应用独立配置、依赖锁和构建目录。使用方式见 [容器开发指南](development/docker.md)。

| 日期 | 变化 |
| --- | --- |
| 2026-10-28 | 完成上游结构调研，初始化协作文档 |
| 2026-10-28 | 导入上游、建立 Docker/Compose、缓存与 hardware-test 工程 |

当前检查：本地工具测试 6 项通过，Compose 配置有效，上游 tree 与固定提交一致。原始完整 gate 和 monorepo 编译尚待容器镜像就绪后执行。用户明确本阶段不烧录，不访问设备。
