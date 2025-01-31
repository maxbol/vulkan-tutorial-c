#include "vulkan/vulkan_core.h"
#define GLFW_INCLUDE_VULKAN

#include <GLFW/glfw3.h>
#include <assert.h>
#include <float.h>
#include <limits.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "arrays.h"

#define DEBUG true
#define MAX_LAYER_COUNT 20

#define optional(type)                                                         \
  struct {                                                                     \
    bool present;                                                              \
    type value;                                                                \
  }

const uint32_t HEIGHT = 600;
const uint32_t WIDTH = 800;

const char *validation_layers[] = {"VK_LAYER_KHRONOS_validation"};
const char *device_extensions[] = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};
#ifdef DEBUG
bool enable_validation_layers = true;
#else
bool enable_validation_layers = false;
#endif

#define error(...)                                                             \
  {                                                                            \
    char out[4096];                                                            \
    int offset = snprintf(out, 4096, "ERROR: %s:%d: ", __FILE__, __LINE__);    \
    snprintf(out + offset, 4096 - offset, __VA_ARGS__);                        \
    fprintf(stderr, "%s\n", out);                                              \
    exit(EXIT_FAILURE);                                                        \
  }

#define clamp(val, floor, ceil) val > ceil ? ceil : (val < floor ? floor : val)

typedef struct {
  VkImage *items;
  uint32_t count;
  uint32_t capacity;
} swapchain_images_da_t;

typedef struct {
  VkImageView *items;
  uint32_t count;
  uint32_t capacity;
} swapchain_image_views_da_t;

typedef struct {
  GLFWwindow *window;
  VkInstance instance;
  VkDebugUtilsMessengerEXT debug_messenger;
  VkPhysicalDevice physical_device;
  VkDevice device;
  VkQueue graphics_queue;
  VkQueue present_queue;
  VkSurfaceKHR surface;
  VkSwapchainKHR swapchain;
  swapchain_images_da_t swapchain_images;
  VkFormat swapchain_image_format;
  VkExtent2D swapchain_extent;
  swapchain_image_views_da_t swapchain_image_views;
} app_t;

typedef struct {
  const char **items;
  size_t count;
  size_t capacity;
} const_strings_da_t;

typedef struct {
  uint32_t *items;
  size_t count;
  size_t capacity;
} uint32_da_t;

typedef optional(uint32_t) optional_uint32_t;

/************
 * Validation
 ************/

VKAPI_ATTR VkBool32 VKAPI_CALL
debug_callback(VkDebugUtilsMessageSeverityFlagBitsEXT message_severity,
               VkDebugUtilsMessageTypeFlagsEXT message_type,
               const VkDebugUtilsMessengerCallbackDataEXT *callback_data,
               void *user_data) {
  printf("validation layer: %s\n", callback_data->pMessage);
  return VK_FALSE;
}

bool check_validation_layer_support(const char **validation_layers,
                                    uint32_t validation_layers_len,
                                    char *missing_out, size_t missing_out_len) {
  uint32_t layer_count;
  vkEnumerateInstanceLayerProperties(&layer_count, NULL);

  VkLayerProperties available_layers[MAX_LAYER_COUNT];
  vkEnumerateInstanceLayerProperties(&layer_count, available_layers);

  for (uint32_t i = 0; i < validation_layers_len; i++) {
    bool layer_found = false;

    for (uint32_t j = 0; j < layer_count; j++) {
      if (strcmp(available_layers[j].layerName, validation_layers[i]) == 0) {
        layer_found = true;
        break;
      }
    }

    if (!layer_found) {
      strncpy(missing_out, validation_layers[i], missing_out_len);
      return false;
    }
  }

  return true;
}

VkResult create_debug_utils_messenger_ext(
    VkInstance instance, const VkDebugUtilsMessengerCreateInfoEXT *create_info,
    const VkAllocationCallbacks *allocator,
    VkDebugUtilsMessengerEXT *debug_messenger) {
  PFN_vkCreateDebugUtilsMessengerEXT func =
      (PFN_vkCreateDebugUtilsMessengerEXT)vkGetInstanceProcAddr(
          instance, "vkCreateDebugUtilsMessengerEXT");
  if (func != NULL) {
    return func(instance, create_info, allocator, debug_messenger);
  } else {
    return VK_ERROR_EXTENSION_NOT_PRESENT;
  }
}

