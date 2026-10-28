# 本地共享组件

仅添加确有复用需求的 ESP-IDF 组件，每个组件放在自己的子目录并提供 CMakeLists.txt。cmake/passport.cmake 会加入这些有效组件目录。

板级驱动唯一源码位于 vendor/ai-passport/components/bsp；不要在这里再复制 BSP。
