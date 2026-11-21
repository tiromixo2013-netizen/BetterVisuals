#include <vulkan/vulkan.h>
#include <vulkan/vk_layer.h>
#include <android/log.h>
#include <cstring>
#include <mutex>
#include <unordered_map>

namespace {
constexpr char kLayerName[] = "VK_LAYER_ULTRASCALE_api_lab";
std::mutex gMutex;
std::unordered_map<VkInstance, PFN_vkGetInstanceProcAddr> gInstances;
std::unordered_map<VkDevice, PFN_vkGetDeviceProcAddr> gDevices;

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
VKAPI_ATTR VkResult VKAPI_CALL LayerCreateInstance(const VkInstanceCreateInfo*, const VkAllocationCallbacks*, VkInstance*);
VKAPI_ATTR VkResult VKAPI_CALL LayerCreateDevice(VkPhysicalDevice, const VkDeviceCreateInfo*, const VkAllocationCallbacks*, VkDevice*);
VKAPI_ATTR void VKAPI_CALL LayerDestroyInstance(VkInstance, const VkAllocationCallbacks*);
VKAPI_ATTR void VKAPI_CALL LayerDestroyDevice(VkDevice, const VkAllocationCallbacks*);
}

extern "C" VKAPI_ATTR VkResult VKAPI_CALL vkEnumerateInstanceLayerProperties(uint32_t* count, VkLayerProperties* props) {
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
extern "C" VKAPI_ATTR VkResult VKAPI_CALL vkEnumerateDeviceLayerProperties(VkPhysicalDevice, uint32_t* count, VkLayerProperties* props) {
    return vkEnumerateInstanceLayerProperties(count, props);
}
extern "C" VKAPI_ATTR VkResult VKAPI_CALL vkEnumerateInstanceExtensionProperties(const char*, uint32_t* count, VkExtensionProperties*) {
    if (!count) return VK_ERROR_INITIALIZATION_FAILED;
    *count = 0;
    return VK_SUCCESS;
}
extern "C" VKAPI_ATTR VkResult VKAPI_CALL vkEnumerateDeviceExtensionProperties(VkPhysicalDevice, const char*, uint32_t* count, VkExtensionProperties*) {
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
    auto nextCreate = reinterpret_cast<PFN_vkCreateDevice>(
        nextGipa(reinterpret_cast<VkInstance>(physical), "vkCreateDevice"));
    if (!nextCreate) return VK_ERROR_INITIALIZATION_FAILED;
    VkResult result = nextCreate(physical, info, allocator, device);
    if (result == VK_SUCCESS && nextGdpa) {
        std::lock_guard<std::mutex> lock(gMutex);
        gDevices[*device] = nextGdpa;
        __android_log_print(ANDROID_LOG_INFO, "UltraScaleVK", "Vulkan layer attached to device");
    }
    return result;
}
VKAPI_ATTR void VKAPI_CALL LayerDestroyInstance(VkInstance instance, const VkAllocationCallbacks* allocator) {
    PFN_vkGetInstanceProcAddr next = nullptr;
    { std::lock_guard<std::mutex> lock(gMutex); auto it = gInstances.find(instance); if (it != gInstances.end()) { next = it->second; gInstances.erase(it); } }
    auto destroy = next ? reinterpret_cast<PFN_vkDestroyInstance>(next(instance, "vkDestroyInstance")) : nullptr;
    if (destroy) destroy(instance, allocator);
}
VKAPI_ATTR void VKAPI_CALL LayerDestroyDevice(VkDevice device, const VkAllocationCallbacks* allocator) {
    PFN_vkGetDeviceProcAddr next = nullptr;
    { std::lock_guard<std::mutex> lock(gMutex); auto it = gDevices.find(device); if (it != gDevices.end()) { next = it->second; gDevices.erase(it); } }
    auto destroy = next ? reinterpret_cast<PFN_vkDestroyDevice>(next(device, "vkDestroyDevice")) : nullptr;
    if (destroy) destroy(device, allocator);
}

extern "C" VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL vkGetInstanceProcAddr(VkInstance instance, const char* name) {
    if (!name) return nullptr;
    if (std::strcmp(name, "vkGetInstanceProcAddr") == 0) return reinterpret_cast<PFN_vkVoidFunction>(vkGetInstanceProcAddr);
    if (std::strcmp(name, "vkCreateInstance") == 0) return reinterpret_cast<PFN_vkVoidFunction>(LayerCreateInstance);
    if (std::strcmp(name, "vkEnumerateInstanceLayerProperties") == 0) return reinterpret_cast<PFN_vkVoidFunction>(vkEnumerateInstanceLayerProperties);
    if (std::strcmp(name, "vkEnumerateInstanceExtensionProperties") == 0) return reinterpret_cast<PFN_vkVoidFunction>(vkEnumerateInstanceExtensionProperties);
    if (std::strcmp(name, "vkEnumerateDeviceLayerProperties") == 0) return reinterpret_cast<PFN_vkVoidFunction>(vkEnumerateDeviceLayerProperties);
    if (std::strcmp(name, "vkEnumerateDeviceExtensionProperties") == 0) return reinterpret_cast<PFN_vkVoidFunction>(vkEnumerateDeviceExtensionProperties);
    if (std::strcmp(name, "vkDestroyInstance") == 0) return reinterpret_cast<PFN_vkVoidFunction>(LayerDestroyInstance);
    if (std::strcmp(name, "vkCreateDevice") == 0) return reinterpret_cast<PFN_vkVoidFunction>(LayerCreateDevice);
    PFN_vkGetInstanceProcAddr next = nullptr;
    { std::lock_guard<std::mutex> lock(gMutex); auto it = gInstances.find(instance); if (it != gInstances.end()) next = it->second; }
    return next ? next(instance, name) : nullptr;
}
extern "C" VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL vkGetDeviceProcAddr(VkDevice device, const char* name) {
    if (!name) return nullptr;
    if (std::strcmp(name, "vkGetDeviceProcAddr") == 0) return reinterpret_cast<PFN_vkVoidFunction>(vkGetDeviceProcAddr);
    if (std::strcmp(name, "vkDestroyDevice") == 0) return reinterpret_cast<PFN_vkVoidFunction>(LayerDestroyDevice);
    PFN_vkGetDeviceProcAddr next = nullptr;
    { std::lock_guard<std::mutex> lock(gMutex); auto it = gDevices.find(device); if (it != gDevices.end()) next = it->second; }
    return next ? next(device, name) : nullptr;
}
extern "C" VKAPI_ATTR VkResult VKAPI_CALL vkNegotiateLoaderLayerInterfaceVersion(VkNegotiateLayerInterface* version) {
    if (!version || version->sType != LAYER_NEGOTIATE_INTERFACE_STRUCT) return VK_ERROR_INITIALIZATION_FAILED;
    if (version->loaderLayerInterfaceVersion > CURRENT_LOADER_LAYER_INTERFACE_VERSION)
        version->loaderLayerInterfaceVersion = CURRENT_LOADER_LAYER_INTERFACE_VERSION;
    version->pfnGetInstanceProcAddr = vkGetInstanceProcAddr;
    version->pfnGetDeviceProcAddr = vkGetDeviceProcAddr;
    version->pfnGetPhysicalDeviceProcAddr = nullptr;
    return VK_SUCCESS;
}
