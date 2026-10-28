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

## UI仿真修订（尚未提交，待外观反馈）

用户否决首版扁平、对称的电阻图。现改为带鼓形肩部、收腰本体、曲面涂层/金属反光、金属引脚及投影的RGB565程序绘图；4/5环拉开右端容差环，6环右端容差和温度系数组与数字/倍率组拉开，配合环号和左起读取箭头。选中标记在本体外。

- Build PASS：日志 artifacts/logs/resistor-build-v2.log，当前工作区dirty为true，基础提交d3053bb，非此前已提交的UI。完整镜像SHA256 `91e292976fdfe619f563418c892bba2782a488e4e80dcc2ff9ccc9bd2784bde5`，ELF SHA256 `b0bca3e31752885f17812ef67553af05dffd15c72f33a489346342e117120e60`；归档 artifacts/resistor-reader/91e292976fdfe619f563418c892bba2782a488e4e80dcc2ff9ccc9bd2784bde5。
- Host tests PASS：10项本地测试；同一源码原生LVGL字形/文案/500次界面更新，新增断言确认各环顺序及容差间隔大于有效数字间隔；原始完整gate回归全部通过。日志分别为resistor-tests-v2.log、resistor-preview-v2.log、resistor-gate-v2.log。
- 链接静态DRAM占用146,792字节/321,296字节（45.69%），其中画布36,480字节；剩余174,504字节是链接统计，不是实测运行空闲堆。日志 artifacts/logs/resistor-size-v2.log。
- 独立渲染审查未发现边界、数学、颜色缓存或LVGL缓冲区问题。只在颜色/环数变化时重绘。
- Device tests NOT RUN：未打开、复位或写入设备。实物材质显示、按钮体验与运行时内存/延迟仍待授权后的真机验收。

## 简洁UI与授权烧录（2026-10-28）

用户明确要求调整后刷入设备，授权覆盖当前简洁UI版本。移除方向文字、箭头、环号排布和颜色名称/色块卡片；阻值与单位组合居中；末环单独拉开，改为更饱满的哑光本体及真实边缘混合。6环末环仍按温度系数计算，间距为示意图设计而非额外色码规则。本次UI修订仍未提交，基础提交d3053bb，来源记录dirty=true。

- Build PASS：ESP-IDF5.5.3、ESP32-C3、8MB；镜像归档 `artifacts/resistor-reader/f60ffae07e54f362c41e310c613a2e04726238669abd4dce272eb43d727e5a4b`。
- 完整镜像SHA256：`f60ffae07e54f362c41e310c613a2e04726238669abd4dce272eb43d727e5a4b`。
- 匹配ELF SHA256：`1949f5bf5d18f17eedd83fa48cd8d66dc4671c8690d6dd1e669cd26131dff678`。
- Host tests PASS：10项本地测试；字形、容差文案、末环间距及500次UI更新；原始上游完整gate回归、独立UI审查通过。日志 artifacts/logs/resistor-{build,tests,preview,gate}-v3.log。
- Device write/verify PASS：`/dev/cu.usbmodem11101`，USB303A:1001，ESP32-C3 rev1.1，实际探测8MB Flash。通过SDK容器显式映射此端口到/dev/passport；使用`--before usb_reset`，首次标准复位连接失败后已纠正。编译容器的默认权限未改变。
- 仅写指定完整镜像，从0x0起738864字节；实际按扇区擦除范围0x00000000–0x000b4fff；未执行整片擦除。保留镜像中的Flash参数，写后哈希和回读验证成功。日志 artifacts/logs/resistor-flash-v3.log。
- Device startup PASS：写入后复位启动，显示240×320、LVGL、三键初始化完成，出现`Ready: 4/5/6 bands`；随后20秒无崩溃/重启特征，串口已关闭。首次CW2017配置日志中的SOC/电压为0，未验证稳定后的电池数值。
- Unverified：实际屏幕美观/颜色、用户物理按键操作、稳定电池读数及长期运行；烧录和启动日志不替代这些验收。

