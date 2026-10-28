---
last-reviewed: 2026-10-28
status: draft
---

# 产品范围

已知目标：在本仓库开发 AI Passport 固件，共享文档、资料和代码。

首款产品固件的用途、页面、按键行为、持久化需求与验收标准待确定。用户已确认先建立支持多个工程的 monorepo，使用 OrbStack Docker，验证原始硬件测试固件，暂不烧录。

当前验收：上游导入可追溯且源码不变；容器内运行原始完整 gate；hardware-test 工程共享同一 main/BSP 并独立构建；容器移除后增量状态及缓存保留；未安装主机工具链或访问设备。产品扩展方案见 [调研](../research/ai-passport-monorepo.md)。
