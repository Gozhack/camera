# Ultra-Low Latency NDK Camera: Zero-Overhead Imaging

## Mission Profile
This project is an Android camera implementation engineered for absolute performance. By bypassing the JVM overhead and interacting directly with the `ACamera` (NDK Camera2) and `ANativeWindow` APIs, we eliminate garbage collection pauses and JNI transition bottlenecks. In this domain, **every millisecond is a liability.**

## Technical Architecture (Phase 1: Core Engine)
The current implementation focuses on the critical path of the imaging pipeline:
- **Direct NDK Camera2 Integration:** Utilizing `camera2/NdkCameraManager.h` for hardware-level control.
- **Synchronous Resource Management:** Deterministic memory handling via C++17/20 to ensure predictable frame-pacing.
- **AImageReader Bridge:** Zero-copy off-screen buffer acquisition utilizing `AHardwareBuffer` to enable concurrent rendering.
- **Vulkan Graphics Pipeline:** Real-time preview rendering with native YUV sampling and UI overlay capabilities.

## Performance Mandates
To maintain the highest possible throughput and lowest latency, the project adheres to the following constraints:
1. **No JVM on the Hot Path:** Frames are processed and rendered entirely within native threads.
2. **Memory Locality:** Cache-friendly data structures to minimize bus contention during high-speed sensor readout.
3. **SIMD Readiness:** Data structures aligned for NEON/ARMv8 instruction sets to enable parallel pixel processing.
4. **Lock-Free Synchronization:** Priority on non-blocking primitives to avoid thread starvation in the rendering loop.

## Stack & Toolchain
- **LLVM/Clang:** Optimized toolchain with `-O3` and `-ffast-math` targets.
- **Android NDK (r30):** Leveraging the latest stable native APIs.
- **C++20:** For modern, zero-cost abstractions.
- **Vulkan 1.1:** Core graphics API for zero-copy, hardware-accelerated preview rendering.

## Roadmap: The Pursuit of Microseconds
- [x] **AImageReader Bridge:** Zero-copy off-screen buffer acquisition for camera frames.
- [ ] **Vulkan Rendering Pipeline:** Real-time display of camera feed utilizing hardware-accelerated YUV-to-RGB conversion.
- [ ] **Pure Native UI:** Implement capture controls and interface overlay utilizing Vulkan directly on the native surface.
- [ ] **NEON Optimized Conversions:** Moving YUV to RGB transformations to SIMD.
- [ ] **Custom Memory Pool:** Pre-allocated buffer queues to eliminate `malloc` calls during capture.

## Getting Started: Deploying the Native Engine

### Build Requirements
- Android NDK r30+ (Configured in `build.gradle`)
- Android Gradle Plugin 8.5.0
- Gradle 8.7
- 16 KB Page Size Alignment Support Included

### 1. Build the APK
Execute the following from the project root:
```bash
./gradlew assembleDebug
```

### 2. Install
```bash
adb install app/build/outputs/apk/debug/app-debug.apk
```
*Note: The application includes a JNI-based runtime permission request, so you no longer need to grant camera access manually via ADB.*

### 3. Run and Monitor
Launch the application and monitor the high-performance logs:
```bash
# Launch
adb shell am start -n com.example.camera/android.app.NativeActivity

# Monitor Logs
adb logcat -s NativeCamera -s VulkanEngine
```
