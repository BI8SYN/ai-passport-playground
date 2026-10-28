---
last-reviewed: 2026-10-28
status: draft
---

# 技术约束

上游调研基线为 ESP32-C3、8 MB Flash、无 PSRAM、ESP-IDF 5.5.3；实际目标板卡版本与连接情况待实施时核实。

每款固件隔离构建目录、sdkconfig、分区、dependencies.lock 和 managed_components。Dockerfile 固定官方 ESP-IDF 5.5.3 镜像 digest；Compose volumes 保留编译、下载和解包缓存。不提交生成缓存、凭证或未经脱敏的设备数据。性能、RAM、Flash、电池续航与联网要求由首款产品应用确定。

验收分别报告构建、主机测试和真机测试。烧录须取得用户授权。当前默认分区没有 OTA 双槽，不能承诺已有 OTA 能力。