void destroy_debug_utils_messenger_ext(VkInstance instance,
                                       VkDebugUtilsMessengerEXT debug_messenger,
                                       const VkAllocationCallbacks *allocator) {
  PFN_vkDestroyDebugUtilsMessengerEXT func =
      (PFN_vkDestroyDebugUtilsMessengerEXT)vkGetInstanceProcAddr(
          instance, "vkDestroyDebugUtilsMessengerEXT");
  if (func != NULL) {
    func(instance, debug_messenger, allocator);
  }
}

void populate_debug_messenger_create_info(
    VkDebugUtilsMessengerCreateInfoEXT *create_info) {
  *create_info = (VkDebugUtilsMessengerCreateInfoEXT){0};

  create_info->sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
  create_info->messageSeverity =
      VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT |
      VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
      /*VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT |*/
      VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
  create_info->messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                             VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT |
                             VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT;
  create_info->pfnUserCallback = debug_callback;
  create_info->pUserData = NULL;
}

void setup_debug_messenger(app_t *app) {
  if (!enable_validation_layers) {
    return;
  }

  VkDebugUtilsMessengerCreateInfoEXT create_info;
  populate_debug_messenger_create_info(&create_info);

  if (create_debug_utils_messenger_ext(app->instance, &create_info, NULL,
                                       &app->debug_messenger) != VK_SUCCESS) {
    error("failed to set up debug messenger!\n");
  }
}

/************
 * Extensions
 ************/

typedef struct {
  VkExtensionProperties *items;
  uint32_t count;
  uint32_t capacity;
} extension_properties_da_t;

