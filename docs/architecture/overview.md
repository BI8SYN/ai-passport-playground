---
last-reviewed: 2026-10-28
status: proposal
---

# 架构概览

候选架构为 firmware/<app> 独立 ESP-IDF 工程，依赖本地共享 components 和 vendor/ai-passport/components/bsp。上游通过 subtree 固定版本导入，原始目录尽量保留。配置、依赖锁和产物按应用隔离。

完整目录、方案比较、构建示例和演进顺序见 [monorepo 调研](../research/ai-passport-monorepo.md)。当前源码目录尚未创建；不能将目标结构视为已实现。
