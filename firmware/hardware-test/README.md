# 原始硬件测试

这是共享源码的构建工程，不是新的产品应用。应用源码直接来自 ../../vendor/ai-passport/main，BSP 来自同一上游树的 components/bsp。

CMake 依次读取公共硬件配置、上游原始 sdkconfig.defaults、当前工程的覆盖文件。分区表和初始依赖锁复制自固定上游基线，作为本工程独立维护的配置；初次构建须检查依赖锁是否产生差异。

```sh
./tools/dev build hardware-test
```

从仓库根目录运行。编译、配置及已解包依赖保留在 Compose volumes，验证通过的固件和 ELF/MAP 导出至 artifacts/hardware-test/。
