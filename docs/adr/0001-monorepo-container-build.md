---
last-reviewed: 2026-10-28
status: accepted
---

# 采用 monorepo 与容器化固件构建

用户在 2026-10-28 确认采用 monorepo + OrbStack Docker，并要求导入上游、配置 Dockerfile/Compose/缓存、验证原始固件，暂不烧录。

决定：完整上游以 git subtree --squash 导入 vendor/ai-passport；各应用独立工程共享同一 BSP；工具链使用固定 digest 的官方 ESP-IDF 5.5.3 镜像，Compose volumes 保存编译和下载缓存。保留原始 gate，并增加 monorepo hardware-test 包装以验证共享源码构建。

替代方案：长期 feature 分支便于跟随官方示例，但共享修复分散；submodule 清晰表达外部 SDK 版本，但增加提交和指针更新步骤；Linux machine 便于长期探索，却需要额外环境管理才能与 CI 一致。本项目优先选择可声明、可重建的镜像。

后果：本仓库需要维护外层构建入口和每应用验证策略。上游原始目录不承担本仓库产品目录或 CI 治理；上游升级需审查并回归。容器重建不会清空 volumes，但首次镜像下载及本地缓存占用较大。

当前阶段不进行设备访问、烧录、发布或远端推送。
