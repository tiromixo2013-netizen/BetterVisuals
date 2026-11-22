#include <jni.h>
#include <cstdint>
#include "ultrascale_gles_backend.h"

extern "C" JNIEXPORT jint JNICALL
Java_dev_ultrascale_samplegame_NativeGlesBridge_process(
    JNIEnv*, jobject,
    jint sourceTexture, jint destinationTexture,
    jint inputWidth, jint inputHeight,
    jint outputWidth, jint outputHeight,
    jfloat sharpness) {
    if (sourceTexture <= 0 || destinationTexture <= 0 ||
        inputWidth <= 0 || inputHeight <= 0 ||
        outputWidth <= 0 || outputHeight <= 0) {
        return US_GLES_INVALID_ARGUMENT;
    }
    return static_cast<jint>(ultrascale_gles_process_texture(
        static_cast<uint32_t>(sourceTexture),
        static_cast<uint32_t>(destinationTexture),
        static_cast<uint32_t>(inputWidth),
        static_cast<uint32_t>(inputHeight),
        static_cast<uint32_t>(outputWidth),
        static_cast<uint32_t>(outputHeight),
        US_GLES_MODE_EDGE_AWARE,
        sharpness));
}
