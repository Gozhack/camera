cmake_minimum_required(VERSION 3.22.1)

project("camera_native")

find_library(
        log-lib
        log)

find_library(
        android-lib
        android)

# Biblioteca NDK de cámara y media
find_library(
        camera2ndk-lib
        camera2ndk)
        
find_library(
        mediandk-lib
        mediandk)

find_library(
        vulkan-lib
        vulkan)

add_library(
        camera_native
        SHARED
        native-lib.cpp
        vulkan_context.cpp
        ${ANDROID_NDK}/sources/android/native_app_glue/android_native_app_glue.c)

target_include_directories(camera_native PRIVATE
    ${ANDROID_NDK}/sources/android/native_app_glue)

target_link_libraries(
        camera_native
        ${log-lib}
        ${android-lib}
        ${camera2ndk-lib}
        ${mediandk-lib}
        ${vulkan-lib})
