# RF Link Web Passport 诊断模拟器

用户已选择Mac模拟BLE外设、Web还原Passport并试听音频的方案，授权制作试用。本地诊断原型，不改产品固件。详细接口与边界见tools/handmic-simulator/CONTRACT.md。

- [x] C后端：复用正式协议/产品模型/LVGL/频谱；stdin/out本地JSON桥；模拟按键、持久设置、收发音频、流控、全部页面和历史。
- [x] Mac BLE：原生CoreBluetooth发布正式GATT，回传原始写入、动态读、通知/指示、广播管理、连接代次/队列统计，独立.app与权限说明。
- [x] Web：真实像素屏幕、三键、麦克风、原始/缓冲后音频试听、收包/格式/序号/间隔/缓冲指标与诊断导出。
- [x] 构建/启动：Docker后端入口，Mac本地Swift桥，Python标准库Web与WS服务，只监听loopback；不安装主机ESP-IDF或修改物理Passport绑定。
- [x] 测试：JSON/WS/PCM检查、真实core命令和UI、后端音频收发与流控、Swift编译、浏览器语法/页面预览、实际广播；iOS连接需用户选择模拟器，不能声称连接前已联调通过。

实现分工按独立域并行，接口先固定；不提交/推送/刷写。真实SMP口令、物理Codec和RF不会由Web虚构还原，界面明确边界。

实际iOS连接、HELLO、3订阅和PCM收包已验证；两路真实听感尚未收到用户比较结果，硬件等效边界保持。首次导出分析与独立密集断续复现见开发记录。
