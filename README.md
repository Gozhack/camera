# Ultra-Low Latency NDK Camera: Zero-Overhead Imaging

## Mission Profile
This project is an Android camera implementation engineered for absolute performance. By bypassing the JVM overhead and interacting directly with the `ACamera` (NDK Camera2) and `Vulkan` APIs, we eliminate garbage collection pauses and JNI transition bottlenecks. In this domain, **every millisecond is a liability.**

## Technical Architecture (Phases)

### Phase 1: Core Foundation (Complete)
- **Direct NDK Camera2 Integration:** Hardware-level control for minimal latency.
- **Pure NativeActivity:** Elimination of the JVM lifecycle from the imaging hot path.

### Phase 2: The AImageReader Bridge (Complete)
- **Off-screen Acquisition:** Redirecting camera output to an `AImageReader` to free up the `ANativeWindow`.
- **Zero-Copy Memory:** Maintaining `AHardwareBuffer` references for direct GPU access.

### Phase 3: High-Speed Vulkan Rendering (Complete)
- **Hardware YUV Sampling:** Utilizing `VK_KHR_sampler_ycbcr_conversion` for automatic, zero-overhead color space transformation.
- **Resource Caching:** Smart buffer management that eliminates per-frame Vulkan object creation, achieving true 60+ FPS preview.
- **Stability Core:** Robust thread synchronization (atomic-guarded) and explicit hardware buffer management for crash-free operation.
- **16 KB Page Alignment:** Full compatibility with Android 15+ devices and modern hardware architectures.

### Phase 4: Pure Native UI (Complete)
- **Zero-JVM Input Handling:** Processing `AInputEvent` directly via `android_native_app_glue`.
- **Orthographic UI Overlay:** A secondary Vulkan pipeline with alpha blending for rendering interactive controls over the live preview.
- **Low-Latency Feedback:** Optimized "Zero-Cost Flash" effect using clear-color toggling for immediate shutter feedback.

### Phase 5: High-Resolution Capture (Complete)
- **Dual-Output Pipeline:** Simultaneous management of 1080p YUV preview and max-resolution (e.g., 12.5MP) JPEG capture streams.
- **Asynchronous I/O:** JPEG encoding via hardware ISP and background disk writing on detached threads to prevent preview stutter.
- **Direct Filesystem Access:** Saving images to app-private external storage without JNI overhead.

## Performance Mandates
1. **No JVM on the Hot Path:** Processing and rendering occur entirely within native threads.
2. **Deterministic Pacing:** Lock-free synchronization between Camera and Vulkan stages.
3. **ALU Efficiency:** Color conversion is handled by fixed-function GPU hardware, not shader math.

## Stack & Toolchain
- **NDK (r30):** Leveraging modern Native APIs.
- **C++20:** Zero-cost abstractions.
- **Vulkan 1.1:** Hardware-accelerated graphics.
- **Android Gradle Plugin 8.5.0 / Gradle 8.7**

## Roadmap
- [x] **AImageReader Bridge:** Zero-copy off-screen buffer acquisition.
- [x] **Vulkan Rendering Pipeline:** Real-time hardware-accelerated preview.
- [x] **Pure Native UI:** Integrated capture controls and feedback.
- [x] **High-Res Capture:** Single-shot high-resolution JPEG acquisition.

## Next Steps
- **Manual Controls:** Implement Native UI for ISO, Exposure Time, and Manual Focus.
- **Media Scanner Integration:** Trigger a broadcast to make saved images visible in the Android Gallery.
- **RAW/DNG Support:** Extend capture pipeline to support uncompressed Bayer RAW data.
- **Advanced UI:** Implement a native Vulkan-based gallery viewer.

## Getting Started

### Build
```bash
./gradlew assembleDebug
```

### Install & Run
```bash
adb install app/build/outputs/apk/debug/app-debug.apk
adb shell am start -n com.example.camera/android.app.NativeActivity
```
*Note: The application includes a JNI-based runtime permission request.*

### Monitor
```bash
adb logcat -s NativeCamera -s VulkanEngine
```
