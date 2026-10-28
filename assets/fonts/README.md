# 字体来源

色环读取器使用 Noto Sans CJK SC Regular 的 16px、4bpp 子集。原字体版权：© 2014-2021 Adobe (http://www.adobe.com/)。上游版本、固定提交、下载地址和完整字体 SHA256 见 noto-source.json，许可见 [Noto-OFL.txt](Noto-OFL.txt)（SIL Open Font License 1.1）。生成文件位于 firmware/resistor-reader/main/resistor_font_16.c；保留上游版权和许可。本子集没有宣称使用 Noto 保留名称作为新字体家族。

子集包含 ASCII、中文 UI 文案、± 和 Ω。界面大号数字使用 LVGL 自带 Montserrat。字体原文件只缓存到 Git 忽略的 artifacts/fonts/，不将16 MB字体打入固件。

在仓库根目录重新生成：

```sh
docker compose run --rm --no-deps -T fonts
./tools/dev build resistor-reader
./tools/dev preview resistor-reader
```

Node 镜像按 digest 固定，lv_font_conv 1.5.3 按 package-lock.json 安装，node_modules 和 npm 缓存位于 Docker volumes。生成脚本核对原字体 SHA256，并从 UI 中的 rr_ui_glyphs 提取字符；新增文案时同时更新字符清单。预览会检查每个字符的真实字形及 Ω，拒绝占位方框。
