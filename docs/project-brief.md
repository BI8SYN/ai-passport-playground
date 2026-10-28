---
last-reviewed: 2026-10-28
---

# AI Passport Playground 项目简述

用于开发 FoloToy AI Passport 固件，并共享硬件资料、开发经验、驱动和通用代码。

用户已确认采用 monorepo + OrbStack Docker。上游已通过 subtree 完整导入，来源固定至 006c2279852e352a8433b2c21e5e09be2ca36f77。容器、缓存及共享源码的 hardware-test 工程已建立并通过真实构建验证。首款产品为独立的4/5/6环色环电阻读取器，见 [应用说明](../firmware/resistor-reader/README.md)。

已接受决策见 [ADR 0001](adr/0001-monorepo-container-build.md)：多个独立 ESP-IDF 工程共享一份上游 BSP；固定 ESP-IDF 5.5.3，各应用独立配置、依赖锁和构建目录。使用方式见 [容器开发指南](development/docker.md)。

| 日期 | 变化 |
| --- | --- |
| 2026-10-28 | 完成上游结构调研，初始化协作文档 |
| 2026-10-28 | 导入上游、建立 Docker/Compose、缓存与 hardware-test 工程 |
| 2026-10-28 | 原始完整 gate、monorepo 构建、增量状态和 ccache 命中验证通过 |

验证结果见 [构建验收记录](development/validation-2026-10-28.md)。原始完整 gate、全部上游 host tests、monorepo 编译及镜像/ELF 归档均 PASS；6 项工具测试 PASS。新容器复用了增量构建状态，强制单文件重编译实际命中 ccache，产物哈希保持不变。真机测试 NOT RUN；全程未访问设备。

首款产品已实现独立 main/UI、配置、模型测试、字体生成、UI预览和打包规则。用户确认简化界面并授权烧录，随后确认确定键短按下一环/400ms长按上一环并要求性能优化。已取消双击等待、离线烘焙绘图和局部刷新；同板整图从约475ms降至3.0–3.2ms，已刷回普通性能版并通过写入校验和20秒启动观察，实际按键手感待用户反馈。精确镜像与ELF身份见 [色环固件验收记录](development/resistor-reader-validation-2026-10-28.md)。
