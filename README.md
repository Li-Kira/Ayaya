# Ayaya

Ayaya: a game engine developed on Mac

![](assets/other/Snipaste_2026-03-10_11-24-08.png)

``` bash
#创建编译目录
mkdir build && cd build
#编译完整项目
cmake .. && make -j$(sysctl -n hw.ncpu)
rm -rf * && cmake .. && make -j$(sysctl -n hw.ncpu)
#单独编译编辑器
cmake .. && make AyayaEditor -j$(sysctl -n hw.ncpu)
#单独编译测试项目
cmake .. && make Sandbox -j$(sysctl -n hw.ncpu)
```