cmake_minimum_required(VERSION 2.5) #要求当前系统环境必须装的cmake环境>=2.5，才可以编译当前的项目
project(mymuduo)    #启动mymuduo

# cmake => makefile   make
# mymuduo最终编译成so动态库，设置动态库路径，放在根目录的lib文件夹下面
set(LIBRARY_OUTPUT_PATH ${PROJECT_SOURCE_DIR}/lib)
# 设置调试信息 以及 启动C++11语言标准
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -g -std=c++11 -fPIC")    # 有些人本地的编译环境gcc,g++版本如果不是非常新的话，默认C++的语法没有打开，应该再加个-std=c++11

# 定义参与编译的源代码文件,把当前根目录下的名字源文件组合起来放在变量SRC_LIST里面
aux_source_directory(. SRC_LIST)
# 编译生成动态库mymuduo
add_library(mymuduo SHARED ${SRC_LIST})