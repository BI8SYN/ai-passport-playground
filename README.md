# AI Passport Playground

FoloToy AI Passport 固件 monorepo，使用 OrbStack Docker 隔离 ESP-IDF 开发环境。

```sh
./tools/dev image
./tools/dev doctor
./tools/dev gate
./tools/dev build hardware-test
./tools/dev build resistor-reader
./tools/dev preview resistor-reader
```

`gate` 验证原始上游工程；`hardware-test` 是引用同一上游 main/BSP 的独立工程。
构建缓存保存在 Docker volumes，验证后的固件及 ELF/MAP 导出至 Git 忽略的 artifacts/。
默认配置没有设备权限，也没有烧录命令。

- [首款应用：色环电阻读取器](firmware/resistor-reader/README.md)
- [色环固件验收与产物身份](docs/development/resistor-reader-validation-2026-10-28.md)
- [项目简述](docs/project-brief.md)
- [Monorepo 调研与建议](docs/research/ai-passport-monorepo.md)
- [架构概览](docs/architecture/overview.md)
- [容器构建与缓存指南](docs/development/docker.md)
- [已完成的构建验证](docs/development/validation-2026-10-28.md)
- [固件工程约定](firmware/README.md)
- [上游版本来源](vendor/ai-passport-source.json)
- [AI 协作入口](AGENTS.md)
