# OpenXR Loader

This directory should contain the OpenXR loader library for Android.

## How to obtain the OpenXR loader:

### Option 1: Download from Khronos
Download the official OpenXR SDK from: https://github.com/KhronosGroup/OpenXR-SDK

### Option 2: Build from source
1. Clone the OpenXR-SDK repository
2. Build for Android using the provided build scripts
3. Copy the built libraries to this directory

### Required files:

**Headers** (place in `include/openxr/`):
- `openxr.h`
- `openxr_platform.h`
- `openxr_platform_defines.h`
- `openxr_reflection.h`

**Libraries** (place in respective arch directories):
- `libs/arm64-v8a/libopenxr_loader.so`
- `libs/armeabi-v7a/libopenxr_loader.so`

### For Meta Quest / Oculus VR:
You can also use the Oculus OpenXR Mobile SDK:
https://developer.oculus.com/downloads/package/oculus-openxr-mobile-sdk/

This includes pre-built OpenXR loader libraries optimized for Quest devices.

## Current Status:
The project expects OpenXR loader files in this directory structure. Without these files, the build will fail. Add them before attempting to build the APK.
