---
last-reviewed: 2026-10-28
---

# OrbStack Docker 固件开发

macOS 只需现有的 OrbStack、Docker CLI 和 Git。ESP-IDF、交叉编译器、Python 依赖、CMake、Ninja、ccache 和 actionlint 均在容器内。本配置不访问 USB，不执行烧录。

## 开始使用

在仓库根目录执行：

```sh
./tools/dev image
./tools/dev doctor
./tools/dev test
./tools/dev gate
./tools/dev build hardware-test
./tools/dev cache-stats
```

首次镜像下载体积较大。基础镜像为官方 ESP-IDF v5.5.3，Dockerfile 固定多平台 manifest digest；Docker 自动选择本机架构。不要把 latest 用于可复现构建。actionlint 1.7.12 从其发布者的固定 digest 多平台镜像复制静态二进制，无需在 RUN 步骤下载 GitHub release 或安装系统包。

`gate` 在 vendor/ai-passport 中执行原始 tools/validate.sh，不修改上游脚本。它包含仓库检查、workflow 检查、全部 host tests、全新临时目录中的 ESP-IDF 构建、镜像合并、分区/镜像边界校验以及 ELF 匹配验证。

`build hardware-test` 是 monorepo 独立工程的增量构建，复用上游 main 和 BSP；`build upstream` 则增量编译原始工程。两者会合并并验证镜像、保留与镜像匹配的 ELF/MAP，但不会代替完整 gate 的 host tests。

## 缓存与配置

| 内容 | 容器内路径 | 存储方式 |
| --- | --- | --- |
| 源码 | /workspace | 当前项目 bind mount |
| 增量编译和隔离 sdkconfig | /build/<target>/<config-id> | builds volume |
| 编译器缓存 | /cache/ccache | 独立 ccache volume，最大 2 GB |
| 组件下载缓存 | /cache/components | component-cache volume |
| 其他用户缓存 | /cache/xdg | user-cache volume |
| 上游解包组件 | /workspace/vendor/ai-passport/managed_components | managed-upstream volume |
| hardware-test 解包组件 | /workspace/firmware/hardware-test/managed_components | managed-hardware-test volume |
| 原始 gate 输出 | /workspace/vendor/ai-passport/build | upstream-gate-artifacts volume |
| 验证后的交付文件 | /workspace/artifacts/<target>/ | 导出到主机，Git 忽略 |

`docker compose run --rm` 只删除执行容器，volumes 继续保留；不需要常驻容器。不要例行运行 docker compose down -v，它会删除这些项目缓存。多个 checkout 并行工作时，可为每个 checkout 设置不同的 COMPOSE_PROJECT_NAME，避免共享工作目录 volume。

源码修改不重建镜像，Ninja 做增量编译；固件组件依赖修改由 ESP-IDF 处理。工具链或开发工具依赖变化才重建镜像。Docker 会复用已下载的镜像层和构建步骤；未来增加 apt/pip 工具时，再在相应安装步骤增加 BuildKit 下载缓存和版本锁。

为避免已有 sdkconfig 遮蔽 defaults 修改，构建工具根据 CMake、defaults、Kconfig、分区、manifest/锁和工具链定义选择隔离构建目录。配置改变后生成新目录，旧缓存保留；普通 C/C++ 源码修改仍使用原目录增量构建。公共 defaults、SDK 版本或共享组件配置变化会更新相应目录。不要直接修改生成的 sdkconfig 作为产品配置；需将有意更改记入应用 defaults。

上游完整 gate 仍使用新的 /tmp 构建目录，不复用增量 sdkconfig 或 Ninja 状态；它可以复用独立 ccache 和组件下载缓存。这样保留原始门禁语义，同时加速重复编译。

## 产物与身份

只有当前命令成功且归档再次验证通过后，才更新 artifacts/<target>/latest.json。该文件记录镜像 SHA256、匹配 ELF 哈希、固定上游 SHA、monorepo 提交及 dirty 状态、IDF 版本、基础镜像身份和归档路径。

归档存于 artifacts/<target>/<full-bin-sha256>/，保留原始 manifest.json、full.bin、应用 bin、ELF、MAP、bootloader、分区表和 flash_args。<sha>.build.json 是同级来源记录；不向严格验证的上游归档内部添加文件。

失败时旧归档及 latest.json 可能仍然存在；旧成功记录不是本轮构建成功。以命令退出状态、完成时间及日志为准。本地 firmware/ELF 不自动上传或提交。

## 更新上游

先检查工作区，再在本地短期分支执行 subtree 更新：

```sh
git fetch --no-tags https://github.com/FoloToy/ai-passport.git <完整提交SHA>
git subtree merge --prefix=vendor/ai-passport --squash FETCH_HEAD
```

同步修改 vendor/ai-passport-source.json，审查硬件事实、依赖、构建与分区变化，更新安装在 .agents/skills 的 passport 技能快照，再运行完整 gate 和所有应用构建。不要把上游 sync-main workflow 安装到外层 .github/workflows；本仓库 main 保存自己的多应用结构。

当前导入树没有本地补丁。文件来源和导入提交可追溯到 subtree 元数据及来源清单；没有创建上游子模块或嵌套 Git 仓库。

## 当前边界

本配置未开放 privileged、devices、宿主 USB、Docker socket 或任何端口。用户已明确本阶段不烧录，因此也不执行设备枚举、串口打开或复位。后续获授权后再独立设计设备访问配置。

Dockerfile 和锁文件固定版本有助于重复构建，但目前不承诺不同时间和机器生成逐字节相同镜像；构建时间、源码版本描述等仍可能影响固件身份。
