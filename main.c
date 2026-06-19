#define _POSIX_C_SOURCE 199309L
#include <time.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <errno.h>

#include <unistd.h>
#include <sys/stat.h>

#define GLFW_INCLUDE_VULKAN        // REQUIRED only for GLFW CreateWindowSurface.
#include <GLFW/glfw3.h>

#include <vulkan/vulkan.h>
#include <vulkan/vk_enum_string_helper.h>

#include "common.h"

typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;

typedef int8_t  s8;
typedef int16_t s16;
typedef int32_t s32;
typedef int64_t s64;



struct Vulkan
{
    VkInstance instance;
    VkSurfaceKHR surface;

    u64 physical_dev_id;
    VkPhysicalDevice physical_dev;
    u64 physical_dev_graphics_queue_idx;

    VkDevice logical_dev;
    VkQueue dev_queue;

    VkShaderModule shader_module;

    VkSwapchainKHR swap_chain;
    u64 swap_chain_image_count;
    VkExtent2D swap_chain_image_extent;
    VkImage swap_chain_images[16];
    VkImageView swap_chain_image_views[16];

    VkPipeline graphics_pipeline;
    VkPipelineLayout pipeline_layout;

    VkCommandPool command_pool;

    VkCommandBuffer command_buffer[16];

    VkSemaphore next_image_semaphore[16];
    VkSemaphore render_semaphore[16];
    VkFence frame_fence[16];
};


#undef STRINGIZE
#undef STRINGIZE2
#undef LINE_STRING
#define STRINGIZE(x) STRINGIZE2(x)
#define STRINGIZE2(x) #x
#define LINE_STRING STRINGIZE(__LINE__)
#define LOG(FMT, ...) log_fn((__FILE__ ":" LINE_STRING "  " FMT "\n"), ##__VA_ARGS__)
#define LOG_ERROR(FMT, ...) log_error_fn(__FILE__ ":" LINE_STRING " ERROR  " FMT "\n", ##__VA_ARGS__)

/////////////////////////////////////////////////////////////////////////////
FILE* log_file = NULL;
/////////////////////////////////////////////////////////////////////////////

void log_fn(const char* msg, ...)
{
    va_list args;
    va_start(args, msg);
    vfprintf(stdout, msg, args);
    va_end(args);
    fflush(stdout);
}

void log_error_fn(const char* msg, ...)
{
    va_list args;
    va_start(args, msg);
    vfprintf(stderr, msg, args);
    va_end(args);
    fflush(stderr);
}


static s64 start_timer_impl()
{
    s64 now = 0;
    struct timespec ts = {0};
    if(clock_gettime(CLOCK_MONOTONIC, &ts) == 0)
    {
        now = ts.tv_sec * 1000000000L + ts.tv_nsec;
    }
    return now;
}

static s64 end_timer_impl(const s64 t)
{
    s64 now = 0;
    struct timespec ts = {0};
    if(clock_gettime(CLOCK_MONOTONIC, &ts) == 0)
    {
        now = ts.tv_sec * 1000000000L + ts.tv_nsec;
    }
    return now - t;
}

#define START_TIMER() start_timer_impl()
#define END_TIMER_NS(T) end_timer_impl(T)
#define END_TIMER_US(T) (end_timer_impl(T) / 1000L)
#define END_TIMER_MS(T) (end_timer_impl(T) / 1000000L)
#define END_TIMER_S(T) (end_timer_impl(T) / 1000000000L)

static inline u64 clamp_u64(const u64 v, const u64 lo, const u64 hi)
{
    return min_u64(max_u64(v, lo), hi);
}