烧录使用SDK镜像内的esptool4.12.dev1，从只读仓库挂载读取精确归档；主机未安装新依赖。串口授权只用于本次固件，未将设备权限加入默认构建服务。

## 双向选环与性能优化（已刷入正式版）

用户确认短按确定前进、长按确定后退，并要求极致优化切换环数/颜色时的延迟。延续本次已连接设备的固件调整和烧录验证流程，先写入显式基准版本比较同一板上的两种算法，再写回不带基准代码的正式版。

### 根因与修改

原应用等待释放后的180ms单/双击窗口，再逐像素执行曲面浮点光照；原轮询20ms、默认刷新周期又增加等待。现复用BSP原始PRESS/RELEASE，不改变ADC去抖和硬件参数；上下按下即改，确定短按释放即前进、按住400ms后退一次，释放不重复动作，延迟CLICK/DOUBLE/LONG不再处理。纯C输入逻辑处理原始时间戳、快速连按、长按后释放与时钟回绕。轮询5ms、刷新10ms，性能编译。

美术算法离线烘焙240,864字节纹理到Flash，保留同一36,480字节画布；运行时整图/单环采用拷贝，单环脏区11×53。UI字段按变化分别更新，焦点变化不重算数值、不重画电阻。逐像素对比原算法通过，外观保持一致。

### 真机对比

同一ESP32-C3 rev1.1 /8MB /同一编译配置，基准版完整镜像：`d6c944f04ac23bdaf7d86d8786cce6581319feb1e8498daaf5b8cf2815e99b23`。各行是4/5/6环：

| 绘图操作 | 4环µs | 5环µs | 6环µs |
| --- | ---: | ---: | ---: |
| 原算法整图（单次） | 475154 | 475101 | 475075 |
| 优化整图（100次平均） | 3049 | 3138 | 3229 |
| 优化单环（100次平均） | 62 | 63 | 63 |

真实LVGL界面检查（µs）：改色UI4186 /连同刷新提交21232；环数UI9474 /连同提交47655；焦点UI1167 /连同提交16167。原生预览一次焦点flush8154像素、改色8842像素，全屏76800像素。这些数字**不是物理按键到屏幕光子的完整延迟**，原输入窗口证据来自BSP/组件状态机，最终体感待用户确认。日志 artifacts/logs/resistor-benchmark-device.log，机器可读结果 artifacts/resistor-reader/performance.json。

### 最终交付与验收

- Build PASS：普通构建来源mode=incremental，不是benchmark；完整镜像`ad11bb24569be961b416ac0765923ffc2bad4a06580c813310378aff7fd4da10`，配套ELF `ad62d80c0d1256f1a11606c53d666a1cc1481a978ac2ac6bbe21078cbf684476`，归档`artifacts/resistor-reader/ad11bb24569be961b416ac0765923ffc2bad4a06580c813310378aff7fd4da10`。当前修改未提交，来源dirty=true、基础提交d3053bb。
- Host tests PASS：12项本地测试（含C模型/输入状态机）；全部角色/颜色的原算法逐像素对比、单环11×53区域、焦点/改色flush面积、字形和500次UI更新；原始完整gate回归通过。
- 独立审查发现原生.incbin资源缺少增量构建依赖，已补充OBJECT_DEPENDS；触碰纹理文件后，实际观察textures.S.o重新构建并重新通过预览断言。ESP-IDF使用target_add_binary_data已有二进制依赖。
- 链接静态RAM144220字节，剩余177076字节；Flash纹理240864字节未拷入RAM。nm确认正式ELF不含rr_art_render/rr_benchmark_run；正常启动不跑基准。
- Device write/verify PASS：同一/dev/cu.usbmodem11101，SDK容器显式端口映射、usb_reset，精确归档完整镜像从0x0写入；未做整片擦除。正式版日志 artifacts/logs/resistor-fast-device.log。
- Device startup PASS：正式版583ms进入Ready，电池读到91%/4089mV，20秒无崩溃/重启特征，串口已关闭。未经人工按键的测量不声称实物按键已验收。
- Unverified：用户真实按压手感、屏幕物理刷新延迟和长期运行。
