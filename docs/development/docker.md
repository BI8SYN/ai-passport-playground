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
./tools/dev build resistor-reader
./tools/dev preview resistor-reader
./tools/dev cache-stats
```

首次镜像下载体积较大。基础镜像为官方 ESP-IDF v5.5.3，Dockerfile 固定多平台 manifest digest；Docker 自动选择本机架构。不要把 latest 用于可复现构建。actionlint 1.7.12 从其发布者的固定 digest 多平台镜像复制静态二进制，无需在 RUN 步骤下载 GitHub release 或安装系统包。

若 Docker Hub 大层下载停滞，可尝试同 digest 的 Google Docker Hub 缓存，仅覆盖本次构建参数，不修改全局 daemon：

```sh
docker compose build \
  --build-arg IDF_IMAGE=mirror.gcr.io/espressif/idf@sha256:8ccd4d2ce413889c6c2bba57e986c670302094efb91c913c6091152e317a7805 \
  --build-arg ACTIONLINT_IMAGE=mirror.gcr.io/rhysd/actionlint@sha256:b1934ee5f1c509618f2508e6eb47ee0d3520686341fec936f3b79331f9315667 \
  sdk
```

缓存服务不保证长期保留所有镜像；缺失时使用原始来源。镜像内容仍由固定 digest 校验。本项目的 [首次验收记录](validation-2026-10-28.md)包含镜像身份、编译和缓存实测结果。

`gate` 在 vendor/ai-passport 中执行原始 tools/validate.sh，不修改上游脚本。它包含仓库检查、workflow 检查、全部 host tests、全新临时目录中的 ESP-IDF 构建、镜像合并、分区/镜像边界校验以及 ELF 匹配验证。

`build hardware-test` 是 monorepo 独立工程的增量构建，复用上游 main 和 BSP；`build upstream` 则增量编译原始工程。两者会合并并验证镜像、保留与镜像匹配的 ELF/MAP，但不会代替完整 gate 的 host tests。

`build ear-trainer` 构建离线练耳产品，`preview ear-trainer` 渲染其真实 LVGL 页面并检查字形。各产品分别保留锁文件、配置、构建与固件/ELF归档。

`build resistor-reader` 构建独立产品并验证其项目名、分区、完整镜像及 ELF 身份；`preview resistor-reader` 在 SDK 容器中编译原生 LVGL，渲染同一份产品 UI 并检查字形。字体再生成见 [字体来源](../../assets/fonts/README.md)。

`build rf-link-handmic` 构建正式BLE手咪，`preview rf-link-handmic` 渲染同一份LVGL界面、检查全部显示字形和100次页面切换；接入合同见[协议包](../spec/rf-link-handmic/README.md)。此target不执行烧录。

RF Link当前仅使用ADPCM，Opus启动自测入口已移除；历史压力数据见[真机记录](rf-link-opus-device-benchmark-2026-11-02.md)。

`build ble-signer-lab` 构建独立 BLE 远程签名实验固件；配套电脑 Web 页面及验收步骤见 [实验说明](../../firmware/ble-signer-lab/README.md)。实验不使用正式证书、不写 eFuse。

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
| ear-trainer 解包组件 | /workspace/firmware/ear-trainer/managed_components | managed-ear-trainer volume |
| resistor-reader 解包组件 | /workspace/firmware/resistor-reader/managed_components | managed-resistor-reader volume |
| rf-link-handmic 解包组件 | /workspace/firmware/rf-link-handmic/managed_components | managed-rf-link-handmic volume |
| 字体工具依赖与下载缓存 | /workspace/tools/fonts/node_modules、/root/.npm | font-node-modules / font-npm-cache volumes |
| 原始 gate 输出 | /workspace/vendor/ai-passport/build | upstream-gate-artifacts volume |
| 验证后的交付文件 | /workspace/artifacts/<target>/ | 导出到主机，Git 忽略 |

`docker compose run --rm` 只删除执行容器，volumes 继续保留；不需要常驻容器。不要例行运行 docker compose down -v，它会删除这些项目缓存。多个 checkout 并行工作时，可为每个 checkout 设置不同的 COMPOSE_PROJECT_NAME，避免共享工作目录 volume。

源码修改不重建镜像，Ninja 做增量编译；固件组件依赖修改由 ESP-IDF 处理。工具链或开发工具依赖变化才重建镜像。Docker 会复用已下载的镜像层和构建步骤；未来增加 apt/pip 工具时，再在相应安装步骤增加 BuildKit 下载缓存和版本锁。

为避免已有 sdkconfig 遮蔽 defaults 修改，构建工具根据 CMake、defaults、Kconfig、分区、manifest/锁和工具链定义选择隔离构建目录。配置改变后生成新目录；普通 C/C++ 源码修改仍使用原目录增量构建。陈旧目录按下述保留规则回收。公共 defaults、SDK 版本或共享组件配置变化会更新相应目录。不要直接修改生成的 sdkconfig 作为产品配置；需将有意更改记入应用 defaults。

上游完整 gate 仍使用新的 /tmp 构建目录，不复用增量 sdkconfig 或 Ninja 状态；它可以复用独立 ccache 和组件下载缓存。这样保留原始门禁语义，同时加速重复编译。

## 陈旧构建目录清理

每个工程默认保留最近使用的 **2 套普通构建 + 1 套 benchmark 构建**。成功构建、验证并导出归档后自动执行一次；失败的构建不触发自动清理。构建开始和完成会更新 `.last-used`，旧目录兼容使用 Ninja 日志及目录修改时间判断先后。

手动命令默认只预览候选目录及预计回收字节数：

```sh
./tools/dev clean-builds
./tools/dev clean-builds resistor-reader
./tools/dev clean-builds --apply
./tools/dev clean-builds resistor-reader --keep 3 --keep-benchmark 1 --apply
```

`--keep` 最小为 1，`--keep-benchmark` 最小为 0；手动参数只影响本次清理，自动清理仍按 2+1。当前配置目录即使较旧也会受保护，并计入对应保留数量；保护数量超过配额时优先保护。

仅删除 `/build/<target>/` 下名称严格匹配 16 位配置哈希（可带 `-benchmark`）的普通目录。不会清理 `archives/`、主机 `artifacts/`、预览目录、组件依赖、ccache、镜像或其他项目的 volumes，也不会沿符号链接删除外部文件。每个工程与构建共用互斥锁，手动清理遇到正在构建的工程会跳过。删除失败会报告路径；手动清理返回失败状态，自动清理失败不把已验证的构建改判为失败。

已清理的目录在下次切回对应配置时可重新生成，但首次编译会慢一些；仍能利用 ccache 和组件下载缓存。保留的是增量编译状态，不代表只保留这些版本的固件；固件及匹配 ELF 的归档始终单独保存。

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

本配置未开放 privileged、devices、宿主 USB、Docker socket 或任何端口。设备发现可在用户连接设备后只读进行；串口打开、复位及写入须按精确产物、端口及数据影响取得授权，后续独立设计设备访问配置。

Dockerfile 和锁文件固定版本有助于重复构建，但目前不承诺不同时间和机器生成逐字节相同镜像；构建时间、源码版本描述等仍可能影响固件身份。