int main()
{
    // GLFW window.
    GLFWwindow* window = NULL;
    u32 num_glfw_req_exts = 0;
    const char** glfw_req_exts = NULL;
    {
        const s64 timer = START_TIMER();

        if(!glfwInit())
        {
            LOG_ERROR("'glfwInit' failed.");
            return 1;
        }
        glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
        // glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);

        const u64 window_width = 1920;
        const u64 window_height = 1080;
        window = glfwCreateWindow((s32)window_width, (s32)window_height, "Vulkan", NULL, NULL);
        if(!window)
        {
            LOG_ERROR("'glfwCreateWindow' failed.");
            return 1;
        }

        glfw_req_exts = glfwGetRequiredInstanceExtensions(&num_glfw_req_exts);
        if(!glfw_req_exts)
        {
            const char* desc;
            const s32 code = glfwGetError(&desc);
            LOG_ERROR("'glfwGetRequiredInstanceExtensions' failed %i %s.", code, desc);
            return 1;
        }

        LOG("PROFILE glfw: %lius", END_TIMER_US(timer));
    }

    // GLFW required extensions.
    {
        const s64 timer = START_TIMER();

        VkExtensionProperties found_exts[256];
        u32 num_found_exts = ARRAY_COUNT(found_exts);
        const VkResult res = vkEnumerateInstanceExtensionProperties(
            NULL,
            &num_found_exts,
            found_exts);
        if(res != VK_SUCCESS && res != VK_INCOMPLETE)
        {
            LOG_ERROR("'vkEnumerateInstanceExtensionProperties' failed %s.", string_VkResult(res));
            return 1;
        }

        u64 num_match = 0;
        for(u64 i_req = 0; i_req < num_glfw_req_exts; i_req++)
        {
            for(u64 i_found = 0; i_found < num_found_exts; i_found++)
            {
                num_match += strcmp(found_exts[i_found].extensionName, glfw_req_exts[i_req]) == 0;
            }
        }
        
        if(num_match != num_glfw_req_exts)
        {
            LOG_ERROR("Did not find all required glfw vulkan extensions.");
            return 1;
        }

        LOG("PROFILE Match glfw extensions: %lius", END_TIMER_US(timer));
    }

    // Vulkan instance layers.
    const char* vulkan_layers[] = {
        "VK_LAYER_KHRONOS_validation"
    };
    {
        const s64 timer = START_TIMER();

        VkLayerProperties found_layers[256];
        u32 num_found_layers = ARRAY_COUNT(found_layers);
        const VkResult res = vkEnumerateInstanceLayerProperties(
            &num_found_layers,
            found_layers);
        if(res != VK_SUCCESS && res != VK_INCOMPLETE)
        {
            LOG_ERROR("'vkEnumerateInstanceLayerProperties' failed %s.", string_VkResult(res));
            return 1;
        }

        u64 num_match = 0;
        for(u64 i_req = 0; i_req < ARRAY_COUNT(vulkan_layers); i_req++)
        {
            for(u64 i_found = 0; i_found < num_found_layers; i_found++)
            {
                num_match += strcmp(found_layers[i_found].layerName, vulkan_layers[i_req]) == 0;
            }
        }

        if(num_match != ARRAY_COUNT(vulkan_layers))
        {
            LOG_ERROR("Could not find all required validation layers. Did you forget to 'source vulkansdk/default/setup-env.sh'?");
            return 1;
        }

        for(u64 i = 0; i < ARRAY_COUNT(vulkan_layers); i++)
        {
            LOG("Enabled vulkan layer: %s", vulkan_layers[i]);
        }

        LOG("PROFILE vulkan layers: %lius", END_TIMER_US(timer));
    }

    struct Vulkan vk;
    
    // Vulkan instance.
    {
        const s64 timer = START_TIMER();

        VkApplicationInfo appInfo = {0};
        appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
        appInfo.pApplicationName = "Hello Triangle";
        appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
        appInfo.pEngineName = "No Engine";
        appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
        appInfo.apiVersion = VK_API_VERSION_1_3;
    
        VkInstanceCreateInfo createInfo = {0};
        createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
        createInfo.pApplicationInfo = &appInfo;
        createInfo.enabledExtensionCount = num_glfw_req_exts;
        createInfo.ppEnabledExtensionNames = glfw_req_exts;
        createInfo.enabledLayerCount = ARRAY_COUNT(vulkan_layers);
        createInfo.ppEnabledLayerNames = vulkan_layers;
    
        const VkResult res = vkCreateInstance(&createInfo, NULL, &vk.instance);
        if(res != VK_SUCCESS)
        {
            LOG_ERROR("'vkCreateInstance' failed %s.", string_VkResult(res));
            return 1;
        }

        LOG("PROFILE create vulkan instance: %lius", END_TIMER_US(timer));
    }

    // GLFW + vulkan surface.
    {
        // TODO(mfritz): Replace allocator.
        const VkResult res = glfwCreateWindowSurface(vk.instance, window, NULL, &vk.surface);
        if(res != VK_SUCCESS)
        {
            LOG_ERROR("'glfwCreateWindowSurface' failed %s.", string_VkResult(res));
            return 1;
        }
    }

    // Vulkan physical device.
    const char* dev_req_exts[] = {
        "VK_KHR_swapchain",
    };
    {
        const s64 timer = START_TIMER();

        VkPhysicalDevice devs[8];
        u32 num_devs = ARRAY_COUNT(devs);

        const VkResult res = vkEnumeratePhysicalDevices(
            vk.instance,
            &num_devs,
            devs);
        if(res != VK_SUCCESS && res != VK_INCOMPLETE)
        {
            LOG_ERROR("'vkEnumeratePhysicalDevices' failed %s.", string_VkResult(res));
            return 1;
        }

        LOG("Devices:");

        vk.physical_dev_id = u64_MAX;
        vk.physical_dev_graphics_queue_idx = u64_MAX;

        for(u64 i = 0; i < num_devs; i++)
        {
            // Get the device.
            VkPhysicalDevice dev = devs[i];
            VkPhysicalDeviceProperties prop;
            vkGetPhysicalDeviceProperties(dev, &prop);
            LOG("    %lu : %s (VK_API_VERSION %u.%u)", i, prop.deviceName, VK_API_VERSION_MAJOR(prop.apiVersion), VK_API_VERSION_MINOR(prop.apiVersion));

            // Get and check supported queue family properites.
            u64 maybe_graphics_queue_idx = u64_MAX;
            {
                VkQueueFamilyProperties dev_queue_props[16];
                u32 num_dev_queues = ARRAY_COUNT(dev_queue_props);
                vkGetPhysicalDeviceQueueFamilyProperties(
                    dev,
                    &num_dev_queues,
                    dev_queue_props);
                for(u64 i = 0; i < num_dev_queues; i++)
                {
                    VkBool32 presentation_supported = 0;
                    const VkResult res = vkGetPhysicalDeviceSurfaceSupportKHR(
                        dev,
                        i,
                        vk.surface,
                        &presentation_supported);
                    if(res == VK_SUCCESS &&
                       dev_queue_props[i].queueFlags & VK_QUEUE_GRAPHICS_BIT &&
                       presentation_supported)
                    {
                        maybe_graphics_queue_idx = i;
                    }
                }
            }

            // Get and check all physical device extensions.
            u64 supports_required_extensions = 0;
            {
                VkExtensionProperties found_exts[256];
                u32 num_found_exts = ARRAY_COUNT(found_exts);
                const VkResult res = vkEnumerateDeviceExtensionProperties(
                    dev,
                    NULL,
                    &num_found_exts,
                    found_exts);
                if(res != VK_SUCCESS && res != VK_INCOMPLETE)
                {
                    LOG_ERROR("'vkEnumerateInstanceExtensionProperties' failed on device %lu, %s.", vk.physical_dev_id, string_VkResult(res));
                    return 1;
                }
    
                u64 num_match = 0;
                for(u64 i_req = 0; i_req < ARRAY_COUNT(dev_req_exts); i_req++)
                {
                    for(u64 i_found = 0; i_found < num_found_exts; i_found++)
                    {
                        num_match += strcmp(found_exts[i_found].extensionName, dev_req_exts[i_req]) == 0;
                    }
                }
                
                if(num_match == ARRAY_COUNT(dev_req_exts))
                {
                    supports_required_extensions = 1;
                }
            }

            // Get and check all physical device features.
            u64 supports_required_features = 0;
            {
                VkPhysicalDeviceExtendedDynamicStateFeaturesEXT features_ext = {0};
                features_ext.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTENDED_DYNAMIC_STATE_FEATURES_EXT;
                features_ext.pNext = NULL;
                VkPhysicalDeviceVulkan13Features features_1_3 = {0};
                features_1_3.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
                features_1_3.pNext = &features_ext;
                VkPhysicalDeviceFeatures2 features_2 = {0};
                features_2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
                features_2.pNext = &features_1_3;
                vkGetPhysicalDeviceFeatures2(dev, &features_2);
                supports_required_features = features_1_3.dynamicRendering && features_ext.extendedDynamicState;
            }

            if(vk.physical_dev_id == u64_MAX &&
               prop.apiVersion >= VK_API_VERSION_1_3 &&
               maybe_graphics_queue_idx != u64_MAX &&
               supports_required_extensions &&
               supports_required_features)
            {
                vk.physical_dev = dev;
                vk.physical_dev_id = i;
                vk.physical_dev_graphics_queue_idx = maybe_graphics_queue_idx;
            }
        }

        if(vk.physical_dev_id < u64_MAX)
        {
            LOG("Using dev %lu", vk.physical_dev_id);
        }
        else
        {
            LOG_ERROR("Could not find suitable physical device.");
            return 1;

        }

        LOG("PROFILE vulkan physical device: %lius", END_TIMER_US(timer));
    }

    // Device graphics queue and logical device.
    {
        const float queue_priority = 0.5f;
        VkDeviceQueueCreateInfo dev_queue_create_info =
        {
            .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
            .pNext = NULL,
            .flags = 0,
            .queueFamilyIndex = vk.physical_dev_graphics_queue_idx,
            .queueCount = 1,
            .pQueuePriorities = &queue_priority
        };

        VkPhysicalDeviceExtendedDynamicStateFeaturesEXT features_ext = {0};
        features_ext.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTENDED_DYNAMIC_STATE_FEATURES_EXT;
        features_ext.pNext = NULL;
        features_ext.extendedDynamicState = 1;
        VkPhysicalDeviceVulkan13Features features_1_3 = {0};
        features_1_3.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
        features_1_3.pNext = &features_ext;
        features_1_3.dynamicRendering = 1;
        features_1_3.synchronization2 = 1;
        VkPhysicalDeviceVulkan11Features features_1_1 = {0};
        features_1_1.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES;
        features_1_1.pNext = &features_1_3;
        features_1_1.shaderDrawParameters = 1;
        VkPhysicalDeviceFeatures2 features_2 = {0};
        features_2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
        features_2.pNext = &features_1_1;

        VkDeviceCreateInfo dev_create_info = {
            .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
            .pNext = &features_2,
            .flags = 0,
            .queueCreateInfoCount = 1,
            .pQueueCreateInfos = &dev_queue_create_info,
            .enabledLayerCount = 0,
            .ppEnabledLayerNames = NULL,
            .enabledExtensionCount = ARRAY_COUNT(dev_req_exts),
            .ppEnabledExtensionNames = dev_req_exts,
            .pEnabledFeatures = NULL
        };

        const VkResult res = vkCreateDevice(
            vk.physical_dev,
            &dev_create_info,
            // TODO(mfritz): Replace allocator
            NULL,
            &vk.logical_dev
        );
        if(res != VK_SUCCESS)
        {
            LOG_ERROR("'vkCreateDevice' failed %s.", string_VkResult(res));
            return 1;
        }

        vkGetDeviceQueue(vk.logical_dev, vk.physical_dev_graphics_queue_idx, 0, &vk.dev_queue);
    }

    // Swap chain.
    const VkFormat surface_format = VK_FORMAT_B8G8R8A8_SRGB;
    {
        VkSurfaceCapabilitiesKHR surface_capabilities = {0};
        {
            const VkResult res = vkGetPhysicalDeviceSurfaceCapabilitiesKHR(
                vk.physical_dev,
                vk.surface,
                &surface_capabilities);
            if(res != VK_SUCCESS)
            {
                LOG_ERROR("'vkGetPhysicalDeviceSurfaceCapabilitiesKHR' failed %s.", string_VkResult(res));
                return 1;
            }
        }

        const VkColorSpaceKHR color_space = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;

        u8 format_supported = 0;
        {
            VkSurfaceFormatKHR surface_formats[64];
            u32 surface_format_count = ARRAY_COUNT(surface_formats);
            const VkResult res = vkGetPhysicalDeviceSurfaceFormatsKHR(
                vk.physical_dev,
                vk.surface,
                &surface_format_count,
                surface_formats);
            if(res != VK_SUCCESS && res != VK_INCOMPLETE)
            {
                LOG_ERROR("'vkGetPhysicalDeviceSurfaceFormatsKHR' failed %s.", string_VkResult(res));
                return 1;
            }

            for(u64 i = 0; i < surface_format_count; i++)
            {
                format_supported |= surface_formats[i].format == surface_format && surface_formats[i].colorSpace == color_space;
            }

            if(!format_supported)
            {
                LOG_ERROR("Format %u, color space %u not supported.", surface_format, color_space);
                return 1;
            }
        }

        const VkPresentModeKHR present_mode = VK_PRESENT_MODE_FIFO_KHR;

        u8 presentation_mode_supported = 0;
        {
            VkPresentModeKHR present_modes[16];
            u32 present_mode_count = ARRAY_COUNT(present_modes);
            const VkResult res = vkGetPhysicalDeviceSurfacePresentModesKHR(
                vk.physical_dev,
                vk.surface,
                &present_mode_count,
                present_modes);
            if(res != VK_SUCCESS && res != VK_INCOMPLETE)
            {
                LOG_ERROR("'vkGetPhysicalDeviceSurfacePresentModesKHR' failed %s.", string_VkResult(res));
                return 1;
            }

            for(u64 i = 0; i < present_mode_count; i++)
            {
                presentation_mode_supported |= present_modes[i] == present_mode;
            }
            
            if(!presentation_mode_supported)
            {
                LOG_ERROR("Presentation mode %u not supported.", present_mode);
            }
        }

        s32 glfw_width, glfw_height;
        glfwGetFramebufferSize(window, &glfw_width, &glfw_height);

        vk.swap_chain_image_extent.width = clamp_u64(glfw_width, surface_capabilities.minImageExtent.width, surface_capabilities.maxImageExtent.width);
        vk.swap_chain_image_extent.height = clamp_u64(glfw_height, surface_capabilities.minImageExtent.height, surface_capabilities.maxImageExtent.height);
        const u64 image_count = min_u64(surface_capabilities.minImageCount + 1, surface_capabilities.maxImageCount);

        const VkSwapchainCreateInfoKHR swap_chain_create_info = {
            .sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
            .pNext = NULL,
            .flags = 0,
            .surface = vk.surface,
            .minImageCount = image_count,
            .imageFormat = surface_format,
            .imageColorSpace = color_space,
            .imageExtent = vk.swap_chain_image_extent,
            1,
            .imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
            .imageSharingMode = VK_SHARING_MODE_EXCLUSIVE,
            .queueFamilyIndexCount = 0,
            .pQueueFamilyIndices = NULL,
            .preTransform = surface_capabilities.currentTransform,
            .compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
            .presentMode = present_mode,
            .clipped = VK_TRUE,
            .oldSwapchain = VK_NULL_HANDLE
        };

        {
            // TODO(mfritz): Replace allocator.
            const VkResult res = vkCreateSwapchainKHR(
                vk.logical_dev,
                &swap_chain_create_info,
                NULL,
                &vk.swap_chain);
            if(res != VK_SUCCESS)
            {
                LOG_ERROR("'vkCreateSwapchainKHR' failed %s.", string_VkResult(res));
                return 1;
            }
        }

        u32 swap_chain_image_count = ARRAY_COUNT(vk.swap_chain_images);
        {
            const VkResult res = vkGetSwapchainImagesKHR(
                vk.logical_dev,
                vk.swap_chain,
                &swap_chain_image_count,
                vk.swap_chain_images);
            // VK_INCOMPLETE is also considered an error here. Increase max vk.swap_chain_images if needed.
            if(res != VK_SUCCESS)
            {
                LOG_ERROR("'vkGetSwapchainImagesKHR' failed %s.", string_VkResult(res));
                return 1;
            }
        }
        
        for(u64 i_view = 0; i_view < swap_chain_image_count; i_view++)
        {
            const VkImageViewCreateInfo image_view_create_info = {
                .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
                .pNext = NULL,
                .flags = 0,
                .image = vk.swap_chain_images[i_view],
                .viewType = VK_IMAGE_VIEW_TYPE_2D,
                .format = surface_format,
                .components = {
                    .r = VK_COMPONENT_SWIZZLE_IDENTITY,
                    .g = VK_COMPONENT_SWIZZLE_IDENTITY,
                    .b = VK_COMPONENT_SWIZZLE_IDENTITY,
                    .a = VK_COMPONENT_SWIZZLE_IDENTITY,
                },
                .subresourceRange = {
                    .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                    .baseMipLevel = 0,
                    .levelCount = 1,
                    .baseArrayLayer = 0,
                    .layerCount = 1,
                }
            };
            // TODO(mfritz): Replace allocator.
            const VkResult res = vkCreateImageView(
                vk.logical_dev,
                &image_view_create_info,
                NULL,
                &vk.swap_chain_image_views[i_view]);
            if(res != VK_SUCCESS)
            {
                LOG_ERROR("'vkCreateImageView' failed %s.", string_VkResult(res));
                return 1;
            }
            vk.swap_chain_image_count = swap_chain_image_count;
        }
    }

    // Shaders.
    VkPipelineShaderStageCreateInfo shader_stages[2] = {0};
    {
        const char* shader_path = "slang.spv";
        FILE* file = fopen(shader_path, "rb");
        if(!file)
        {
            LOG_ERROR("Could not open file \"%s\", errno=%i", shader_path, errno);
            return 1;
        }

        struct stat st;
        if(stat(shader_path, &st) != 0)
        {
            LOG_ERROR("Could not stat file \"%s\", errno=%i", shader_path, errno);
            fclose(file);
            return 1;
        }
        const u64 file_size = (u64)st.st_size;

        _Alignas(32) u8 file_buf[65536];
        const u64 fread_res = fread(file_buf, 1, min_u64(file_size, sizeof(file_buf)), file);
        if(fread_res < file_size)
        {
            LOG_ERROR("Could not read all of file \"%s\" %lu / %lu", shader_path, fread_res, file_size);
            fclose(file);
            return 1;
        }

        fclose(file);

        if((file_size == 0) || (file_size % 4 != 0))
        {
            LOG_ERROR("Shader code size (%lu) must be > 0 and a multiple of 4.", file_size);
            return 1;
        }

        const VkShaderModuleCreateInfo shader_module_create_info = {
            .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
            .pNext = NULL,
            .flags = 0,
            .codeSize = file_size,
            .pCode = (u32*)file_buf
        };

        // TODO(mfritz): Replace allocator.
        const VkResult res = vkCreateShaderModule(
            vk.logical_dev,
            &shader_module_create_info,
            NULL,
            &vk.shader_module);
        if(res != VK_SUCCESS)
        {
            LOG_ERROR("'vkCreateShaderModule' failed %s", string_VkResult(res));
            return 1;
        }

        const VkPipelineShaderStageCreateInfo vert_shader_create_info = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .pNext = NULL,
            .flags = 0,
            .stage = VK_SHADER_STAGE_VERTEX_BIT,
            .module = vk.shader_module,
            .pName = "vert_main",
            .pSpecializationInfo = NULL,
        };
        const VkPipelineShaderStageCreateInfo frag_shader_create_info = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .pNext = NULL,
            .flags = 0,
            .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
            .module = vk.shader_module,
            .pName = "frag_main",
            .pSpecializationInfo = NULL,
        };

        _Static_assert(ARRAY_COUNT(shader_stages) == 2, "Update");
        shader_stages[0] = vert_shader_create_info;
        shader_stages[1] = frag_shader_create_info;
    }

    // Pipeline.
    // Static viewport and scissors.
    const VkViewport viewport = {
        .x = 0.0f,
        .y = 0.0f,
        .width = (float)vk.swap_chain_image_extent.width,
        .height = (float)vk.swap_chain_image_extent.height,
        .minDepth = 0.0f,
        .maxDepth = 1.0f,
    };
    const VkRect2D scissor = {
        .offset = {
            .x = 0,
            .y = 0,
        },
        .extent = vk.swap_chain_image_extent,
    };
    {
        const VkPipelineVertexInputStateCreateInfo vertex_input_state_create_info = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
            .pNext = NULL,
            .flags = 0,
            .vertexBindingDescriptionCount = 0,
            .pVertexBindingDescriptions = NULL,
            .vertexAttributeDescriptionCount = 0,
            .pVertexAttributeDescriptions = NULL,
        };

        const VkPipelineInputAssemblyStateCreateInfo input_assembly_state_create_info = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
            .pNext = NULL,
            .flags = 0,
            .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
            .primitiveRestartEnable = VK_FALSE,
        };

        const VkPipelineViewportStateCreateInfo viewport_state_create_info = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
            .pNext = NULL,
            .flags = 0,
            .viewportCount = 1,
            .pViewports = &viewport,
            .scissorCount = 1,
            .pScissors = &scissor,
        };

        const VkPipelineRasterizationStateCreateInfo rasterization_state_create_info = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
            .pNext = NULL,
            .flags = 0,
            .depthClampEnable = VK_FALSE,
            .rasterizerDiscardEnable = VK_FALSE,
            .polygonMode = VK_POLYGON_MODE_FILL,
            // TODO(mfritz)
            // .cullMode = VK_CULL_MODE_BACK_BIT,
            .cullMode = VK_CULL_MODE_NONE,
            .frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE,
            .depthBiasEnable = VK_FALSE,
            .depthBiasConstantFactor = 0.0f,
            .depthBiasClamp = 0.0f,
            .depthBiasSlopeFactor = 0.0f,
            .lineWidth = 1.0f,
        };

        const VkPipelineMultisampleStateCreateInfo multisample_state_create_info = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
            .pNext = NULL,
            .flags = 0,
            .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT,
            .sampleShadingEnable = VK_FALSE,
            .minSampleShading = 0.0f,
            .pSampleMask = NULL,
            .alphaToCoverageEnable = VK_FALSE,
            .alphaToOneEnable = VK_FALSE,
        };

        const VkPipelineColorBlendAttachmentState color_blend_attachment_state = {
            .blendEnable = VK_FALSE,
            // .srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA,
            // .dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
            // .colorBlendOp = VK_BLEND_OP_ADD,
            // .srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE,
            // .dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO,
            // .alphaBlendOp = VK_BLEND_OP_ADD,
            .colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT,
        };

        const VkPipelineColorBlendStateCreateInfo color_blend_state_create_info = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
            .pNext = NULL,
            .flags = 0,
            .logicOpEnable = VK_FALSE,
            .logicOp = VK_LOGIC_OP_COPY,
            .attachmentCount = 1,
            .pAttachments = &color_blend_attachment_state,
            .blendConstants = {0},
        };

        const VkDynamicState dynamic_states[2] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
        const VkPipelineDynamicStateCreateInfo dynamic_state_create_info = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
            .pNext = NULL,
            .flags = 0,
            .dynamicStateCount = ARRAY_COUNT(dynamic_states),
            .pDynamicStates = dynamic_states,
        };

        {
            const VkPipelineLayoutCreateInfo layout_create_info = {
                .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
                .pNext = NULL,
                .flags = 0,
                .setLayoutCount = 0,
                .pSetLayouts = NULL,
                .pushConstantRangeCount = 0,
                .pPushConstantRanges = NULL,
            };
            // TODO(mfritz): Replace allocator.
            const VkResult res = vkCreatePipelineLayout(
                vk.logical_dev,
                &layout_create_info,
                NULL,
                &vk.pipeline_layout);
            if(res != VK_SUCCESS)
            {
                LOG_ERROR("'vkCreatePipelineLayout' failed %s", string_VkResult(res));
                return 1;
            }
        }

        const VkPipelineRenderingCreateInfo pipeline_rendering_create_info = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO,
            .pNext = NULL,
            .viewMask = 0,
            .colorAttachmentCount = 1,
            .pColorAttachmentFormats = &surface_format,
            .depthAttachmentFormat = VK_FORMAT_UNDEFINED,
            .stencilAttachmentFormat = VK_FORMAT_UNDEFINED,
        };

        const VkGraphicsPipelineCreateInfo graphics_pipeline_create_info = {
            .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
            .pNext = &pipeline_rendering_create_info,
            .flags = 0,
            .stageCount = 2,
            .pStages = shader_stages,
            .pVertexInputState = &vertex_input_state_create_info,
            .pInputAssemblyState = &input_assembly_state_create_info,
            .pTessellationState = NULL,
            .pViewportState = &viewport_state_create_info,
            .pRasterizationState = &rasterization_state_create_info,
            .pMultisampleState = &multisample_state_create_info,
            .pDepthStencilState = NULL,
            .pColorBlendState = &color_blend_state_create_info,
            .pDynamicState = &dynamic_state_create_info,
            .layout = vk.pipeline_layout,
            .renderPass = NULL,
            .subpass = 0,
            .basePipelineHandle = NULL,
            .basePipelineIndex = 0,
        };

        // TODO(mfritz): Replace allocator.
        const VkResult res = vkCreateGraphicsPipelines(
            vk.logical_dev,
            VK_NULL_HANDLE,
            1,
            &graphics_pipeline_create_info,
            NULL,
            &vk.graphics_pipeline);
        if(res != VK_SUCCESS)
        {
            LOG_ERROR("'vkCreateGraphicsPipelines' failed %s.", string_VkResult(res));
            return 1;
        }
    }

    // Command pool.
    {
        const VkCommandPoolCreateInfo pool_info = {
            .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
            .pNext = NULL,
            .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
            .queueFamilyIndex = vk.physical_dev_graphics_queue_idx,
        };
        // TODO(mfritz): Replace allocator.
        const VkResult res = vkCreateCommandPool(
            vk.logical_dev,
            &pool_info,
            NULL,
            &vk.command_pool);
        if(res != VK_SUCCESS)
        {
            LOG_ERROR("'vkCreateCommandPool' failed %s.", string_VkResult(res));
            return 1;
        }
    }

    // Command buffer.
    {
        const VkCommandBufferAllocateInfo command_buffer_allocate_info = {
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
            .pNext = NULL,
            .commandPool = vk.command_pool,
            .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
            .commandBufferCount = vk.swap_chain_image_count,
        };
        const VkResult res = vkAllocateCommandBuffers(
            vk.logical_dev,
            &command_buffer_allocate_info,
            vk.command_buffer);
        if(res != VK_SUCCESS)
        {
            LOG_ERROR("'vkAllocateCommandBuffers' failed %s.", string_VkResult(res));
            return 1;
        }
    }
    
    for(u64 i_swap_chain = 0; i_swap_chain < vk.swap_chain_image_count; i_swap_chain++)
    {

        // Next image semaphore.
        {
            const VkSemaphoreCreateInfo semaphore_create_info = {
                .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
                .pNext = NULL,
                .flags = 0,
            };
            // TODO(mfritz): Replace allocator;
            const VkResult res = vkCreateSemaphore(
                vk.logical_dev,
                &semaphore_create_info,
                NULL,
                &vk.next_image_semaphore[i_swap_chain]);
            if(res != VK_SUCCESS)
            {
                LOG_ERROR("'vkCreateSemaphore' failed %s.", string_VkResult(res));
                return 1;
            }
        }
        
        // Render sempahore.
        {
            const VkSemaphoreCreateInfo semaphore_create_info = {
                .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
                .pNext = NULL,
                .flags = 0,
            };
            // TODO(mfritz): Replace allocator;
            const VkResult res = vkCreateSemaphore(
                vk.logical_dev,
                &semaphore_create_info,
                NULL,
                &vk.render_semaphore[i_swap_chain]);
            if(res != VK_SUCCESS)
            {
                LOG_ERROR("'vkCreateSemaphore' failed %s.", string_VkResult(res));
                return 1;
            }
        }
    
        // Frame fence.
        {
            const VkFenceCreateInfo fence_create_info = {
                .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
                .pNext = NULL,
                .flags = VK_FENCE_CREATE_SIGNALED_BIT,
            };
            // TODO(mfritz): Replace allocator;
            const VkResult res = vkCreateFence(
                vk.logical_dev,
                &fence_create_info,
                NULL,
                &vk.frame_fence[i_swap_chain]);
            if(res != VK_SUCCESS)
            {
                LOG_ERROR("'vkCreateFence' failed %s.", string_VkResult(res));
                return 1;
            }
        }
    }

    u64 frame = 0;
    for(;; frame++)
    {
        glfwPollEvents();

        if(glfwWindowShouldClose(window))
        {
            break;
        }

        const u64 swap_chain_idx = frame % vk.swap_chain_image_count;

        // Wait for the previous frame to finish.
        {
            const VkResult res = vkWaitForFences(
                vk.logical_dev,
                1,
                &vk.frame_fence[swap_chain_idx],
                1,
                u64_MAX);
            if(res != VK_SUCCESS)
            {
                LOG_ERROR("'vkWaitForFences' failed %s.", string_VkResult(res));
                return 1;
            }
        }
        {
            const VkResult res = vkResetFences(vk.logical_dev, 1, &vk.frame_fence[swap_chain_idx]);
            if(res != VK_SUCCESS)
            {
                LOG_ERROR("'vkResetFences' failed %s.", string_VkResult(res));
                return 1;
            }
        }

        // Acquire next swap chain image.
        u32 image_index = u32_MAX;
        {
            const VkResult res = vkAcquireNextImageKHR(
                vk.logical_dev,
                vk.swap_chain,
                u64_MAX,
                vk.next_image_semaphore[swap_chain_idx],
                NULL,
                &image_index);
            if(res != VK_SUCCESS)
            {
                LOG_ERROR("'vkAcquireNextImageKHR' failed %s.", string_VkResult(res));
                return 1;
            }
        }

        // Begin command recording.
        const VkCommandBuffer cmd_buf = vk.command_buffer[swap_chain_idx];
        {
            const VkCommandBufferBeginInfo command_buffer_begin_info = {
                .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
                .pNext = NULL,
                .flags = 0,
                .pInheritanceInfo = NULL,
            };
            const VkResult res = vkBeginCommandBuffer(
                cmd_buf,
                &command_buffer_begin_info);
            if(res != VK_SUCCESS)
            {
                LOG_ERROR("'vkBeginCommandBuffer' failed %s.", string_VkResult(res));
                return 1;
            }
        }

        // Transition swap chain image to color.
        {
            const VkImageMemoryBarrier2 image_memory_barrier = {
                .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
                .pNext = NULL,
                .srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                .srcAccessMask = VK_ACCESS_2_NONE,
                .dstStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                .dstAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
                .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
                .newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .image = vk.swap_chain_images[image_index],
                .subresourceRange = {
                    .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                    .baseMipLevel = 0,
                    .levelCount = 1,
                    .baseArrayLayer = 0,
                    .layerCount = 1,
                }
            };
            const VkDependencyInfo dependency_info = {
                .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
                .pNext = NULL,
                .dependencyFlags = 0,
                .memoryBarrierCount = 0,
                .pMemoryBarriers = NULL,
                .bufferMemoryBarrierCount = 0,
                .pBufferMemoryBarriers = NULL,
                .imageMemoryBarrierCount = 1,
                .pImageMemoryBarriers = &image_memory_barrier,
            };
            vkCmdPipelineBarrier2(cmd_buf, &dependency_info);
        }

        // Begin rendering.
        {
            const VkClearValue clear_value = {
                .color = {
                    .float32[0] = 0.004f,
                    .float32[1] = 0.0f,
                    .float32[2] = 0.01f,
                    .float32[3] = 1.0f,
                }
            };
            const VkRenderingAttachmentInfo color_rendering_attachment_info = {
                .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
                .pNext = NULL,
                .imageView = vk.swap_chain_image_views[image_index],
                .imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                .resolveMode = VK_RESOLVE_MODE_NONE,
                .resolveImageView = NULL,
                .resolveImageLayout = VK_IMAGE_LAYOUT_UNDEFINED,
                .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
                .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
                .clearValue = clear_value,
            };
            const VkRenderingInfo rendering_info = {
                .sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
                .pNext = NULL,
                .flags = 0,
                .renderArea = {
                    .offset = {
                        .x = 0,
                        .y = 0,
                    },
                    .extent = vk.swap_chain_image_extent,
                },
                .layerCount = 1,
                .viewMask = 0,
                .colorAttachmentCount = 1,
                .pColorAttachments = &color_rendering_attachment_info,
                .pDepthAttachment = VK_NULL_HANDLE,
                .pStencilAttachment = VK_NULL_HANDLE,
            };
            vkCmdBeginRendering(cmd_buf, &rendering_info);
        }

        vkCmdBindPipeline(cmd_buf, VK_PIPELINE_BIND_POINT_GRAPHICS, vk.graphics_pipeline);
        vkCmdSetViewport(cmd_buf, 0, 1, &viewport);
        vkCmdSetScissor(cmd_buf, 0, 1, &scissor);
        vkCmdDraw(cmd_buf, 3, 1, 0, 0);
        vkCmdEndRendering(cmd_buf);

        // Transition swap chain image to present.
        {
            const VkImageMemoryBarrier2 image_memory_barrier = {
                .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
                .pNext = NULL,
                .srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                .srcAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
                // TODO(mfritz): Why is this "VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT" instead of "VK_PIPELINE_STAGE_2_NONE"?
                .dstStageMask = VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT,
                // TODO(mfritz): Why is this not "VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT"? Is it because there are no other operations in the pipeline?
                .dstAccessMask = VK_ACCESS_2_NONE,
                .oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                .newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
                .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .image = vk.swap_chain_images[image_index],
                .subresourceRange = {
                    .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                    .baseMipLevel = 0,
                    .levelCount = 1,
                    .baseArrayLayer = 0,
                    .layerCount = 1,
                }
            };
            const VkDependencyInfo dependency_info = {
                .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
                .pNext = NULL,
                .dependencyFlags = 0,
                .memoryBarrierCount = 0,
                .pMemoryBarriers = NULL,
                .bufferMemoryBarrierCount = 0,
                .pBufferMemoryBarriers = NULL,
                .imageMemoryBarrierCount = 1,
                .pImageMemoryBarriers = &image_memory_barrier,
            };
            vkCmdPipelineBarrier2(cmd_buf, &dependency_info);
        }

        vkEndCommandBuffer(cmd_buf);

        // Submit.
        {
            // TODO(mfritz): Read about subpass dependencies. Apparently this is incorrect.

            // const VkSemaphoreSubmitInfo present_semaphore_submit_info = {
            //     .sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
            //     .pNext = NULL,
            //     .semaphore = vk.next_image_semaphore,
            //     .value = 0,
            //     .stageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
            //     .deviceIndex = 0,
            // };
            // const VkCommandBufferSubmitInfo command_buffer_submit_info = {
            //         .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO,
            //         .pNext = NULL,
            //         .commandBuffer = vk.command_buffer,
            //         .deviceMask = 0,
            // };
            // const VkSemaphoreSubmitInfo render_semaphore_submit_info = {
            //     .sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
            //     .pNext = NULL,
            //     .semaphore = vk.render_semaphore,
            //     .value = 0,
            //     .stageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
            //     .deviceIndex = 0,
            // };
            // const VkSubmitInfo2 submit_info = {
            //     .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2,
            //     .pNext = NULL,
            //     .flags = 0,
            //     .waitSemaphoreInfoCount = 1,
            //     .pWaitSemaphoreInfos = &present_semaphore_submit_info,
            //     .commandBufferInfoCount = 1,
            //     .pCommandBufferInfos = &command_buffer_submit_info,
            //     .signalSemaphoreInfoCount = 1,
            //     .pSignalSemaphoreInfos = &render_semaphore_submit_info,
            // };
            // const VkResult res = vkQueueSubmit2(vk.dev_queue, 1, &submit_info, vk.frame_fence); 
            // if(res != VK_SUCCESS)
            // {
            //     LOG_ERROR("'vkQueueSubmit2' failed %s.", string_VkResult(res));
            //     return 1;
            // }

            const VkPipelineStageFlags wait_dst_stage_mask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
            const VkSubmitInfo submit_info = {
                .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
                .pNext = NULL,
                .waitSemaphoreCount = 1,
                .pWaitSemaphores = &vk.next_image_semaphore[swap_chain_idx],
                .pWaitDstStageMask = &wait_dst_stage_mask,
                .commandBufferCount = 1,
                .pCommandBuffers = &cmd_buf,
                .signalSemaphoreCount = 1,
                .pSignalSemaphores = &vk.render_semaphore[swap_chain_idx],
            };
            const VkResult res = vkQueueSubmit(vk.dev_queue, 1, &submit_info, vk.frame_fence[swap_chain_idx]);
            if(res != VK_SUCCESS)
            {
                LOG_ERROR("'vkQueueSubmit' failed %s.", string_VkResult(res));
                return 1;
            }
        }

        // Present.
        {
            const VkPresentInfoKHR present_info = {
                .sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
                .pNext = NULL,
                // No need to sync on previous frame presents:
                // https://docs.vulkan.org/refpages/latest/refpages/source/vkQueuePresentKHR.html
                // "However, presentation requests sent to a particular queue are always performed in order."
                .waitSemaphoreCount = 1,
                .pWaitSemaphores = &vk.render_semaphore[swap_chain_idx],
                .swapchainCount = 1,
                .pSwapchains = &vk.swap_chain,
                .pImageIndices = &image_index,
                .pResults = NULL,
            };
            const VkResult res = vkQueuePresentKHR(vk.dev_queue, &present_info);
            if(res != VK_SUCCESS)
            {
                LOG_ERROR("'vkQueuePresentKHR' failed %s.", string_VkResult(res));
                return 1;
            }
        }
    }

    // Wait for device idle.
    {
        const VkResult res = vkDeviceWaitIdle(vk.logical_dev);
        if(res != VK_SUCCESS)
        {
            LOG_ERROR("'vkDeviceWaitIdle' failed %s.", string_VkResult(res));
            return 1;
        }
    }

    // TODO(mfritz): Replace allocator FOR ALL.
    {
        for(u64 i = 0; i < vk.swap_chain_image_count; i++)
        {
            vkDestroyFence(vk.logical_dev, vk.frame_fence[i], NULL);
            vkDestroySemaphore(vk.logical_dev, vk.render_semaphore[i], NULL);
            vkDestroySemaphore(vk.logical_dev, vk.next_image_semaphore[i], NULL);
            vkDestroyImageView(vk.logical_dev, vk.swap_chain_image_views[i], NULL);
        }
        vkDestroyCommandPool(vk.logical_dev, vk.command_pool, NULL);
        vkDestroyPipeline(vk.logical_dev, vk.graphics_pipeline, NULL);
        vkDestroyShaderModule(vk.logical_dev, vk.shader_module, NULL);
        vkDestroyPipelineLayout(vk.logical_dev, vk.pipeline_layout, NULL);
        vkDestroySwapchainKHR(vk.logical_dev, vk.swap_chain, NULL);
        vkDestroyDevice(vk.logical_dev, NULL);
        vkDestroySurfaceKHR(vk.instance, vk.surface, NULL);
        vkDestroyInstance(vk.instance, NULL);
    }

    glfwDestroyWindow(window);
    glfwTerminate();

    return 0;
}
