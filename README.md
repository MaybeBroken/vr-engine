# VR Engine - Android OpenXR Project

A C++ Android VR application using OpenXR API for virtual reality development.

## Project Structure

```
vr-engine/
├── app/
│   ├── src/
│   │   └── main/
│   │       ├── cpp/
│   │       │   └── main.cpp              # Main C++ source with OpenXR and OpenGL ES
│   │       ├── res/
│   │       │   └── values/
│   │       │       └── strings.xml       # App resources
│   │       └── AndroidManifest.xml       # Android manifest configuration
│   ├── openxr_loader/                    # OpenXR loader library (see README inside)
│   ├── CMakeLists.txt                    # CMake build configuration
│   └── build.gradle                      # App-level Gradle build configuration
├── gradle/
│   └── wrapper/
│       └── gradle-wrapper.properties     # Gradle wrapper configuration
├── build.gradle                          # Root-level Gradle configuration
├── settings.gradle                       # Gradle settings
├── gradle.properties                     # Gradle properties
├── gradlew                               # Gradle wrapper script (Unix)
└── gradlew.bat                           # Gradle wrapper script (Windows)
```

## Features

- **OpenXR Integration**: Initialize OpenXR instance and system for VR
- **OpenGL ES 3.0**: Graphics rendering context setup
- **Android NDK**: Native C++ development
- **CMake Build System**: Modern C++ build configuration

## Prerequisites

Before building this project, you need:

1. **Android Studio** (Arctic Fox or newer)
2. **Android NDK** (r21 or newer)
3. **CMake** (3.18.1 or newer)
4. **OpenXR Loader**: See `app/openxr_loader/README.md` for instructions

## Setup Instructions

### 1. Install Android Studio and SDK
- Download and install Android Studio
- Install Android SDK (API level 26 or higher)
- Install Android NDK through SDK Manager

### 2. Add OpenXR Loader
Before building, you must add the OpenXR loader library:
```bash
# See app/openxr_loader/README.md for detailed instructions
# Place OpenXR headers in: app/openxr_loader/include/openxr/
# Place libraries in: app/openxr_loader/libs/{arm64-v8a,armeabi-v7a}/
```

### 3. Build the Project

#### Using Android Studio:
1. Open the project in Android Studio
2. Wait for Gradle sync to complete
3. Build > Make Project
4. Run on connected VR device or emulator

#### Using Command Line:
```bash
# Build debug APK
./gradlew assembleDebug

# Build release APK
./gradlew assembleRelease

# Install to connected device
./gradlew installDebug
```

## Configuration

### Target Devices
- **Minimum SDK**: Android 8.0 (API 26)
- **Target SDK**: Android 13 (API 33)
- **Supported ABIs**: arm64-v8a, armeabi-v7a

### VR Requirements
- Device with OpenXR runtime support
- Head tracking capability
- OpenGL ES 3.0 support

## Code Overview

### main.cpp
The main entry point contains:
- **OpenXR Initialization**: Creates OpenXR instance with Android extensions
- **OpenGL ES Setup**: Initializes EGL display, context, and surface
- **Event Loop**: Handles Android app lifecycle events
- **VREngine Class**: Manages VR context and rendering

Key components:
```cpp
- android_main()           // Entry point
- VREngine::initializeOpenXR()    // OpenXR setup
- VREngine::initializeOpenGLES()  // Graphics setup
- handleAppCmd()           // Android event handler
```

## Building APK

The project is configured to output APK files:
- Debug APK: `app/build/outputs/apk/debug/app-debug.apk`
- Release APK: `app/build/outputs/apk/release/app-release.apk`

## Next Steps

This is a basic OpenXR project template. To extend it:

1. **Add Swapchain Creation**: Create OpenXR swapchains for rendering
2. **Implement Frame Loop**: Add XR frame submission and rendering
3. **Add Controllers**: Implement action system for VR controllers
4. **Create 3D Scene**: Add actual 3D content rendering
5. **Add Shaders**: Implement vertex and fragment shaders

## Troubleshooting

### Build Failures
- Ensure OpenXR loader is properly placed in `app/openxr_loader/`
- Check that NDK is installed and CMake version is correct
- Verify Gradle and Android SDK versions

### Runtime Issues
- Ensure target device supports OpenXR
- Check logcat for detailed error messages: `adb logcat -s VREngine`
- Verify VR permissions in AndroidManifest.xml

## License

This is a template project for educational purposes.

## Resources

- [OpenXR Specification](https://www.khronos.org/openxr/)
- [Android NDK Documentation](https://developer.android.com/ndk)
- [OpenGL ES Documentation](https://www.khronos.org/opengles/)