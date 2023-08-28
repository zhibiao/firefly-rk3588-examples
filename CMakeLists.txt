cmake_minimum_required(VERSION 3.0.0)
project(hdmiin_kms VERSION 0.1.0 LANGUAGES C)


add_executable(hdmiin_kms hdmiin_kms.c)
target_link_libraries(hdmiin_kms PUBLIC drm)