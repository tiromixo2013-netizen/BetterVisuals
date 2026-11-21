#include <vulkan/vulkan.h>
#include <vulkan/vk_layer.h>
#include <android/log.h>
#include <cstring>
#include <atomic>
#include <mutex>
#include <unordered_map>

namespace {
constexpr char kLayerName[] = "VK_LAYER_ULTRASCALE_api_lab";
std::mutex gMutex;
std::unordered_map<VkInstance, PFN_vkGetInstanceProcAddr> gInstances;
std::unordered_map<VkDevice, PFN_vkGetDeviceProcAddr> gDevices;
std::unordered_map<VkPhysicalDevice, VkInstance> gPhysicalDevices;
std::unordered_map<VkQueue, PFN_vkQueuePresentKHR> gQueues;
std::unordered_map<VkQueue, VkDevice> gQueueDevices;
struct SwapchainInfo {
    VkFormat format = VK_FORMAT_UNDEFINED;
    VkExtent2D extent{0, 0};
    uint32_t imageCount = 0;
};
std::unordered_map<VkSwapchainKHR, SwapchainInfo> gSwapchains;
std::atomic<uint64_t> gPresentedFrames{0};

VkLayerInstanceCreateInfo* FindInstanceLink(const VkInstanceCreateInfo* info) {
    auto* item = reinterpret_cast<const VkBaseInStructure*>(info->pNext);
    while (item) {
        if (item->sType == VK_STRUCTURE_TYPE_LOADER_INSTANCE_CREATE_INFO) {
            auto* link = reinterpret_cast<VkLayerInstanceCreateInfo*>(const_cast<VkBaseInStructure*>(item));
            if (link->function == VK_LAYER_LINK_INFO) return link;
        }
        item = item->pNext;
    }
    return nullptr;
}
VkLayerDeviceCreateInfo* FindDeviceLink(const VkDeviceCreateInfo* info) {
    auto* item = reinterpret_cast<const VkBaseInStructure*>(info->pNext);
    while (item) {
        if (item->sType == VK_STRUCTURE_TYPE_LOADER_DEVICE_CREATE_INFO) {
            auto* link = reinterpret_cast<VkLayerDeviceCreateInfo*>(const_cast<VkBaseInStructure*>(item));
            if (link->function == VK_LAYER_LINK_INFO) return link;
        }
        item = item->pNext;
    }
    return nullptr;
}

}

VKAPI_ATTR VkResult VKAPI_CALL LayerCreateInstance(const VkInstanceCreateInfo*, const VkAllocationCallbacks*, VkInstance*);
VKAPI_ATTR VkResult VKAPI_CALL LayerCreateDevice(VkPhysicalDevice, const VkDeviceCreateInfo*, const VkAllocationCallbacks*, VkDevice*);
VKAPI_ATTR VkResult VKAPI_CALL LayerEnumeratePhysicalDevices(VkInstance, uint32_t*, VkPhysicalDevice*);
VKAPI_ATTR VkResult VKAPI_CALL LayerEnumeratePhysicalDeviceGroups(VkInstance, uint32_t*, VkPhysicalDeviceGroupProperties*);
VKAPI_ATTR void VKAPI_CALL LayerDestroyInstance(VkInstance, const VkAllocationCallbacks*);
VKAPI_ATTR void VKAPI_CALL LayerDestroyDevice(VkDevice, const VkAllocationCallbacks*);
VKAPI_ATTR void VKAPI_CALL LayerGetDeviceQueue(VkDevice, uint32_t, uint32_t, VkQueue*);
VKAPI_ATTR void VKAPI_CALL LayerGetDeviceQueue2(VkDevice, const VkDeviceQueueInfo2*, VkQueue*);
VKAPI_ATTR VkResult VKAPI_CALL LayerQueuePresent(VkQueue, const VkPresentInfoKHR*);
VKAPI_ATTR VkResult VKAPI_CALL LayerCreateSwapchain(VkDevice, const VkSwapchainCreateInfoKHR*, const VkAllocationCallbacks*, VkSwapchainKHR*);
VKAPI_ATTR VkResult VKAPI_CALL LayerGetSwapchainImages(VkDevice, VkSwapchainKHR, uint32_t*, VkImage*);
VKAPI_ATTR VkResult VKAPI_CALL LayerAcquireNextImage(VkDevice, VkSwapchainKHR, uint64_t, VkSemaphore, VkFence, uint32_t*);
VKAPI_ATTR void VKAPI_CALL LayerDestroySwapchain(VkDevice, VkSwapchainKHR, const VkAllocationCallbacks*);

