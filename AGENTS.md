# AI 协作入口

先读 docs/project-brief.md，再按任务阅读 docs/development/docker.md、docs/spec/ 和 docs/architecture/ 中的相关文档。用户已确认采用 monorepo + OrbStack Docker，决定见 docs/adr/0001-monorepo-container-build.md。

对话和本地项目文档默认中文；引入的上游文档和工作流模板保留原文。不要覆盖用户修改。Git 操作前检查状态；不要自动 push、发布或烧录。

上游来源见 vendor/ai-passport-source.json。vendor/ai-passport 是通过 subtree 导入的完整上游，默认保留原样；硬件事实和 BSP 唯一源码位于其中。本地组件放 components/，应用放 firmware/<app>/。新产品不得复制上游硬件测试 UI。

所有 ESP-IDF 命令通过 ./tools/dev 在容器运行，不安装主机工具链。原始门禁：./tools/dev gate；增量构建：./tools/dev build <target>（hardware-test、upstream、resistor-reader）；本地工具测试：./tools/dev test。产物、sdkconfig、managed_components 和缓存不提交。

五个 passport 技能的本地快照位于 .agents/skills/。技能中原上游的相对路径应在 vendor/ai-passport 下解析；本仓库自己的文档和入口规则优先用于外层工程。修改上游时读取其 AGENTS 和相应指南，新增本地文档无需机械遵循上游双语规则。上游更新后同步技能快照。

用户已连接设备并允许只读发现；不要仅因设备连接或编译成功就打开串口、复位或烧录。烧录前确认端口、精确验证产物和数据影响，并取得该次写入的明确授权。当前容器仍无设备访问权限。

构建通过、主机测试通过、真机验收通过必须分别报告。未运行时明确标记；不得声称当前研究方案已通过固件编译。

文档地图及本地协作技能见 CLAUDE.md。架构决定确认后再记入 docs/adr/。

Git 提交统一使用 BI8SYN，邮箱 326114386+BI8SYN@users.noreply.github.com；仅在本仓库配置，不修改全局身份。远程为 git@github-bi8syn:BI8SYN/ai-passport-playground.git，推送使用该 SSH 别名。首次推送前的本地提交身份迁移见 docs/development/git-identity-migration.json；不得推送 refs/backup 或本地构建/凭据。
