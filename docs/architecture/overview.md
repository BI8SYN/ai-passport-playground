---
last-reviewed: 2026-10-28
status: accepted
---

# 架构概览

firmware/<app> 是独立 ESP-IDF 工程，依赖本地共享 components 和 vendor/ai-passport/components/bsp。上游通过 subtree --squash 固定版本完整导入；当前无本地补丁。hardware-test 工程直接引用上游 main 和 BSP，验证多应用目录方式，不复制应用源码。

Dockerfile 固定 ESP-IDF 5.5.3 官方镜像 digest，Compose 提供 /workspace 源码挂载、/build 增量目录、独立的 ccache/下载/解包依赖 volumes。tools/dev 是主机入口，tools/firmware.py 在容器内运行上游 gate 或隔离的增量构建。成功归档校验后导出 artifacts/<target>/ 并更新来源记录。

上游完整 gate 使用全新临时目录并执行 host tests；增量构建按配置身份选择持久化目录。它们共享编译器缓存，但不共享生成的 sdkconfig。默认没有设备访问权限或烧录入口。

完整使用方式见 [容器开发指南](../development/docker.md)，决策见 [ADR 0001](../adr/0001-monorepo-container-build.md)，原始方案比较见 [调研](../research/ai-passport-monorepo.md)。

resistor-reader 是首个独立产品，纯 C 色码模型与 LVGL UI 分离，按键通过队列进入 UI 任务。原生预览复用同一 UI/字体；字体工具独立使用按 digest 固定的 Node 容器。产品打包复用上游格式解析并验证自身项目名及镜像/ELF身份。

`ear-trainer` 是独立离线练耳产品，乐理/计分/控制器与合成器可在主机测试；音频持续送PCM，UI与NVS存储分任务。音频按最高重叠声部固定增益，界面音量映射以板级音频示例为参考的Codec档位。三键使用按下/释放事件，节奏按下直接记时间，UI不充当音频时钟。