extern "C" __attribute__((visibility("default"))) VKAPI_ATTR VkResult VKAPI_CALL vkEnumerateInstanceLayerProperties(uint32_t* count, VkLayerProperties* props) {
    if (!count) return VK_ERROR_INITIALIZATION_FAILED;
    if (!props) { *count = 1; return VK_SUCCESS; }
    if (*count == 0) return VK_INCOMPLETE;
    VkLayerProperties p{};
    std::strncpy(p.layerName, kLayerName, sizeof(p.layerName) - 1);
    p.specVersion = VK_API_VERSION_1_0;
    p.implementationVersion = 1;
    std::strncpy(p.description, "UltraScale API lab pass-through layer", sizeof(p.description) - 1);
    props[0] = p; *count = 1; return VK_SUCCESS;
}
extern "C" __attribute__((visibility("default"))) VKAPI_ATTR VkResult VKAPI_CALL vkEnumerateDeviceLayerProperties(VkPhysicalDevice, uint32_t* count, VkLayerProperties* props) {
    return vkEnumerateInstanceLayerProperties(count, props);
}
extern "C" __attribute__((visibility("default"))) VKAPI_ATTR VkResult VKAPI_CALL vkEnumerateInstanceExtensionProperties(const char*, uint32_t* count, VkExtensionProperties*) {
    if (!count) return VK_ERROR_INITIALIZATION_FAILED;
    *count = 0;
    return VK_SUCCESS;
}
extern "C" __attribute__((visibility("default"))) VKAPI_ATTR VkResult VKAPI_CALL vkEnumerateDeviceExtensionProperties(VkPhysicalDevice, const char*, uint32_t* count, VkExtensionProperties*) {
    if (!count) return VK_ERROR_INITIALIZATION_FAILED;
    *count = 0;
    return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL LayerCreateInstance(const VkInstanceCreateInfo* info, const VkAllocationCallbacks* allocator, VkInstance* instance) {
    auto* chain = FindInstanceLink(info);
    if (!chain || !chain->u.pLayerInfo) return VK_ERROR_INITIALIZATION_FAILED;
    auto* link = chain->u.pLayerInfo;
    PFN_vkGetInstanceProcAddr nextGipa = link->pfnNextGetInstanceProcAddr;
    chain->u.pLayerInfo = link->pNext;
    auto nextCreate = reinterpret_cast<PFN_vkCreateInstance>(nextGipa(VK_NULL_HANDLE, "vkCreateInstance"));
    if (!nextCreate) return VK_ERROR_INITIALIZATION_FAILED;
    VkResult result = nextCreate(info, allocator, instance);
    if (result == VK_SUCCESS) {
        std::lock_guard<std::mutex> lock(gMutex);
        gInstances[*instance] = nextGipa;
        __android_log_print(ANDROID_LOG_INFO, "UltraScaleVK", "Vulkan layer attached to instance");
    }
    return result;
}
VKAPI_ATTR VkResult VKAPI_CALL LayerCreateDevice(VkPhysicalDevice physical, const VkDeviceCreateInfo* info, const VkAllocationCallbacks* allocator, VkDevice* device) {
    auto* chain = FindDeviceLink(info);
    if (!chain || !chain->u.pLayerInfo) return VK_ERROR_INITIALIZATION_FAILED;
    auto* link = chain->u.pLayerInfo;
    PFN_vkGetInstanceProcAddr nextGipa = link->pfnNextGetInstanceProcAddr;
    PFN_vkGetDeviceProcAddr nextGdpa = link->pfnNextGetDeviceProcAddr;
    chain->u.pLayerInfo = link->pNext;
    VkInstance instance = VK_NULL_HANDLE;
    {
        std::lock_guard<std::mutex> lock(gMutex);
        auto found = gPhysicalDevices.find(physical);
        if (found != gPhysicalDevices.end()) instance = found->second;
    }
    if (instance == VK_NULL_HANDLE) return VK_ERROR_INITIALIZATION_FAILED;
    auto nextCreate = reinterpret_cast<PFN_vkCreateDevice>(nextGipa(instance, "vkCreateDevice"));
    if (!nextCreate) return VK_ERROR_INITIALIZATION_FAILED;
    VkResult result = nextCreate(physical, info, allocator, device);
    if (result == VK_SUCCESS && nextGdpa) {
        std::lock_guard<std::mutex> lock(gMutex);
        gDevices[*device] = nextGdpa;
        __android_log_print(ANDROID_LOG_INFO, "UltraScaleVK", "Vulkan layer attached to device");
    }
    return result;
}
VKAPI_ATTR VkResult VKAPI_CALL LayerEnumeratePhysicalDevices(VkInstance instance, uint32_t* count, VkPhysicalDevice* devices) {
    if (!count) return VK_ERROR_INITIALIZATION_FAILED;

    PFN_vkGetInstanceProcAddr next = nullptr;
    {
        std::lock_guard<std::mutex> lock(gMutex);
        auto found = gInstances.find(instance);
        if (found != gInstances.end()) next = found->second;
    }
    if (!next) return VK_ERROR_INITIALIZATION_FAILED;

    auto enumerate = reinterpret_cast<PFN_vkEnumeratePhysicalDevices>(
        next(instance, "vkEnumeratePhysicalDevices"));
    if (!enumerate) return VK_ERROR_INITIALIZATION_FAILED;

    const VkResult result = enumerate(instance, count, devices);
    if ((result == VK_SUCCESS || result == VK_INCOMPLETE) && devices) {
        std::lock_guard<std::mutex> lock(gMutex);
        for (uint32_t i = 0; i < *count; ++i) {
            if (devices[i] != VK_NULL_HANDLE) gPhysicalDevices[devices[i]] = instance;
        }
    }
    return result;
}

VKAPI_ATTR VkResult VKAPI_CALL LayerEnumeratePhysicalDeviceGroups(
    VkInstance instance, uint32_t* count, VkPhysicalDeviceGroupProperties* groups) {
    if (!count) return VK_ERROR_INITIALIZATION_FAILED;

    PFN_vkGetInstanceProcAddr next = nullptr;
    {
        std::lock_guard<std::mutex> lock(gMutex);
        auto found = gInstances.find(instance);
        if (found != gInstances.end()) next = found->second;
    }
    if (!next) return VK_ERROR_INITIALIZATION_FAILED;

    auto enumerate = reinterpret_cast<PFN_vkEnumeratePhysicalDeviceGroups>(
        next(instance, "vkEnumeratePhysicalDeviceGroups"));
    if (!enumerate) {
        enumerate = reinterpret_cast<PFN_vkEnumeratePhysicalDeviceGroups>(
            next(instance, "vkEnumeratePhysicalDeviceGroupsKHR"));
    }
    if (!enumerate) return VK_ERROR_INITIALIZATION_FAILED;

    const VkResult result = enumerate(instance, count, groups);
    if ((result == VK_SUCCESS || result == VK_INCOMPLETE) && groups) {
        std::lock_guard<std::mutex> lock(gMutex);
        for (uint32_t i = 0; i < *count; ++i) {
            const uint32_t memberCount =
                groups[i].physicalDeviceCount < VK_MAX_DEVICE_GROUP_SIZE
                    ? groups[i].physicalDeviceCount : VK_MAX_DEVICE_GROUP_SIZE;
            for (uint32_t j = 0; j < memberCount; ++j) {
                const VkPhysicalDevice physical = groups[i].physicalDevices[j];
                if (physical != VK_NULL_HANDLE) gPhysicalDevices[physical] = instance;
            }
        }
    }
    return result;
}

VKAPI_ATTR void VKAPI_CALL LayerDestroyInstance(VkInstance instance, const VkAllocationCallbacks* allocator) {
    PFN_vkGetInstanceProcAddr next = nullptr;
    { std::lock_guard<std::mutex> lock(gMutex); auto it = gInstances.find(instance); if (it != gInstances.end()) { next = it->second; gInstances.erase(it); }
      for (auto p = gPhysicalDevices.begin(); p != gPhysicalDevices.end();) {
          if (p->second == instance) p = gPhysicalDevices.erase(p); else ++p;
      } }
    auto destroy = next ? reinterpret_cast<PFN_vkDestroyInstance>(next(instance, "vkDestroyInstance")) : nullptr;
    if (destroy) destroy(instance, allocator);
}
VKAPI_ATTR void VKAPI_CALL LayerDestroyDevice(VkDevice device, const VkAllocationCallbacks* allocator) {
    PFN_vkGetDeviceProcAddr next = nullptr;
    { std::lock_guard<std::mutex> lock(gMutex);
      auto it = gDevices.find(device); if (it != gDevices.end()) { next = it->second; gDevices.erase(it); }
      for (auto q = gQueueDevices.begin(); q != gQueueDevices.end();) {
          if (q->second == device) { gQueues.erase(q->first); q = gQueueDevices.erase(q); } else ++q;
      } }
    auto destroy = next ? reinterpret_cast<PFN_vkDestroyDevice>(next(device, "vkDestroyDevice")) : nullptr;
    if (destroy) destroy(device, allocator);
}
VKAPI_ATTR void VKAPI_CALL LayerGetDeviceQueue(VkDevice device, uint32_t family, uint32_t index, VkQueue* queue) {
    PFN_vkGetDeviceProcAddr next = nullptr;
    { std::lock_guard<std::mutex> lock(gMutex); auto it = gDevices.find(device); if (it != gDevices.end()) next = it->second; }
    auto getQueue = next ? reinterpret_cast<PFN_vkGetDeviceQueue>(next(device, "vkGetDeviceQueue")) : nullptr;
    if (!getQueue) return;
    getQueue(device, family, index, queue);
    if (queue && *queue && next) {
        auto present = reinterpret_cast<PFN_vkQueuePresentKHR>(next(device, "vkQueuePresentKHR"));
        if (present) { std::lock_guard<std::mutex> lock(gMutex); gQueues[*queue] = present; gQueueDevices[*queue] = device; }
    }
}
VKAPI_ATTR void VKAPI_CALL LayerGetDeviceQueue2(VkDevice device, const VkDeviceQueueInfo2* info, VkQueue* queue) {
    PFN_vkGetDeviceProcAddr next = nullptr;
    { std::lock_guard<std::mutex> lock(gMutex); auto it = gDevices.find(device); if (it != gDevices.end()) next = it->second; }
    auto getQueue = next ? reinterpret_cast<PFN_vkGetDeviceQueue2>(next(device, "vkGetDeviceQueue2")) : nullptr;
    if (!getQueue) return;
    getQueue(device, info, queue);
    if (queue && *queue && next) {
        auto present = reinterpret_cast<PFN_vkQueuePresentKHR>(next(device, "vkQueuePresentKHR"));
        if (present) { std::lock_guard<std::mutex> lock(gMutex); gQueues[*queue] = present; gQueueDevices[*queue] = device; }
    }
}
VKAPI_ATTR VkResult VKAPI_CALL LayerCreateSwapchain(
    VkDevice device, const VkSwapchainCreateInfoKHR* info,
    const VkAllocationCallbacks* allocator, VkSwapchainKHR* swapchain) {
    PFN_vkGetDeviceProcAddr gdpa = nullptr;
    { std::lock_guard<std::mutex> lock(gMutex); auto it = gDevices.find(device); if (it != gDevices.end()) gdpa = it->second; }
    auto create = gdpa ? reinterpret_cast<PFN_vkCreateSwapchainKHR>(gdpa(device, "vkCreateSwapchainKHR")) : nullptr;
    if (!create) return VK_ERROR_EXTENSION_NOT_PRESENT;
    const VkResult result = create(device, info, allocator, swapchain);
    if (result == VK_SUCCESS && info && swapchain && *swapchain) {
        std::lock_guard<std::mutex> lock(gMutex);
        gSwapchains[*swapchain] = {info->imageFormat, info->imageExtent, 0};
        __android_log_print(ANDROID_LOG_INFO, "UltraScaleVK",
            "Swapchain created extent=%ux%u format=%d images=%u",
            info->imageExtent.width, info->imageExtent.height,
            static_cast<int>(info->imageFormat), info->minImageCount);
    }
    return result;
}

VKAPI_ATTR VkResult VKAPI_CALL LayerGetSwapchainImages(
    VkDevice device, VkSwapchainKHR swapchain, uint32_t* count, VkImage* images) {
    PFN_vkGetDeviceProcAddr gdpa = nullptr;
    { std::lock_guard<std::mutex> lock(gMutex); auto it = gDevices.find(device); if (it != gDevices.end()) gdpa = it->second; }
    auto getImages = gdpa ? reinterpret_cast<PFN_vkGetSwapchainImagesKHR>(gdpa(device, "vkGetSwapchainImagesKHR")) : nullptr;
    if (!getImages) return VK_ERROR_EXTENSION_NOT_PRESENT;
    const VkResult result = getImages(device, swapchain, count, images);
    if ((result == VK_SUCCESS || result == VK_INCOMPLETE) && count) {
        std::lock_guard<std::mutex> lock(gMutex);
        auto it = gSwapchains.find(swapchain);
        if (it != gSwapchains.end()) it->second.imageCount = *count;
    }
    return result;
}

VKAPI_ATTR VkResult VKAPI_CALL LayerAcquireNextImage(
    VkDevice device, VkSwapchainKHR swapchain, uint64_t timeout,
    VkSemaphore semaphore, VkFence fence, uint32_t* imageIndex) {
    PFN_vkGetDeviceProcAddr gdpa = nullptr;
    { std::lock_guard<std::mutex> lock(gMutex); auto it = gDevices.find(device); if (it != gDevices.end()) gdpa = it->second; }
    auto acquire = gdpa ? reinterpret_cast<PFN_vkAcquireNextImageKHR>(gdpa(device, "vkAcquireNextImageKHR")) : nullptr;
    if (!acquire) return VK_ERROR_EXTENSION_NOT_PRESENT;
    const VkResult result = acquire(device, swapchain, timeout, semaphore, fence, imageIndex);
    if (result == VK_SUCCESS || result == VK_SUBOPTIMAL_KHR) {
        SwapchainInfo info{};
        { std::lock_guard<std::mutex> lock(gMutex); auto it = gSwapchains.find(swapchain); if (it != gSwapchains.end()) info = it->second; }
        __android_log_print(ANDROID_LOG_VERBOSE, "UltraScaleVK",
            "Acquire swapchain image=%u extent=%ux%u result=%d",
            imageIndex ? *imageIndex : 0, info.extent.width, info.extent.height, result);
    }
    return result;
}

VKAPI_ATTR void VKAPI_CALL LayerDestroySwapchain(
    VkDevice device, VkSwapchainKHR swapchain, const VkAllocationCallbacks* allocator) {
    PFN_vkGetDeviceProcAddr gdpa = nullptr;
    { std::lock_guard<std::mutex> lock(gMutex); auto it = gDevices.find(device); if (it != gDevices.end()) gdpa = it->second; }
    auto destroy = gdpa ? reinterpret_cast<PFN_vkDestroySwapchainKHR>(gdpa(device, "vkDestroySwapchainKHR")) : nullptr;
    { std::lock_guard<std::mutex> lock(gMutex); gSwapchains.erase(swapchain); }
    if (destroy) destroy(device, swapchain, allocator);
}

VKAPI_ATTR VkResult VKAPI_CALL LayerQueuePresent(VkQueue queue, const VkPresentInfoKHR* info) {
    PFN_vkQueuePresentKHR next = nullptr;
    { std::lock_guard<std::mutex> lock(gMutex); auto it = gQueues.find(queue); if (it != gQueues.end()) next = it->second; }
    if (!next) return VK_ERROR_INITIALIZATION_FAILED;
    const uint64_t frame = ++gPresentedFrames;
    if ((frame % 600) == 1 && info) {
        for (uint32_t i = 0; i < info->swapchainCount; ++i) {
            SwapchainInfo state{};
            { std::lock_guard<std::mutex> lock(gMutex); auto it = gSwapchains.find(info->pSwapchains[i]); if (it != gSwapchains.end()) state = it->second; }
            const uint32_t imageIndex = info->pImageIndices ? info->pImageIndices[i] : 0;
            __android_log_print(ANDROID_LOG_INFO, "UltraScaleVK",
                "Present frame=%llu swapchain_image=%u extent=%ux%u format=%d image_count=%u",
                static_cast<unsigned long long>(frame), imageIndex,
                state.extent.width, state.extent.height, static_cast<int>(state.format), state.imageCount);
        }
    }
    return next(queue, info);
}

extern "C" __attribute__((visibility("default"))) VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL vkGetInstanceProcAddr(VkInstance instance, const char* name) {
    if (!name) return nullptr;
    if (std::strcmp(name, "vkGetInstanceProcAddr") == 0) return reinterpret_cast<PFN_vkVoidFunction>(vkGetInstanceProcAddr);
    if (std::strcmp(name, "vkCreateInstance") == 0) return reinterpret_cast<PFN_vkVoidFunction>(LayerCreateInstance);
    if (std::strcmp(name, "vkEnumerateInstanceLayerProperties") == 0) return reinterpret_cast<PFN_vkVoidFunction>(vkEnumerateInstanceLayerProperties);
    if (std::strcmp(name, "vkEnumerateInstanceExtensionProperties") == 0) return reinterpret_cast<PFN_vkVoidFunction>(vkEnumerateInstanceExtensionProperties);
    if (std::strcmp(name, "vkEnumerateDeviceLayerProperties") == 0) return reinterpret_cast<PFN_vkVoidFunction>(vkEnumerateDeviceLayerProperties);
    if (std::strcmp(name, "vkEnumerateDeviceExtensionProperties") == 0) return reinterpret_cast<PFN_vkVoidFunction>(vkEnumerateDeviceExtensionProperties);
    if (std::strcmp(name, "vkDestroyInstance") == 0) return reinterpret_cast<PFN_vkVoidFunction>(LayerDestroyInstance);
    if (std::strcmp(name, "vkCreateDevice") == 0) return reinterpret_cast<PFN_vkVoidFunction>(LayerCreateDevice);
    if (std::strcmp(name, "vkEnumeratePhysicalDevices") == 0) return reinterpret_cast<PFN_vkVoidFunction>(LayerEnumeratePhysicalDevices);
    if (std::strcmp(name, "vkEnumeratePhysicalDeviceGroups") == 0 ||
        std::strcmp(name, "vkEnumeratePhysicalDeviceGroupsKHR") == 0)
        return reinterpret_cast<PFN_vkVoidFunction>(LayerEnumeratePhysicalDeviceGroups);
    PFN_vkGetInstanceProcAddr next = nullptr;
    { std::lock_guard<std::mutex> lock(gMutex); auto it = gInstances.find(instance); if (it != gInstances.end()) next = it->second; }
    return next ? next(instance, name) : nullptr;
}
extern "C" __attribute__((visibility("default"))) VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL vkGetDeviceProcAddr(VkDevice device, const char* name) {
    if (!name) return nullptr;
    if (std::strcmp(name, "vkGetDeviceProcAddr") == 0) return reinterpret_cast<PFN_vkVoidFunction>(vkGetDeviceProcAddr);
    if (std::strcmp(name, "vkDestroyDevice") == 0) return reinterpret_cast<PFN_vkVoidFunction>(LayerDestroyDevice);
    if (std::strcmp(name, "vkGetDeviceQueue") == 0) return reinterpret_cast<PFN_vkVoidFunction>(LayerGetDeviceQueue);
    if (std::strcmp(name, "vkGetDeviceQueue2") == 0) return reinterpret_cast<PFN_vkVoidFunction>(LayerGetDeviceQueue2);
    if (std::strcmp(name, "vkQueuePresentKHR") == 0) return reinterpret_cast<PFN_vkVoidFunction>(LayerQueuePresent);
    if (std::strcmp(name, "vkCreateSwapchainKHR") == 0) return reinterpret_cast<PFN_vkVoidFunction>(LayerCreateSwapchain);
    if (std::strcmp(name, "vkGetSwapchainImagesKHR") == 0) return reinterpret_cast<PFN_vkVoidFunction>(LayerGetSwapchainImages);
    if (std::strcmp(name, "vkAcquireNextImageKHR") == 0) return reinterpret_cast<PFN_vkVoidFunction>(LayerAcquireNextImage);
    if (std::strcmp(name, "vkDestroySwapchainKHR") == 0) return reinterpret_cast<PFN_vkVoidFunction>(LayerDestroySwapchain);
    PFN_vkGetDeviceProcAddr next = nullptr;
    { std::lock_guard<std::mutex> lock(gMutex); auto it = gDevices.find(device); if (it != gDevices.end()) next = it->second; }
    return next ? next(device, name) : nullptr;
}
extern "C" __attribute__((visibility("default"))) VKAPI_ATTR VkResult VKAPI_CALL vkNegotiateLoaderLayerInterfaceVersion(VkNegotiateLayerInterface* version) {
    if (!version || version->sType != LAYER_NEGOTIATE_INTERFACE_STRUCT) return VK_ERROR_INITIALIZATION_FAILED;
    if (version->loaderLayerInterfaceVersion > CURRENT_LOADER_LAYER_INTERFACE_VERSION)
        version->loaderLayerInterfaceVersion = CURRENT_LOADER_LAYER_INTERFACE_VERSION;
    version->pfnGetInstanceProcAddr = vkGetInstanceProcAddr;
    version->pfnGetDeviceProcAddr = vkGetDeviceProcAddr;
    version->pfnGetPhysicalDeviceProcAddr = nullptr;
    return VK_SUCCESS;
}
