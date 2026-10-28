---
last-reviewed: 2026-10-28
---

# 2026 年 10 月 28 日构建验证结果

原始上游完整门禁和 monorepo hardware-test 工程均已在 OrbStack Docker ARM64 环境通过验证。没有安装主机 ESP-IDF 工具链，没有枚举、打开或刷写设备。

## 来源与环境

- 上游 SHA：006c2279852e352a8433b2c21e5e09be2ca36f77。
- 上游 tree：c522c9c8ee7d0bf57eb2b80b72424826b753cd08；subtree 导入及构建后均无上游跟踪文件差异。
- 本轮构建源码提交：c79db8f38a0be5cb81bc98e3dc188dde95b8ff35；构建时工作区干净。
- macOS ARM64、OrbStack 2.2.4、Docker Engine 29.8.2。
- ESP-IDF v5.5.3、RISC-V GCC 14.2.0、Python 3.12.3、ccache 4.9.1、actionlint 1.7.12。
- 开发镜像：ai-passport-sdk:idf5.5.3，ID 为 sha256:b482d01d12ab0d6a42769e6a88352e4bbf20911bb0f4a8be75dabd4f36f33054，平台 arm64。

首次 Docker Hub 大层下载停滞后，通过 Google Docker Hub 缓存下载相同 digest 的镜像完成初始化。随后使用仓库默认官方来源重新执行 ./tools/dev image，复用缓存成功，生成相同镜像 ID。没有修改全局 Docker/OrbStack 配置。

## 检查结果

| 检查 | 命令或证据 | 结果 |
| --- | --- | --- |
| Compose 配置 | docker compose config --quiet | PASS |
| 镜像默认来源重建 | ./tools/dev image | PASS，镜像层复用 |
| 容器环境检查 | ./tools/dev doctor | PASS |
| 本地工具测试 | ./tools/dev test / 主机 Python unittest | PASS，6 项 |
| 原始仓库检查 | 上游 tools/validate.sh，340 个文本文件 | PASS |
| 原始 workflow 与 host tests | ./tools/dev gate 内原始静态门禁 | PASS |
| 原始固件构建、合并与归档 | ./tools/dev gate | PASS |
| monorepo 共享源码工程 | ./tools/dev build hardware-test | PASS |
| 新容器中的重复增量构建 | 第二次 ./tools/dev build hardware-test | PASS，无源码重新编译，仅必要的尺寸/bootloader 检查 |
| ccache 实际复用 | 仅更新 main.c 的 mtime，再构建 | PASS，新增 1 次编译调用且直接命中，产物哈希不变 |
| 依赖锁及上游源码保持原样 | git diff -- vendor/ai-passport firmware/hardware-test/dependencies.lock | PASS，无差异 |
| 真机测试 | 用户要求暂不烧录 | NOT RUN |

配置首次生成时出现 LVGL 可选 Kconfig 尚未加载的警告，ESP-IDF 自动重新配置后成功；依赖管理器提示有新版本可用，仍使用原始锁定版本，没有升级依赖。

## 精确产物身份

原始工程：

- 合并镜像 SHA256：d2a8e05155691e7100076f49fc2bf69e55ec67604de537db7ae00356536b7c70。
- 匹配 ELF SHA256：05f20d391f83edefe62264a0b6a66def905337351d07547b82fabdbef8bd7e84。
- 本地目录：artifacts/upstream/d2a8e05155691e7100076f49fc2bf69e55ec67604de537db7ae00356536b7c70/。

Monorepo hardware-test 工程：

- 合并镜像 SHA256：04aa0153c8da12677fc65f812a67409f9762b99923d9a92085845fb6f4c40cac。
- 本地目录：artifacts/hardware-test/04aa0153c8da12677fc65f812a67409f9762b99923d9a92085845fb6f4c40cac/。

两款构建使用同一上游应用源码和 BSP，但项目路径、配置路径、构建时间等身份不同，因此不要求二进制哈希相同。各自的 manifest 和 latest.json 记录匹配的 ELF 身份和来源。

本轮应用镜像均为 1,527,616 字节；合并镜像为 1,593,152 字节。应用位于 0x10000 的 factory 分区，分区大小 8,323,072 字节。验证确认镜像偏移、分区 MD5、边界、非重叠和 ELF 内嵌身份匹配。这里只验证文件，未写入设备。

ccache 最终累计 3,713 次编译调用中有 1 次直接命中，其余为两条构建路径的首次冷编译；这证明本次强制单文件重编译可以跨执行容器复用缓存，不代表完整工程的总体命中率为 100%。

## 复现与日志

日常使用见 [容器开发指南](docker.md)。未提交的本地日志位于 artifacts/logs/，主要包括 upstream-gate.log、hardware-test-build.log、hardware-test-incremental.log、hardware-test-ccache.log、cache-final.log 和 image-rebuild-default.log。验证后的归档和日志均保留在本机，没有提交或上传。

Build: PASS。Host tests: PASS。Device tests: NOT RUN。Unverified: 显示、按键、音频、无线、休眠和功耗等真实硬件行为。
