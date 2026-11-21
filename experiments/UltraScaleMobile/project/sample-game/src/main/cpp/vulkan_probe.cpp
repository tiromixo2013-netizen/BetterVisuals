#include <jni.h>
#include <vulkan/vulkan.h>
#include <android/log.h>
#include <vector>
#include <string>

extern "C" JNIEXPORT jstring JNICALL
Java_dev_ultrascale_samplegame_MainActivity_runVulkanProbe(JNIEnv* env, jobject) {
    VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};
    app.pApplicationName = "UltraScale Layer Test";
    app.apiVersion = VK_API_VERSION_1_0;
    VkInstanceCreateInfo create{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    create.pApplicationInfo = &app;
    VkInstance instance = VK_NULL_HANDLE;
    VkResult result = vkCreateInstance(&create, nullptr, &instance);
    if (result != VK_SUCCESS) {
        std::string error = "vkCreateInstance failed: " + std::to_string(result);
        return env->NewStringUTF(error.c_str());
    }

    uint32_t count = 0;
    result = vkEnumeratePhysicalDevices(instance, &count, nullptr);
    if (result != VK_SUCCESS || count == 0) {
        vkDestroyInstance(instance, nullptr);
        return env->NewStringUTF("No Vulkan physical device found");
    }
    std::vector<VkPhysicalDevice> devices(count);
    result = vkEnumeratePhysicalDevices(instance, &count, devices.data());
    if (result != VK_SUCCESS) {
        vkDestroyInstance(instance, nullptr);
        return env->NewStringUTF("Physical device enumeration failed");
    }

    VkPhysicalDeviceProperties properties{};
    vkGetPhysicalDeviceProperties(devices[0], &properties);
    uint32_t familyCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(devices[0], &familyCount, nullptr);
    std::vector<VkQueueFamilyProperties> families(familyCount);
    vkGetPhysicalDeviceQueueFamilyProperties(devices[0], &familyCount, families.data());
    uint32_t graphicsFamily = UINT32_MAX;
    for (uint32_t i = 0; i < familyCount; ++i) {
        if (families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) { graphicsFamily = i; break; }
    }
    if (graphicsFamily == UINT32_MAX) {
        vkDestroyInstance(instance, nullptr);
        return env->NewStringUTF("No graphics queue family found");
    }

    float priority = 1.0f;
    VkDeviceQueueCreateInfo queue{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
    queue.queueFamilyIndex = graphicsFamily;
    queue.queueCount = 1;
    queue.pQueuePriorities = &priority;
    VkDeviceCreateInfo deviceInfo{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
    deviceInfo.queueCreateInfoCount = 1;
    deviceInfo.pQueueCreateInfos = &queue;
    VkDevice device = VK_NULL_HANDLE;
    result = vkCreateDevice(devices[0], &deviceInfo, nullptr, &device);
    std::string output = "GPU: " + std::string(properties.deviceName) +
        " | Vulkan " + std::to_string(VK_VERSION_MAJOR(properties.apiVersion)) +
        "." + std::to_string(VK_VERSION_MINOR(properties.apiVersion));
    if (result == VK_SUCCESS) {
        output += " | vkCreateDevice OK";
        vkDestroyDevice(device, nullptr);
    } else {
        output += " | vkCreateDevice failed: " + std::to_string(result);
    }
    __android_log_print(ANDROID_LOG_INFO, "UltraScaleTestVK", "%s", output.c_str());
    vkDestroyInstance(instance, nullptr);
    return env->NewStringUTF(output.c_str());
}
