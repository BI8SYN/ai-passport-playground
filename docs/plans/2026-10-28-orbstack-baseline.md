# OrbStack 固件基线实施计划

> For agentic workers: 使用 superpowers:executing-plans，在当前专用干净工作区逐项实施并验证。

**Goal:** 建立可迁移的 monorepo + Docker 环境，验证原始固件，禁止烧录。

**Architecture:** subtree 保留完整上游；官方固定版本镜像承载工具链；Compose 挂载源码和持久化缓存。原始 gate 与共享源码的 hardware-test 工程分别验证。

**Tech Stack:** ESP-IDF 5.5.3、CMake、Docker Compose、Python 标准库、OrbStack ARM64。

**Spec:** 用户已确认的 monorepo/容器方案，以及 docs/research/ai-passport-monorepo.md。

## Global Constraints

- 固定上游 SHA 006c2279852e352a8433b2c21e5e09be2ca36f77，保留其全部源文件与许可证。
- 不安装主机工具链，不访问串口，不烧录、不发布、不推送。
- 不更改上游硬件测试 UI；hardware-test 明确是原始测试工程的包装。
- build、配置和依赖解包按工程隔离，ccache 与下载缓存持久化。
- 官方完整门禁使用全新构建目录，增量构建另设入口。

## Review Focus

- 原始上游导入目录与来源 tree 一致，构建不产生跟踪文件差异。
- 路径穿越或未知工程名不能访问其他目录；入口不支持 flash。
- 清除/重建容器后缓存和增量构建仍保留；变更 defaults 不能悄悄使用旧 sdkconfig。
- 原始 gate 必须执行其静态/host 检查和镜像/ELF 归档验证。
- 失败构建不能发布本轮成功记录或把旧固件报告成新产物。

## Tasks

- [x] 导入完整上游 subtree，检查指南与五个 passport 技能，保留版本来源。
- [x] 配置固定镜像 Dockerfile、Compose、构建和缓存 volumes、无 USB 默认配置。
- [x] 先测试工程解析、非法命令和配置变更识别，再实现统一工具入口。
- [x] 建立 hardware-test 工程，引用上游 main/BSP；保留独立锁与分区。
- [x] 容器运行原始完整 gate，验证合并镜像和 ELF，再验证 monorepo 增量构建与缓存复用。
- [x] 审查、更新 ADR/开发指南与项目入口，保留本地验证结果。

验证命令：docker compose config --quiet；./tools/dev image；./tools/dev test；./tools/dev gate；./tools/dev build hardware-test；./tools/dev cache-stats。
