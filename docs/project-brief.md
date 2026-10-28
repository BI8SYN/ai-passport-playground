---
last-reviewed: 2026-10-28
---

# AI Passport Playground 项目简述

用于开发 FoloToy AI Passport 固件，并共享硬件资料、开发经验、驱动和通用代码。

用户已确认采用 monorepo + OrbStack Docker。上游已通过 subtree 完整导入，来源固定至 006c2279852e352a8433b2c21e5e09be2ca36f77。容器、缓存及共享源码的 hardware-test 工程已建立并通过真实构建验证。首款产品应用需求尚未确定。

已接受决策见 [ADR 0001](adr/0001-monorepo-container-build.md)：多个独立 ESP-IDF 工程共享一份上游 BSP；固定 ESP-IDF 5.5.3，各应用独立配置、依赖锁和构建目录。使用方式见 [容器开发指南](development/docker.md)。

| 日期 | 变化 |
| --- | --- |
| 2026-10-28 | 完成上游结构调研，初始化协作文档 |
| 2026-10-28 | 导入上游、建立 Docker/Compose、缓存与 hardware-test 工程 |
| 2026-10-28 | 原始完整 gate、monorepo 构建、增量状态和 ccache 命中验证通过 |

验证结果见 [构建验收记录](development/validation-2026-10-28.md)。原始完整 gate、全部上游 host tests、monorepo 编译及镜像/ELF 归档均 PASS；6 项工具测试 PASS。新容器复用了增量构建状态，强制单文件重编译实际命中 ccache，产物哈希保持不变。真机测试 NOT RUN；全程未访问设备。

下一步：确定首款产品固件的需求，添加其独立 main/UI、配置、测试和打包规则。
