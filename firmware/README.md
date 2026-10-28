# 固件工程

每个子目录是独立 ESP-IDF 工程，独立拥有配置、分区和 dependencies.lock。

- `hardware-test/`：上游原始硬件测试的 monorepo 包装，直接引用 vendor 中的 main 和 BSP，不复制驱动或应用源码。
- `upstream`：构建工具中的别名，直接构建 vendor/ai-passport 原始工程，不对应本目录中的另一份源码。

新产品使用自己的 main/ 和 UI，通过 ../../cmake/passport.cmake 引入同一份 BSP。不要把 hardware-test 的测试菜单当作产品界面。配置采用公共板级 defaults 与本应用 defaults；不必继承 hardware-test 开启的 Wi-Fi/BLE 等测试配置。

新应用还需为 Compose 增加独立 managed_components volume，并为 tools/firmware.py 增加目标与对应的镜像验证/打包规则。当前验证工具只注册原始基线和 hardware-test，避免把上游固定产物名的校验器误用于其他产品。

构建方法见 [容器开发指南](../docs/development/docker.md)。
