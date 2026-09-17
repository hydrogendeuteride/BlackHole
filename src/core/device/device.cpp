#include "device.h"
#include "config.h"
#include "SDL2/SDL.h"
#include "SDL2/SDL_vulkan.h"

#include <cstdlib>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>

static bool env_truthy(const char *name)
{
    const char *v = std::getenv(name);
    if (!v) return false;
    if (!*v) return true;
    return std::strcmp(v, "0") != 0 && std::strcmp(v, "false") != 0 && std::strcmp(v, "FALSE") != 0;
}

// Create Vulkan instance/device, enable debug/validation (in Debug), pick a GPU,
// and set up VMA with buffer device address. If available, enable Ray Query and
// Acceleration Structure extensions + features.
void DeviceManager::init_vulkan(SDL_Window *window)
{
    vkb::InstanceBuilder builder;

    //make the vulkan instance, with basic debug features
    auto inst_ret = builder.set_app_name("Example Vulkan Application")
            .request_validation_layers(kUseValidationLayers)
            .use_default_debug_messenger()
            .require_api_version(1, 3, 0)
            .build();

    if (!inst_ret)
    {
        throw std::runtime_error("[Device] Failed to create Vulkan instance: " + inst_ret.error().message());
    }
    vkb::Instance vkb_inst = inst_ret.value();

    //grab the instance
    _instance = vkb_inst.instance;
    _debug_messenger = vkb_inst.debug_messenger;

    if (SDL_Vulkan_CreateSurface(window, _instance, &_surface) != SDL_TRUE)
    {
        throw std::runtime_error(std::string("[Device] Failed to create Vulkan surface: ") + SDL_GetError());
    }

    VkPhysicalDeviceVulkan13Features features{.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES};
    features.dynamicRendering = true;
    features.synchronization2 = true;

    VkPhysicalDeviceVulkan12Features features12{.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES};
    features12.bufferDeviceAddress = VK_TRUE;
    features12.descriptorIndexing = VK_TRUE;
    // Enable update-after-bind related toggles for graphics/compute descriptors
    features12.descriptorBindingPartiallyBound = VK_TRUE;
    features12.descriptorBindingUpdateUnusedWhilePending = VK_TRUE;
    features12.runtimeDescriptorArray = VK_TRUE;
    features12.descriptorBindingUniformBufferUpdateAfterBind = VK_TRUE;
    features12.descriptorBindingStorageBufferUpdateAfterBind = VK_TRUE;
    features12.descriptorBindingSampledImageUpdateAfterBind = VK_TRUE;
    features12.descriptorBindingStorageImageUpdateAfterBind = VK_TRUE;

    //use vkbootstrap to select a gpu.
    //We want a gpu that can write to the SDL surface and supports vulkan 1.3
    vkb::PhysicalDeviceSelector selector{vkb_inst};
    auto physical_device_ret = selector
            .set_minimum_version(1, 3)
            .set_required_features_13(features)
            .set_required_features_12(features12)
            .set_surface(_surface)
            .select();
    if (!physical_device_ret)
    {
        throw std::runtime_error("[Device] Failed to select physical device: " + physical_device_ret.error().message());
    }
    vkb::PhysicalDevice physicalDevice = physical_device_ret.value();
    const vkb::PhysicalDevice basePhysicalDevice = physicalDevice;

    //physicalDevice.features.
    // Enable ray tracing extensions on the physical device if supported (before creating the DeviceBuilder)
    // Query ray tracing capability on the chosen physical device
    {
        const bool nvidiaGpu = physicalDevice.properties.vendorID == 0x10de;
        // NVIDIA's ICD can fail vkCreateDevice when a sanitized Debug build
        // requests RayQuery/AccelerationStructure. Release builds do not define
        // VULKAN_ENGINE_DEBUG_SANITIZERS_ENABLED and keep this path enabled.
        const bool disableNvidiaSanitizedRayQuery =
#if defined(VULKAN_ENGINE_DEBUG_SANITIZERS_ENABLED)
            nvidiaGpu;
#else
            false;
#endif
        const bool forceDisableRayQuery =
            env_truthy("VKG_DISABLE_RAY_QUERY") ||
            env_truthy("VE_DISABLE_RAY_QUERY") ||
            disableNvidiaSanitizedRayQuery;

        VkPhysicalDeviceAccelerationStructureFeaturesKHR accelFeat{
            .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR };
        VkPhysicalDeviceRayQueryFeaturesKHR rayqFeat{
            .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_QUERY_FEATURES_KHR,
            .pNext = &accelFeat };
        VkPhysicalDeviceFeatures2 feats2{ .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2, .pNext = &rayqFeat };
        vkGetPhysicalDeviceFeatures2(physicalDevice.physical_device, &feats2);
        const bool rayQueryCapable = (rayqFeat.rayQuery == VK_TRUE);
        const bool accelCapable = (accelFeat.accelerationStructure == VK_TRUE);

        const bool rayQueryExtensionsAvailable =
            physicalDevice.is_extension_present(VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME) &&
            physicalDevice.is_extension_present(VK_KHR_RAY_QUERY_EXTENSION_NAME) &&
            physicalDevice.is_extension_present(VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME);

        _rayQuerySupported = rayQueryCapable && rayQueryExtensionsAvailable;
        _accelStructSupported = accelCapable && rayQueryExtensionsAvailable;

        if (forceDisableRayQuery)
        {
            _rayQuerySupported = false;
            _accelStructSupported = false;
        }

        Logger::info("[Device] RayQuery support: {} | AccelStruct: {}",
                   rayQueryCapable ? "yes" : "no",
                   accelCapable ? "yes" : "no");

        if ((rayQueryCapable || accelCapable) && !rayQueryExtensionsAvailable)
        {
            Logger::warn("[Device] RayQuery/AccelStruct feature reported, but required KHR extensions are incomplete; disabling ray-query path.");
        }

        if (forceDisableRayQuery && rayQueryCapable && accelCapable)
        {
            if (disableNvidiaSanitizedRayQuery)
            {
                Logger::info("[Device] NVIDIA sanitized debug path detected; forcing RayQuery/AccelStruct OFF for compatibility.");
            }
            else
            {
                Logger::info("[Device] VKG_DISABLE_RAY_QUERY is set; forcing RayQuery/AccelStruct OFF for compatibility.");
            }
        }
    }

    bool rayQueryRequested = _rayQuerySupported && _accelStructSupported;

    if (rayQueryRequested)
    {
        const std::vector<const char *> rayQueryExtensions = {
            VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME,
            VK_KHR_RAY_QUERY_EXTENSION_NAME,
            VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME,
        };

        VkPhysicalDeviceAccelerationStructureFeaturesKHR accelReq{
            .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR,
            .accelerationStructure = VK_TRUE,
        };
        VkPhysicalDeviceRayQueryFeaturesKHR rayqReq{
            .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_QUERY_FEATURES_KHR,
            .rayQuery = VK_TRUE,
        };

        const bool rayQueryEnableOk =
            physicalDevice.enable_extensions_if_present(rayQueryExtensions) &&
            physicalDevice.enable_extension_features_if_present(accelReq) &&
            physicalDevice.enable_extension_features_if_present(rayqReq);

        if (!rayQueryEnableOk)
        {
            Logger::warn("[Device] Failed to enable RayQuery/AccelStruct device requirements; disabling ray-query path.");
            _rayQuerySupported = false;
            _accelStructSupported = false;
            rayQueryRequested = false;
            physicalDevice = basePhysicalDevice;
        }
    }

    //create the final vulkan device
    vkb::DeviceBuilder deviceBuilder{physicalDevice};

    auto device_ret = deviceBuilder.build();
    if (!device_ret && rayQueryRequested)
    {
        Logger::warn("[Device] Failed to create logical device with RayQuery/AccelStruct enabled ({}); retrying without ray-query path.",
                     device_ret.error().message());
        _rayQuerySupported = false;
        _accelStructSupported = false;

        vkb::DeviceBuilder fallbackDeviceBuilder{basePhysicalDevice};
        device_ret = fallbackDeviceBuilder.build();
    }
    if (!device_ret)
    {
        throw std::runtime_error("[Device] Failed to create logical device: " + device_ret.error().message());
    }
    vkb::Device vkbDevice = device_ret.value();

    // Get the VkDevice handle used in the rest of a vulkan application
    _device = vkbDevice.device;
    _chosenGPU = physicalDevice.physical_device;

    // use vkbootstrap to get a Graphics queue
    auto graphics_queue_ret = vkbDevice.get_queue(vkb::QueueType::graphics);
    if (!graphics_queue_ret)
    {
        throw std::runtime_error("[Device] Failed to get graphics queue: " + graphics_queue_ret.error().message());
    }
    _graphicsQueue = graphics_queue_ret.value();

    auto graphics_queue_index_ret = vkbDevice.get_queue_index(vkb::QueueType::graphics);
    if (!graphics_queue_index_ret)
    {
        throw std::runtime_error("[Device] Failed to get graphics queue family: " +
                                 graphics_queue_index_ret.error().message());
    }
    _graphicsQueueFamily = graphics_queue_index_ret.value();

    //> vma_init
    //initialize the memory allocator
    VmaAllocatorCreateInfo allocatorInfo = {};
    allocatorInfo.physicalDevice = _chosenGPU;
    allocatorInfo.device = _device;
    allocatorInfo.instance = _instance;
    allocatorInfo.flags = VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT;
    vmaCreateAllocator(&allocatorInfo, &_allocator);

    _deletionQueue.push_function([&]() {
        vmaDestroyAllocator(_allocator);
    });
    //< vma_init
}

