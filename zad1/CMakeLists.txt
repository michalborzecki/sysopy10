cmake_minimum_required(VERSION 3.4)
project(zad1 C)

set(CMAKE_C_FLAGS "-Wall -pthread")

add_executable(server server.c)
add_executable(client client.c)