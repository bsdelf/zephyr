# 1. 声明所需的最低 CMake 版本（新版 Zephyr 规范推荐使用 3.20.0 或更高版本）
cmake_minimum_required(VERSION 3.20.0)

# 2. 定位并引入 Zephyr 包（这步会自动加载 Zephyr 的所有底层编译工具链和宏定义）
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})

# 3. 声明你的项目名称（可以任意命名）
project(zephyr_st7305_keyboard)

# 4. 将你的业务源码文件（如 main.c）添加进 Zephyr 的核心编译目标中
target_sources(app PRIVATE main.c )