void DeviceManager::cleanup()
{
    // Always query VMA stats once before destroying the allocator so we can
    // spot leaks that would trigger the vk_mem_alloc.h assertion:
    // "Some allocations were not freed before destruction of this memory block!"
    if (_allocator)
    {
        VmaTotalStatistics stats{};
        vmaCalculateStatistics(_allocator, &stats);
        const VmaStatistics &s = stats.total.statistics;

        if (s.allocationCount != 0)
        {
            Logger::warn("[VMA] WARNING: {} live allocations ({} bytes) remain before allocator destruction -- this will trip vk_mem_alloc.h assertion: \"Some allocations were not freed before destruction of this memory block!\"",
                       (size_t)s.allocationCount,
                       (unsigned long long)s.allocationBytes);
        }
        else if (vmaDebugEnabled())
        {
            Logger::info("[VMA] Blocks: {} | Allocations: {} | BlockBytes: {} | AllocationBytes: {}",
                       (size_t)s.blockCount,
                       (size_t)s.allocationCount,
                       (unsigned long long)s.blockBytes,
                       (unsigned long long)s.allocationBytes);
        }
    }
    vkDestroySurfaceKHR(_instance, _surface, nullptr);
    _deletionQueue.flush();
    vkDestroyDevice(_device, nullptr);
    vkb::destroy_debug_utils_messenger(_instance, _debug_messenger);
    vkDestroyInstance(_instance, nullptr);
    Logger::info("DeviceManager::cleanup()");
}
