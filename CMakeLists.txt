cmake_minimum_required(VERSION 3.20)
project(cpp_programm)

set(CMAKE_CXX_STANDARD 17)

# Твой исходный файл
add_executable(cpp_programm main.cpp
        RemoteZoneProfiler.h
)

# WinSock2 (для TCP)
target_link_libraries(cpp_programm PRIVATE ws2_32)

# Подключаем Tracy
add_subdirectory(extern/tracy)

# Связываем с Tracy клиентом
target_link_libraries(cpp_programm PRIVATE Tracy::TracyClient)