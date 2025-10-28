cmake_minimum_required(VERSION 3.18.1)

project(vrengine)

# Set C++ standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Add the native app glue
add_library(native_app_glue STATIC
    ${ANDROID_NDK}/sources/android/native_app_glue/android_native_app_glue.c)

target_include_directories(native_app_glue PUBLIC
    ${ANDROID_NDK}/sources/android/native_app_glue)

# Find required libraries
find_library(log-lib log)
find_library(android-lib android)
find_library(EGL-lib EGL)
find_library(GLESv3-lib GLESv3)

# Add OpenXR loader
add_library(openxr_loader SHARED IMPORTED)
set_target_properties(openxr_loader PROPERTIES
    IMPORTED_LOCATION ${CMAKE_CURRENT_SOURCE_DIR}/openxr_loader/libs/${ANDROID_ABI}/libopenxr_loader.so)

# Main native library
add_library(vrengine SHARED
    src/main/cpp/main.cpp)

target_include_directories(vrengine PRIVATE
    ${ANDROID_NDK}/sources/android/native_app_glue
    ${CMAKE_CURRENT_SOURCE_DIR}/openxr_loader/include)

target_link_libraries(vrengine
    native_app_glue
    openxr_loader
    ${log-lib}
    ${android-lib}
    ${EGL-lib}
    ${GLESv3-lib})
