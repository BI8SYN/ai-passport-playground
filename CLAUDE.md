# 项目协作说明

必读入口：[AGENTS.md](AGENTS.md)。

## Language Preferences

对话和本地文档默认中文，由本次交互语言推定，可随用户要求调整。复制的工作流模板保留英文。

## Documentation Map

| 用途 | 路径 |
| --- | --- |
| 项目入口 | docs/project-brief.md |
| 产品范围 | docs/spec/product-spec.md |
| 技术约束 | docs/spec/technical-spec.md |
| 架构决策 | docs/adr/README.md |
| 架构概览 | docs/architecture/overview.md |
| 模块文档 | docs/architecture/modules/ |
| 调研 | docs/research/ |
| 开发计划 | docs/plans/ |
| 项目技能 | .claude/skills/ |
| 项目 agent 模板 | .claude/agents/ |

## Session Start Protocol

先读项目简述并执行轻量文档新鲜度检查；发现漂移时说明，但不阻塞用户任务。Claude Code 会运行本地 SessionStart hook；Codex 按上述路径手动检查，不声称运行了 Claude hook。

## Git Conventions

以下为初始化默认建议，尚未经用户选择：GitHub Flow、主分支 main、短期 feat/fix/docs/chore 分支、Conventional Commits、PR 审阅后 squash merge。PR 门禁待实际构建入口建立后确定。不把各应用长期存放在不同分支。

后续 commit、push、远端创建和发布按用户授权执行；本次文档初始化由 bootstrap 工作流建立初始提交。

## Development Workflow

按任务使用本地需求收集、架构文档、Git 工作流、漂移检查和文档同步技能；适用时配合已安装的 Superpowers。产品需求尚未确定，不创建假定功能。每次完成实现后更新项目简述及相关权威文档。

工作流资源来自 loom 初始化，已复制到本项目，不依赖 loom 在运行时提供；Superpowers 引用仍依赖对应已安装技能。
