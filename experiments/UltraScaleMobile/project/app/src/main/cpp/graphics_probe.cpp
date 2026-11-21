#include <jni.h>
#include <string>
#include <vulkan/vulkan.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES3/gl3.h>

static std::string vulkanInfo() {
    uint32_t count = 0;
    VkResult r = vkEnumerateInstanceExtensionProperties(nullptr, &count, nullptr);
    if (r != VK_SUCCESS) return "Vulkan loader unavailable";
    uint32_t version = VK_API_VERSION_1_0;
    auto enumerateVersion = reinterpret_cast<PFN_vkEnumerateInstanceVersion>(
        vkGetInstanceProcAddr(VK_NULL_HANDLE, "vkEnumerateInstanceVersion"));
    if (enumerateVersion) enumerateVersion(&version);
    return "Vulkan loader present; instance API " +
        std::to_string(VK_VERSION_MAJOR(version)) + "." +
        std::to_string(VK_VERSION_MINOR(version)) +
        "; extensions=" + std::to_string(count);
}

static std::string glesInfo() {
    EGLDisplay display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    EGLint major = 0, minor = 0;
    if (display == EGL_NO_DISPLAY || !eglInitialize(display, &major, &minor))
        return "EGL initialization unavailable";
    const char* vendor = eglQueryString(display, EGL_VENDOR);
    const char* version = eglQueryString(display, EGL_VERSION);
    std::string result = "EGL " + std::to_string(major) + "." + std::to_string(minor);
    if (vendor) result += "; vendor=" + std::string(vendor);
    if (version) result += "; version=" + std::string(version);
    eglTerminate(display);
    return result;
}

extern "C" JNIEXPORT jstring JNICALL
Java_dev_ultrascale_mobile_NativeGraphicsProbe_vulkanLoaderInfo(JNIEnv* env, jobject) {
    const auto value = vulkanInfo();
    return env->NewStringUTF(value.c_str());
}

extern "C" JNIEXPORT jstring JNICALL
Java_dev_ultrascale_mobile_NativeGraphicsProbe_eglLoaderInfo(JNIEnv* env, jobject) {
    const auto value = glesInfo();
    return env->NewStringUTF(value.c_str());
}
