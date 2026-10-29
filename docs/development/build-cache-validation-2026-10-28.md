# 陈旧构建目录清理验收

日期：2026-10-28。代码改动为本地构建工具及文档，未修改固件源码或烧录设备。

- `./tools/dev test`：20 项通过，含预览不删除、普通/性能模式分开保留、当前配置双模式保护、旧 Ninja 目录排序、非法路径/符号链接保护、并发构建跳过，以及归档导出失败不得触发自动清理。
- 代码审查发现自动清理起初只保护当前模式；已修复为同时保护当前配置的普通和 benchmark 目录，补充测试并复核通过。
- 先执行 `./tools/dev clean-builds`，再执行 `./tools/dev clean-builds --apply`。删除 resistor-reader 的 `2895161b1880dd0c` 和 `62b6fdc5ae3e6353`，共回收 470,896,640 字节，约449 MiB。保留最近两套普通构建和一套性能构建，其他工程无删除项。
- 再次预览：候选目录为空，无错误。
- 哈希验证：Docker `/build/resistor-reader/archives` 和主机 `artifacts/resistor-reader` 均保留此前提交审核的完整镜像 SHA256 `ad11bb24569be961b416ac0765923ffc2bad4a06580c813310378aff7fd4da10`，同一归档内匹配 ELF SHA256 `ad62d80c0d1256f1a11606c53d666a1cc1481a978ac2ac6bbe21078cbf684476`。
- `git diff --check` 通过。未重新编译固件；自动清理调用顺序由测试验证，本次真实删除通过手动入口完成。

本地日志（Git忽略）：`artifacts/logs/build-cache-{tests-green,preview.json,apply.json,after.json,archives-verified}.log`。

使用规则见 [容器开发指南](docker.md#陈旧构建目录清理)。新固件调研独立记录在 [练耳可行性](../spec/ear-trainer.md)。
