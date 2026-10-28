---
last-reviewed: 2026-10-28
status: accepted
---

# 架构概览

firmware/<app> 是独立 ESP-IDF 工程，依赖本地共享 components 和 vendor/ai-passport/components/bsp。上游通过 subtree --squash 固定版本完整导入；当前无本地补丁。hardware-test 工程直接引用上游 main 和 BSP，验证多应用目录方式，不复制应用源码。

Dockerfile 固定 ESP-IDF 5.5.3 官方镜像 digest，Compose 提供 /workspace 源码挂载、/build 增量目录、独立的 ccache/下载/解包依赖 volumes。tools/dev 是主机入口，tools/firmware.py 在容器内运行上游 gate 或隔离的增量构建。成功归档校验后导出 artifacts/<target>/ 并更新来源记录。

上游完整 gate 使用全新临时目录并执行 host tests；增量构建按配置身份选择持久化目录。它们共享编译器缓存，但不共享生成的 sdkconfig。默认没有设备访问权限或烧录入口。

完整使用方式见 [容器开发指南](../development/docker.md)，决策见 [ADR 0001](../adr/0001-monorepo-container-build.md)，原始方案比较见 [调研](../research/ai-passport-monorepo.md)。
