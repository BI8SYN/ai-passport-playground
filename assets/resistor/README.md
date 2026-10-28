# 电阻绘图资源

本项目原创程序美术，算法源为 tools/renderer/resistor_art.c，几何布局在 firmware/resistor-reader/main/resistor_layout.c。没有使用外部照片或AI示意图作为设备画面。

运行时不计算曲面浮点光照。resistor_textures.bin 是小端RGB565：2张240×76的无色环本体，随后按位置、颜色排列12×12张11×53色环贴片；共240,864字节。尺寸、位置、格式和SHA256见同名JSON。资源直接映射Flash，只保留原有36,480字节RAM画布；单色环变化只拷贝并失效该色环区域。

再生成（SDK容器，不安装主机依赖）：

```sh
docker compose run --rm --no-deps -T sdk python3 tools/generate_resistor_textures.py
./tools/dev preview resistor-reader
./tools/dev build resistor-reader
```

程序艺术、布局或色值改变时必须再生成并提交二进制和元数据。原生预览会把全部角色/颜色变化与原算法逐像素对比，检查局部更新和flush面积；ESP-IDF和原生预览都将相同二进制嵌入只读段。普通设备构建不包含旧浮点算法。
