cmake_minimum_required(VERSION 3.10)
project(libmodbus C)

# 收集 src 目录下的所有 .c 源文件
file(GLOB MODBUS_SOURCES "src/*.c")

# 生成动态库
add_library(modbus SHARED ${MODBUS_SOURCES})
target_compile_definitions(modbus PRIVATE LIBMODBUS_EXPORTS)
target_link_libraries(modbus PRIVATE ws2_32)
target_include_directories(modbus PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)

# 生成静态库
add_library(modbus_static STATIC ${MODBUS_SOURCES})
target_link_libraries(modbus_static PRIVATE ws2_32)
target_include_directories(modbus_static PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