const_strings_da_t get_required_instance_extensions() {
  const_strings_da_t required_extensions = {0};

  uint32_t glfw_required_extension_count = 0;
  const char **glfw_extensions =
      glfwGetRequiredInstanceExtensions(&glfw_required_extension_count);

  for (uint32_t i = 0; i < glfw_required_extension_count; i++) {
    da_append(required_extensions, glfw_extensions[i]);
  }

  da_append(required_extensions, VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME);
  da_append(required_extensions,
            VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME);
  da_append(required_extensions, VK_KHR_SURFACE_EXTENSION_NAME);
  da_append(required_extensions, VK_EXT_DEBUG_UTILS_EXTENSION_NAME);

  if (enable_validation_layers) {
    da_append(required_extensions, VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
  }

  return required_extensions;
}

extension_properties_da_t get_available_instance_extensions() {
  extension_properties_da_t available_extensions = {0};

  vkEnumerateInstanceExtensionProperties(NULL, &available_extensions.count,
                                         NULL);

  da_capacity(available_extensions, available_extensions.count);

  vkEnumerateInstanceExtensionProperties(NULL, &available_extensions.count,
                                         available_extensions.items);

  return available_extensions;
}

/****************
 * Queue families
 ****************/

typedef struct {
  optional_uint32_t graphics_family;
  optional_uint32_t present_family;
} queue_family_indices_t;

typedef struct {
  VkQueueFamilyProperties *items;
  uint32_t count;
  uint32_t capacity;
} queue_family_properties_da_t;

bool indices_complete(queue_family_indices_t indices) {
  return indices.graphics_family.present && indices.present_family.present;
}

queue_family_indices_t find_queue_families(app_t *app,
                                           VkPhysicalDevice device) {
  queue_family_indices_t indices = {0};
  queue_family_properties_da_t queue_families = {0};

  vkGetPhysicalDeviceQueueFamilyProperties(device, &queue_families.count, NULL);
  da_capacity(queue_families, queue_families.count);
  vkGetPhysicalDeviceQueueFamilyProperties(device, &queue_families.count,
                                           queue_families.items);

  for (uint32_t i = 0; i < queue_families.count; i++) {
    if (queue_families.items[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
      indices.graphics_family =
          (optional_uint32_t){.present = true, .value = i};
    }

    VkBool32 present_support = false;
    vkGetPhysicalDeviceSurfaceSupportKHR(device, i, app->surface,
                                         &present_support);
    if (present_support) {
      indices.present_family = (optional_uint32_t){.present = true, .value = i};
    }

    if (indices_complete(indices)) {
      break;
    }
  }

  return indices;
}

/***********
 * Swapchain
 ***********/

typedef struct {
  VkSurfaceFormatKHR *items;
  uint32_t count;
  uint32_t capacity;
} surface_formats_da_t;

typedef struct {
  VkPresentModeKHR *items;
  uint32_t count;
  uint32_t capacity;
} present_modes_da_t;

typedef struct {
  VkSurfaceCapabilitiesKHR capabilities;
  surface_formats_da_t formats;
  present_modes_da_t present_modes;
} swapchain_support_details_t;

swapchain_support_details_t query_swap_chain_support(app_t *app,
                                                     VkPhysicalDevice device) {
  swapchain_support_details_t details = {0};

  vkGetPhysicalDeviceSurfaceCapabilitiesKHR(device, app->surface,
                                            &details.capabilities);

  vkGetPhysicalDeviceSurfaceFormatsKHR(device, app->surface,
                                       &details.formats.count, NULL);

  if (details.formats.count != 0) {
    da_capacity(details.formats, details.formats.count);
    vkGetPhysicalDeviceSurfaceFormatsKHR(
        device, app->surface, &details.formats.count, details.formats.items);
  }

  vkGetPhysicalDeviceSurfacePresentModesKHR(device, app->surface,
                                            &details.present_modes.count, NULL);

  if (details.present_modes.count != 0) {
    da_capacity(details.present_modes, details.present_modes.count);
    vkGetPhysicalDeviceSurfacePresentModesKHR(device, app->surface,
                                              &details.present_modes.count,
                                              details.present_modes.items);
  }

  return details;
}

VkSurfaceFormatKHR choose_swap_surface_format(surface_formats_da_t formats) {

  for (uint32_t i = 0; i < formats.count; i++) {
    VkSurfaceFormatKHR format = formats.items[i];
    if (format.format == VK_FORMAT_B8G8R8A8_SRGB &&
        format.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
      return format;
    }
  }

  return formats.items[0];
}

VkPresentModeKHR choose_swap_present_mode(present_modes_da_t present_modes) {
  for (uint32_t i = 0; i < present_modes.count; i++) {
    VkPresentModeKHR present_mode = present_modes.items[i];
    if (present_mode == VK_PRESENT_MODE_MAILBOX_KHR) {
      return present_mode;
    }
  }
  return VK_PRESENT_MODE_FIFO_KHR;
}

VkExtent2D choose_swap_extent(app_t *app,
                              const VkSurfaceCapabilitiesKHR capabilities) {
  if (capabilities.currentExtent.width != UINT_MAX) {
    return capabilities.currentExtent;
  }

  int width, height;
  glfwGetFramebufferSize(app->window, &width, &height);

  VkExtent2D actual_extent = {(uint32_t)width, (uint32_t)height};

  actual_extent.width =
      clamp(actual_extent.width, capabilities.minImageExtent.width,
            capabilities.maxImageExtent.width);
  actual_extent.height =
      clamp(actual_extent.height, capabilities.minImageExtent.height,
            capabilities.maxImageExtent.width);

  return actual_extent;
}

void create_swapchain(app_t *app) {
  swapchain_support_details_t swap_chain_support =
      query_swap_chain_support(app, app->physical_device);

  VkSurfaceFormatKHR surface_format =
      choose_swap_surface_format(swap_chain_support.formats);
  VkPresentModeKHR present_mode =
      choose_swap_present_mode(swap_chain_support.present_modes);
  VkExtent2D extent = choose_swap_extent(app, swap_chain_support.capabilities);

  uint32_t image_count = swap_chain_support.capabilities.minImageCount + 1;

  if (swap_chain_support.capabilities.maxImageCount > 0 &&
      image_count > swap_chain_support.capabilities.maxImageCount) {
    image_count = swap_chain_support.capabilities.maxImageCount;
  }

  VkSwapchainCreateInfoKHR create_info = {0};
  create_info.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
  create_info.surface = app->surface;
  create_info.minImageCount = image_count;
  create_info.imageFormat = surface_format.format;
  create_info.imageColorSpace = surface_format.colorSpace;
  create_info.imageExtent = extent;
  create_info.imageArrayLayers = 1;
  create_info.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

  queue_family_indices_t indices =
      find_queue_families(app, app->physical_device);

  uint32_t queue_family_indices[] = {indices.graphics_family.value,
                                     indices.present_family.value};

  if (indices.graphics_family.value != indices.present_family.value) {
    create_info.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
    create_info.queueFamilyIndexCount = 2;
    create_info.pQueueFamilyIndices = queue_family_indices;
  } else {
    create_info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    create_info.queueFamilyIndexCount = 0;
    create_info.pQueueFamilyIndices = NULL;
  }

  create_info.preTransform = swap_chain_support.capabilities.currentTransform;
  create_info.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
  create_info.presentMode = present_mode;
  create_info.clipped = VK_TRUE;
  create_info.oldSwapchain = VK_NULL_HANDLE;

  if (vkCreateSwapchainKHR(app->device, &create_info, NULL, &app->swapchain) !=
      VK_SUCCESS) {
    error("failed to create swap chain!");
  }

  vkGetSwapchainImagesKHR(app->device, app->swapchain, &image_count, NULL);
  da_capacity(app->swapchain_images, image_count); // NOLINT
  app->swapchain_images.count = image_count;
  vkGetSwapchainImagesKHR(app->device, app->swapchain, &image_count,
                          app->swapchain_images.items);

  app->swapchain_extent = extent;
  app->swapchain_image_format = surface_format.format;
}

/*************
 * Image views
 *************/

void create_image_views(app_t *app) {
  app->swapchain_image_views = (swapchain_image_views_da_t){0};
  da_capacity(app->swapchain_image_views, app->swapchain_images.count);

  for (size_t i = 0; i < app->swapchain_images.count; i++) {
    VkImageViewCreateInfo create_info = {0};
    create_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    create_info.image = app->swapchain_images.items[i];
    create_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
    create_info.format = app->swapchain_image_format;
    create_info.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
    create_info.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
    create_info.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
    create_info.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
    create_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    create_info.subresourceRange.baseMipLevel = 0;
    create_info.subresourceRange.levelCount = 1;
    create_info.subresourceRange.baseArrayLayer = 0;
    create_info.subresourceRange.layerCount = 1;

    if (vkCreateImageView(app->device, &create_info, NULL,
                          &app->swapchain_image_views.items[i]) != VK_SUCCESS) {
      error("failed to create image views!");
    }
  }
}

/******************
 * Physical devices
 ******************/

typedef struct {
  int score;
  VkPhysicalDevice device;
} physical_device_scored_t;

typedef struct {
  VkPhysicalDevice *items;
  uint32_t count;
  uint32_t capacity;
} physical_devices_da_t;

typedef struct {
  physical_device_scored_t *items;
  uint32_t count;
  uint32_t capacity;
} physical_devices_scored_da_t;

bool check_device_extension_support(VkPhysicalDevice device) {
  extension_properties_da_t available_extensions = {0};
  vkEnumerateDeviceExtensionProperties(device, NULL,
                                       &available_extensions.count, NULL);
  da_capacity(available_extensions, available_extensions.count);
  vkEnumerateDeviceExtensionProperties(
      device, NULL, &available_extensions.count, available_extensions.items);

  const_strings_da_t required_extensions = {0};

  for (uint32_t i = 0; i < sizeof(device_extensions) / sizeof(const char *);
       i++) {
    for (uint32_t j = 0; j < required_extensions.count; j++) {
      if (strcmp(required_extensions.items[j], device_extensions[i]) == 0) {
        da_remove_shuffle(required_extensions, j);
        break;
      }
    }
  }

  return required_extensions.count == 0;
}

int rate_device_suitability(app_t *app, VkPhysicalDevice device) {
  VkPhysicalDeviceProperties device_properties;
  VkPhysicalDeviceFeatures device_features;

  vkGetPhysicalDeviceProperties(device, &device_properties);
  vkGetPhysicalDeviceFeatures(device, &device_features);

  int score = 0;

  if (device_properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) {
    score += 1000;
  }

  score += device_properties.limits.maxImageDimension2D;

  if (device_features.geometryShader) {
    score += 100;
  }

  queue_family_indices_t indices = find_queue_families(app, device);

  if (!indices_complete(indices) || !check_device_extension_support(device)) {
    return 0;
  }

  swapchain_support_details_t swap_chain_support =
      query_swap_chain_support(app, device);

  if (swap_chain_support.formats.count == 0 ||
      swap_chain_support.present_modes.count == 0) {
    return 0;
  }

  return score;
}

int sort_scored_devices(physical_device_scored_t left,
                        physical_device_scored_t right) {
  return left.score - right.score;
}

void pick_physical_device(app_t *app) {
  physical_devices_da_t devices = {0};
  vkEnumeratePhysicalDevices(app->instance, &devices.count, NULL);

  if (devices.count == 0) {
    error("failed to find GPUs with Vulkan support!\n")
  }

  da_capacity(devices, devices.count); // NOLINT
  vkEnumeratePhysicalDevices(app->instance, &devices.count, devices.items);

  physical_devices_scored_da_t candidates = {0};
  da_capacity(candidates, devices.count);

  for (uint32_t i = 0; i < devices.count; i++) {
    VkPhysicalDevice device = devices.items[i];
    physical_device_scored_t candidate = {
        .score = rate_device_suitability(app, device),
        .device = device,
    };
    da_append(candidates, candidate);
  }

  da_quicksort(candidates, sort_scored_devices);

  if (candidates.items[0].score > 0) {
    app->physical_device = candidates.items[0].device;
  }

  if (app->physical_device == VK_NULL_HANDLE) {
    error("failed to find a suitable GPU!\n");
  }
}

/*****************
 * Logical devices
 *****************/

typedef struct {
  VkDeviceQueueCreateInfo *items;
  uint32_t capacity;
  uint32_t count;
} device_queue_create_infos_da_t;

void create_logical_device(app_t *app) {
  queue_family_indices_t indices =
      find_queue_families(app, app->physical_device);

  assert(indices_complete(indices));

  device_queue_create_infos_da_t queue_create_infos = {0};
  uint32_da_t unique_queue_families = {0};
  da_append(unique_queue_families, indices.graphics_family.value);

  if (indices.graphics_family.value != indices.present_family.value) {
    da_append(unique_queue_families, indices.present_family.value);
  }

  da_capacity(queue_create_infos, unique_queue_families.count);
  float queue_priority = 1.0f;

  for (uint32_t i = 0; i < unique_queue_families.count; i++) {
    VkDeviceQueueCreateInfo queue_create_info = {0};
    queue_create_info.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    queue_create_info.queueFamilyIndex = unique_queue_families.items[i];
    queue_create_info.queueCount = 1;
    queue_create_info.pQueuePriorities = &queue_priority;
    da_append(queue_create_infos, queue_create_info);
  }

  VkPhysicalDeviceFeatures device_features = {0};

  const_strings_da_t enabled_extensions = {0};
  da_append(enabled_extensions, "VK_KHR_portability_subset");

  for (uint32_t i = 0; i < sizeof(device_extensions) / sizeof(const char *);
       i++) {
    da_append(enabled_extensions, device_extensions[i]);
  }

  VkDeviceCreateInfo create_info = {0};
  create_info.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
  create_info.pQueueCreateInfos = queue_create_infos.items;
  create_info.queueCreateInfoCount = queue_create_infos.count;
  create_info.pEnabledFeatures = &device_features;
  create_info.enabledExtensionCount = enabled_extensions.count;
  create_info.ppEnabledExtensionNames = enabled_extensions.items;

  // Redundant in modern vulkan, defined for backwards-compatibility
  if (enable_validation_layers) {
    create_info.enabledLayerCount =
        sizeof(validation_layers) / sizeof(const char *);
    create_info.ppEnabledLayerNames = validation_layers;
  } else {
    create_info.enabledLayerCount = 0;
  }

  if (vkCreateDevice(app->physical_device, &create_info, NULL, &app->device) !=
      VK_SUCCESS) {
    error("failed to create logical device!\n");
  }

  vkGetDeviceQueue(app->device, indices.graphics_family.value, 0,
                   &app->graphics_queue);
  vkGetDeviceQueue(app->device, indices.present_family.value, 0,
                   &app->present_queue);
}

/**********
 * Instance
 **********/

void create_instance(app_t *app) {
  VkApplicationInfo app_info = {0};
  app_info.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
  app_info.pApplicationName = "Hello Triangle";
  app_info.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
  app_info.pEngineName = "No Engine";
  app_info.engineVersion = VK_MAKE_VERSION(1, 0, 0);
  app_info.apiVersion = VK_API_VERSION_1_0;

  const_strings_da_t required_extensions = get_required_instance_extensions();
  extension_properties_da_t available_extensions =
      get_available_instance_extensions();

  printf("Vulkan extensions support:\n");
  for (uint32_t i = 0; i < available_extensions.count; i++) {
    printf("  %s\n", available_extensions.items[i].extensionName);
  }

  char missing_layer[1024];
  if (enable_validation_layers &&
      !check_validation_layer_support(
          validation_layers, sizeof(validation_layers) / sizeof(const char *),
          missing_layer, 1024)) {
    error("validation layer %s requested, but not available!", missing_layer);
  }

  VkInstanceCreateInfo create_info = {0};
  create_info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
  create_info.pApplicationInfo = &app_info;
  create_info.enabledExtensionCount = (uint32_t)required_extensions.count;
  create_info.ppEnabledExtensionNames = required_extensions.items;
  create_info.flags |= VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;

  VkDebugUtilsMessengerCreateInfoEXT debug_create_info;
  if (enable_validation_layers) {
    create_info.enabledLayerCount =
        sizeof(validation_layers) / sizeof(const char *);
    create_info.ppEnabledLayerNames = validation_layers;

    populate_debug_messenger_create_info(&debug_create_info);
    create_info.pNext = &debug_create_info;
  } else {
    create_info.enabledLayerCount = 0;
    create_info.pNext = NULL;
  }

  VkResult result =
      vkCreateInstance(&create_info, NULL, &app->instance) != VK_SUCCESS;

  if (result != VK_SUCCESS) {
    error("failed to create vulkan instance!");
  }
}

/************
 * Surface
 ************/

void create_surface(app_t *app) {
  uint32_t result =
      glfwCreateWindowSurface(app->instance, app->window, NULL, &app->surface);
  if (result != VK_SUCCESS) {
    error("failed to create window surface with status %d\n", result);
  }
}

/************
 * Main hooks
 ************/

void init_window(app_t *app) {
  glfwInit();

  // Don't create an OpenGL context
  glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
  glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);

  app->window = glfwCreateWindow(WIDTH, HEIGHT, "Vulkan", NULL, NULL);
}

void init_vulkan(app_t *app) {
  create_instance(app);
  setup_debug_messenger(app);
  create_surface(app);
  pick_physical_device(app);
  create_logical_device(app);
  create_swapchain(app);
  create_image_views(app);
}

void main_loop(app_t *app) {
  while (!glfwWindowShouldClose(app->window)) {
    glfwPollEvents();
  }
}

void cleanup(app_t *app) {
  for (uint32_t i = 0; i < app->swapchain_image_views.count; i++) {
    vkDestroyImageView(app->device, app->swapchain_image_views.items[i], NULL);
  }

  vkDestroySwapchainKHR(app->device, app->swapchain, NULL);
  vkDestroyDevice(app->device, NULL);

  if (enable_validation_layers) {
    destroy_debug_utils_messenger_ext(app->instance, app->debug_messenger,
                                      NULL);
  }

  vkDestroySurfaceKHR(app->instance, app->surface, NULL);
  vkDestroyInstance(app->instance, NULL);
  glfwDestroyWindow(app->window);
  glfwTerminate();
}

void run(void) {
  app_t app = {.physical_device = VK_NULL_HANDLE};

  init_window(&app);
  init_vulkan(&app);
  main_loop(&app);
  cleanup(&app);
}

int main(void) {
  run();
  return EXIT_SUCCESS;
}
