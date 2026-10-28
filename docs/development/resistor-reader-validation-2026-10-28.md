# 色环电阻读取器验收记录（2026-10-28）

原环境分支已快进合入 main（d302795）。应用位于 feature/resistor-reader；实现提交 429a19d1e2fe1f2b579bb62c811e956b1a63f32b。本记录追加于构建之后，不改动应用源码。

## Build — PASS

`./tools/dev build resistor-reader`，ESP-IDF5.5.3，OrbStack Linux ARM64，构建时工作区 clean。日志 artifacts/logs/resistor-build-final.log。

- 完整镜像：artifacts/resistor-reader/246b0e382daedea93818c49f258a7c14da878a8481aae60edf75b0796f035da7/resistor-reader-full.bin
- 完整镜像 SHA256：`246b0e382daedea93818c49f258a7c14da878a8481aae60edf75b0796f035da7`
- 匹配 ELF SHA256：`f7ec01c903620dcddd07a51a52f9b5662414d1eb2ab1c1ed44dbab1d64ad9d16`
- 项目/版本：resistor-reader / 429a19d；ESP32-C3，8MB flash。
- 合并镜像 737568 字节，从0x0开始，范围0x0–0xb411f（含末字节）；物理写入还会按扇区对齐擦除。
- bootloader 0x0、分区表0x8000、应用0x10000；镜像内容、分区MD5、地址/容量/重叠、项目名、ELF头和嵌入ELF SHA256全部验证通过。
- 归档包含完整及分段镜像、ELF、MAP、flash_args、manifest；artifacts/resistor-reader/latest.json 及同级build.json记录来源。交付时使用这里指定的哈希目录，不用旧的调试版本。

## Host tests — PASS

- `./tools/dev test`：10项测试，含纯C模型测试程序；标准阻值、上下界、小数倍率、容差、温度系数、合法颜色循环、按键/环数转换、格式化及产品打包与篡改拒绝。日志 artifacts/logs/resistor-all-tests.log。
- `./tools/dev preview resistor-reader`：同一UI/字体/模型与锁定LVGL9.5.0原生渲染240×320；真实字形检查、容差文案断言和500次状态更新通过，32KiB LVGL堆。日志 artifacts/logs/resistor-preview-final.log。
- 已目视检查4环及6环PNG，修复LVGL内置格式化器不支持浮点`%g`的问题；容差先由libc格式化后传入LVGL。预览位于 artifacts/resistor-reader-preview/。
- `./tools/dev gate`：原始上游仓库/工作流检查、host tests、全新临时目录编译、完整镜像/ELF归档通过，未修改vendor。日志 artifacts/logs/resistor-upstream-gate.log。
- `docker compose config --quiet`、Git diff空白检查通过。独立只读代码审查未发现新增问题。

## Device tests — NOT RUN

用户设备已连接；仅通过OrbStack只读串口枚举发现 `/dev/cu.usbmodem11101`（及对应tty）。未打开串口、复位、进入下载模式或写入设备；没有自动烧录或整片擦除。

## Unverified

真实屏幕显示、硬件按键/双击/长按、电池电量、持续运行及实际设备身份仍待烧录后验收。主机预览不能代替实物检查；待验收步骤见 firmware/resistor-reader/README.md。

完整镜像包含分区和中间填充区，可能重置NVS/设置。若用户要求保留设置，先核对现有分区布局并选择兼容的分段写入方案；本次未授权任何写入。
