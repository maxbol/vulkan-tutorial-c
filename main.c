#include "vk_video/vulkan_video_codec_av1std.h"
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
#include <unistd.h>

#include "arrays.h"
#include "set.h/set.h"

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
  VkFramebuffer *items;
  uint32_t count;
  uint32_t capacity;
} swapchain_frame_buffers_da_t;

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
  VkPipelineLayout pipeline_layout;
  VkRenderPass render_pass;
  VkPipeline graphics_pipeline;
  swapchain_frame_buffers_da_t swapchain_frame_buffers;
  VkCommandPool command_pool;
  VkCommandBuffer command_buffer;
  VkSemaphore image_available_semaphore;
  VkSemaphore render_finished_semaphore;
  VkFence in_flight_fence;
} app_t;

typedef struct {
  const char **items;
  size_t count;
  size_t capacity;
} const_strings_da_t;

typedef struct {
  char *items;
  size_t count;
  size_t capacity;
} const_string_da_t;

typedef struct {
  uint32_t *items;
  size_t count;
  size_t capacity;
} uint32_da_t;

typedef set_type(uint32_t) set_uint32_t;
typedef optional(uint32_t) optional_uint32_t;

const_string_da_t read_file(const char *filename) {
  if (access(filename, F_OK) == -1) {
    error("Can't access file %s", filename);
  }

  FILE *file = fopen(filename, "r");
  fseek(file, 0L, SEEK_END);
  size_t file_size = ftell(file);

  rewind(file);

  const_string_da_t data = {0};
  da_capacity(data, file_size);
  unsigned int bytes_read = fread(data.items, 1, file_size, file);
  data.count = bytes_read;

  return data;
}

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
  app->swapchain_image_views.count = app->swapchain_images.count;

  for (size_t i = 0; i < app->swapchain_images.count; i++) {
    printf("Creating image view %d\n", i);
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

/***************
 * Render passes
 ***************/

void create_render_pass(app_t *app) {
  VkAttachmentDescription color_attachment = {0};
  color_attachment.format = app->swapchain_image_format;
  color_attachment.samples = VK_SAMPLE_COUNT_1_BIT;
  color_attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
  color_attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
  color_attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
  color_attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
  color_attachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  color_attachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

  VkAttachmentReference color_attachment_ref = {0};
  color_attachment_ref.attachment = 0;
  color_attachment_ref.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

  VkSubpassDescription subpass = {0};
  subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
  subpass.colorAttachmentCount = 1;
  subpass.pColorAttachments = &color_attachment_ref;

  VkSubpassDependency dependency = {0};
  dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
  dependency.dstSubpass = 0;
  dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
  dependency.srcAccessMask = 0;
  dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
  dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

  VkRenderPassCreateInfo render_pass_info = {0};
  render_pass_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
  render_pass_info.attachmentCount = 1;
  render_pass_info.pAttachments = &color_attachment;
  render_pass_info.subpassCount = 1;
  render_pass_info.pSubpasses = &subpass;
  render_pass_info.dependencyCount = 1;
  render_pass_info.pDependencies = &dependency;

  if (vkCreateRenderPass(app->device, &render_pass_info, NULL,
                         &app->render_pass) != VK_SUCCESS) {
    error("failed to create render pass!");
  }
}

/*******************
 * Graphics pipeline
 *******************/

typedef struct {
  VkDynamicState *items;
  uint32_t count;
  uint32_t capacity;
} dynamic_states_da_t;

VkShaderModule create_shader_module(app_t *app, const_string_da_t code) {
  VkShaderModuleCreateInfo create_info = {0};
  create_info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
  create_info.codeSize = code.count;
  create_info.pCode = (uint32_t *)code.items;

  VkShaderModule shader_module;
  if (vkCreateShaderModule(app->device, &create_info, NULL, &shader_module) !=
      VK_SUCCESS) {
    error("failed to create shader module!");
  }

  return shader_module;
}

void create_graphics_pipeline(app_t *app) {
  const_string_da_t vert_shader_code = read_file("shaders/vert.spv");
  const_string_da_t frag_shader_code = read_file("shaders/frag.spv");

  VkShaderModule vert_shader_module =
      create_shader_module(app, vert_shader_code);

  VkShaderModule frag_shader_module =
      create_shader_module(app, frag_shader_code);

  VkPipelineShaderStageCreateInfo vert_shader_stage_info = {0};
  vert_shader_stage_info.sType =
      VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  vert_shader_stage_info.stage = VK_SHADER_STAGE_VERTEX_BIT;
  vert_shader_stage_info.module = vert_shader_module;
  vert_shader_stage_info.pName = "main";

  VkPipelineShaderStageCreateInfo frag_shader_stage_info = {0};
  frag_shader_stage_info.sType =
      VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  frag_shader_stage_info.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
  frag_shader_stage_info.module = frag_shader_module;
  frag_shader_stage_info.pName = "main";

  VkPipelineShaderStageCreateInfo shader_stages[] = {vert_shader_stage_info,
                                                     frag_shader_stage_info};

  dynamic_states_da_t dynamic_states = {0};
  da_append(dynamic_states, VK_DYNAMIC_STATE_VIEWPORT);
  da_append(dynamic_states, VK_DYNAMIC_STATE_SCISSOR);

  VkPipelineDynamicStateCreateInfo dynamic_state = {0};
  dynamic_state.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
  dynamic_state.dynamicStateCount = dynamic_states.count;
  dynamic_state.pDynamicStates = dynamic_states.items;

  VkPipelineVertexInputStateCreateInfo vertex_input_info = {0};
  vertex_input_info.sType =
      VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
  vertex_input_info.vertexBindingDescriptionCount = 0;
  vertex_input_info.pVertexBindingDescriptions = NULL;
  vertex_input_info.vertexAttributeDescriptionCount = 0;
  vertex_input_info.pVertexAttributeDescriptions = NULL;

  VkPipelineInputAssemblyStateCreateInfo input_assembly = {0};
  input_assembly.sType =
      VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
  input_assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
  input_assembly.primitiveRestartEnable = VK_FALSE;

  VkViewport viewport = {0};
  viewport.x = 0.0f;
  viewport.y = 0.0f;
  viewport.width = (float)app->swapchain_extent.width;
  viewport.height = (float)app->swapchain_extent.height;
  viewport.minDepth = 0.0f;
  viewport.maxDepth = 1.0f;

  VkRect2D scissor = {0};
  scissor.offset = (VkOffset2D){0, 0};
  scissor.extent = app->swapchain_extent;

  VkPipelineViewportStateCreateInfo viewport_state = {0};
  viewport_state.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
  viewport_state.viewportCount = 1;
  viewport_state.scissorCount = 1;

  VkPipelineRasterizationStateCreateInfo rasterizer = {0};
  rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
  rasterizer.depthClampEnable = VK_FALSE;
  rasterizer.rasterizerDiscardEnable = VK_FALSE;
  rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
  rasterizer.lineWidth = 1.0f;
  rasterizer.cullMode = VK_CULL_MODE_BACK_BIT;
  rasterizer.frontFace = VK_FRONT_FACE_CLOCKWISE;
  rasterizer.depthBiasEnable = VK_FALSE;
  rasterizer.depthBiasConstantFactor = 0.0f;
  rasterizer.depthBiasClamp = 0.0f;
  rasterizer.depthBiasSlopeFactor = 0.0f;

  VkPipelineMultisampleStateCreateInfo multisampling = {0};
  multisampling.sType =
      VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
  multisampling.sampleShadingEnable = VK_FALSE;
  multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
  multisampling.minSampleShading = 1.0f;
  multisampling.pSampleMask = NULL;
  multisampling.alphaToCoverageEnable = VK_FALSE;
  multisampling.alphaToOneEnable = VK_FALSE;

  VkPipelineColorBlendAttachmentState color_blend_attachment = {0};
  color_blend_attachment.colorWriteMask =
      VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
      VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
  color_blend_attachment.blendEnable = VK_TRUE;
  color_blend_attachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
  color_blend_attachment.dstColorBlendFactor =
      VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
  color_blend_attachment.colorBlendOp = VK_BLEND_OP_ADD;
  color_blend_attachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
  color_blend_attachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
  color_blend_attachment.alphaBlendOp = VK_BLEND_OP_ADD;

  VkPipelineColorBlendStateCreateInfo color_blending = {0};
  color_blending.sType =
      VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
  color_blending.logicOpEnable = VK_FALSE;
  color_blending.logicOp = VK_LOGIC_OP_COPY;
  color_blending.attachmentCount = 1;
  color_blending.pAttachments = &color_blend_attachment;
  color_blending.blendConstants[0] = 0.0f;
  color_blending.blendConstants[1] = 0.0f;
  color_blending.blendConstants[2] = 0.0f;
  color_blending.blendConstants[3] = 0.0f;

  VkPipelineLayoutCreateInfo pipeline_layout_info = {0};
  pipeline_layout_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
  pipeline_layout_info.setLayoutCount = 0;
  pipeline_layout_info.pSetLayouts = NULL;
  pipeline_layout_info.pushConstantRangeCount = 0;
  pipeline_layout_info.pPushConstantRanges = NULL;

  if (vkCreatePipelineLayout(app->device, &pipeline_layout_info, NULL,
                             &app->pipeline_layout) != VK_SUCCESS) {
    error("failed to create pipeline layout");
  }

  VkGraphicsPipelineCreateInfo pipeline_info = {0};
  pipeline_info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
  pipeline_info.stageCount = 2;
  pipeline_info.pStages = shader_stages;

  pipeline_info.pVertexInputState = &vertex_input_info;
  pipeline_info.pInputAssemblyState = &input_assembly;
  pipeline_info.pViewportState = &viewport_state;
  pipeline_info.pRasterizationState = &rasterizer;
  pipeline_info.pMultisampleState = &multisampling;
  pipeline_info.pDepthStencilState = NULL;
  pipeline_info.pColorBlendState = &color_blending;
  pipeline_info.pDynamicState = &dynamic_state;
  pipeline_info.layout = app->pipeline_layout;
  pipeline_info.renderPass = app->render_pass;
  pipeline_info.subpass = 0;

  pipeline_info.basePipelineHandle = VK_NULL_HANDLE;
  pipeline_info.basePipelineIndex = -1;

  if (vkCreateGraphicsPipelines(app->device, VK_NULL_HANDLE, 1, &pipeline_info,
                                NULL, &app->graphics_pipeline) != VK_SUCCESS) {
    error("failed to create graphics pipeline!");
  }

  vkDestroyShaderModule(app->device, vert_shader_module, NULL);
  vkDestroyShaderModule(app->device, frag_shader_module, NULL);
}

/**************
 * Framebuffers
 **************/

void create_frame_buffers(app_t *app) {
  da_capacity(app->swapchain_frame_buffers, app->swapchain_image_views.count);
  app->swapchain_frame_buffers.count = app->swapchain_image_views.count;

  for (size_t i = 0; i < app->swapchain_image_views.count; i++) {
    VkImageView attachments[] = {app->swapchain_image_views.items[i]};

    VkFramebufferCreateInfo framebuffer_info = {0};
    framebuffer_info.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    framebuffer_info.renderPass = app->render_pass;
    framebuffer_info.attachmentCount = 1;
    framebuffer_info.pAttachments = attachments;
    framebuffer_info.width = app->swapchain_extent.width;
    framebuffer_info.height = app->swapchain_extent.height;
    framebuffer_info.layers = 1;

    if (vkCreateFramebuffer(app->device, &framebuffer_info, NULL,
                            &app->swapchain_frame_buffers.items[i]) !=
        VK_SUCCESS) {
      error("failed to create framebuffer!");
    }
  }
}

/***********************
 * Command pool & buffer
 ***********************/

void create_command_pool(app_t *app) {
  queue_family_indices_t queue_family_indices =
      find_queue_families(app, app->physical_device);

  VkCommandPoolCreateInfo pool_info = {0};
  pool_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
  pool_info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
  pool_info.queueFamilyIndex = queue_family_indices.graphics_family.value;

  if (vkCreateCommandPool(app->device, &pool_info, NULL, &app->command_pool) !=
      VK_SUCCESS) {
    error("failed to create command pool!");
  }
}

void create_command_buffer(app_t *app) {
  VkCommandBufferAllocateInfo alloc_info = {0};
  alloc_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
  alloc_info.commandPool = app->command_pool;
  alloc_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
  alloc_info.commandBufferCount = 1;

  if (vkAllocateCommandBuffers(app->device, &alloc_info,
                               &app->command_buffer) != VK_SUCCESS) {
    error("failed to allocate command buffers!");
  }
}

void record_command_buffer(app_t *app, VkCommandBuffer command_buffer,
                           uint32_t image_index) {
  VkCommandBufferBeginInfo begin_info = {0};
  begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
  begin_info.flags = 0;
  begin_info.pInheritanceInfo = NULL;

  if (vkBeginCommandBuffer(command_buffer, &begin_info) != VK_SUCCESS) {
    error("failed to begin recording command buffer!");
  }

  VkRenderPassBeginInfo render_pass_info = {0};
  render_pass_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
  render_pass_info.renderPass = app->render_pass;
  render_pass_info.framebuffer =
      app->swapchain_frame_buffers.items[image_index];
  render_pass_info.renderArea.offset = (VkOffset2D){0, 0};
  render_pass_info.renderArea.extent = app->swapchain_extent;

  VkClearValue clear_color = {{{0.0f, 0.0f, 0.0f, 1.0f}}};
  render_pass_info.clearValueCount = 1;
  render_pass_info.pClearValues = &clear_color;

  vkCmdBeginRenderPass(command_buffer, &render_pass_info,
                       VK_SUBPASS_CONTENTS_INLINE);
  vkCmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                    app->graphics_pipeline);

  VkViewport viewport = {0};
  viewport.x = 0.0f;
  viewport.y = 1.0f;
  viewport.width = (float)app->swapchain_extent.width;
  viewport.height = (float)app->swapchain_extent.height;
  viewport.minDepth = 0.0f;
  viewport.maxDepth = 1.0f;
  vkCmdSetViewport(command_buffer, 0, 1, &viewport);

  VkRect2D scissor = {0};
  scissor.offset = (VkOffset2D){0, 0};
  scissor.extent = app->swapchain_extent;
  vkCmdSetScissor(command_buffer, 0, 1, &scissor);

  vkCmdDraw(command_buffer, 3, 1, 0, 0);

  vkCmdEndRenderPass(command_buffer);

  if (vkEndCommandBuffer(command_buffer) != VK_SUCCESS) {
    error("failed to record command buffer!");
  }
}

/**************
 * Sync objects
 **************/

void create_sync_objects(app_t *app) {
  VkSemaphoreCreateInfo semaphore_info = {0};
  semaphore_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

  VkFenceCreateInfo fence_info = {0};
  fence_info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
  fence_info.flags = VK_FENCE_CREATE_SIGNALED_BIT;

  if (vkCreateSemaphore(app->device, &semaphore_info, NULL,
                        &app->image_available_semaphore) != VK_SUCCESS ||
      vkCreateSemaphore(app->device, &semaphore_info, NULL,
                        &app->render_finished_semaphore) != VK_SUCCESS ||
      vkCreateFence(app->device, &fence_info, NULL, &app->in_flight_fence) !=
          VK_SUCCESS) {
    error("failed to create semaphores!");
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

uint64_t set_uint32_hash_fn(uint32_t value) { return value; }
bool set_uint32_equals_fn(uint32_t a, uint32_t b) { return a == b; }

void create_logical_device(app_t *app) {
  queue_family_indices_t indices =
      find_queue_families(app, app->physical_device);

  assert(indices_complete(indices));

  device_queue_create_infos_da_t queue_create_infos = {0};

  set_uint32_t unique_queue_families = {0};
  set_init(unique_queue_families, set_uint32_hash_fn, set_uint32_equals_fn);

  set_add(unique_queue_families, indices.graphics_family.value);
  set_add(unique_queue_families, indices.present_family.value);

  size_t unique_queue_families_count = set_size(unique_queue_families);

  da_capacity(queue_create_infos, unique_queue_families_count);
  float queue_priority = 1.0f;

  tree_addr_t node_addr = unique_queue_families.root;
  while (tree_is_valid_addr(node_addr)) {
    uint32_t queue_family_index =
        set_get_entry(unique_queue_families, node_addr);

    VkDeviceQueueCreateInfo queue_create_info = {0};
    queue_create_info.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    queue_create_info.queueFamilyIndex = queue_family_index;
    queue_create_info.queueCount = 1;
    queue_create_info.pQueuePriorities = &queue_priority;
    da_append(queue_create_infos, queue_create_info);

    node_addr = tree_next(unique_queue_families, node_addr);
  }

  set_free(unique_queue_families);

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
  create_render_pass(app);
  create_graphics_pipeline(app);
  create_frame_buffers(app);
  create_command_pool(app);
  create_command_buffer(app);
  create_sync_objects(app);
}

void draw_frame(app_t *app) {
  vkWaitForFences(app->device, 1, &app->in_flight_fence, VK_TRUE, UINT64_MAX);
  vkResetFences(app->device, 1, &app->in_flight_fence);

  uint32_t image_index;
  vkAcquireNextImageKHR(app->device, app->swapchain, UINT64_MAX,
                        app->image_available_semaphore, VK_NULL_HANDLE,
                        &image_index);

  vkResetCommandBuffer(app->command_buffer, 0);
  record_command_buffer(app, app->command_buffer, image_index);

  VkSubmitInfo submit_info = {0};
  submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;

  VkSemaphore wait_semaphores[] = {app->image_available_semaphore};

  VkPipelineStageFlags wait_stages[] = {
      VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};

  submit_info.waitSemaphoreCount = 1;
  submit_info.pWaitSemaphores = wait_semaphores;
  submit_info.pWaitDstStageMask = wait_stages;

  submit_info.commandBufferCount = 1;
  submit_info.pCommandBuffers = &app->command_buffer;

  VkSemaphore signal_sempahores[] = {app->render_finished_semaphore};
  submit_info.signalSemaphoreCount = 1;
  submit_info.pSignalSemaphores = signal_sempahores;

  if (vkQueueSubmit(app->graphics_queue, 1, &submit_info,
                    app->in_flight_fence) != VK_SUCCESS) {
    error("failed to submit draw command buffer");
  }

  VkPresentInfoKHR present_info = {0};
  present_info.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
  present_info.waitSemaphoreCount = 1;
  present_info.pWaitSemaphores = signal_sempahores;

  VkSwapchainKHR swap_chains[] = {app->swapchain};
  present_info.swapchainCount = 1;
  present_info.pSwapchains = swap_chains;
  present_info.pImageIndices = &image_index;
  present_info.pResults = NULL;

  vkQueuePresentKHR(app->present_queue, &present_info);
}

void main_loop(app_t *app) {
  while (!glfwWindowShouldClose(app->window)) {
    glfwPollEvents();
    draw_frame(app);
  }

  vkDeviceWaitIdle(app->device);
}

void cleanup(app_t *app) {
  vkDestroySemaphore(app->device, app->image_available_semaphore, NULL);
  vkDestroySemaphore(app->device, app->render_finished_semaphore, NULL);
  vkDestroyFence(app->device, app->in_flight_fence, NULL);

  vkDestroyCommandPool(app->device, app->command_pool, NULL);

  for (size_t i = 0; i < app->swapchain_frame_buffers.count; i++) {
    printf("Destroying framebuffer %d\n", i);
    vkDestroyFramebuffer(app->device, app->swapchain_frame_buffers.items[i],
                         NULL);
  }

  vkDestroyPipeline(app->device, app->graphics_pipeline, NULL);
  vkDestroyPipelineLayout(app->device, app->pipeline_layout, NULL);
  vkDestroyRenderPass(app->device, app->render_pass, NULL);

  for (uint32_t i = 0; i < app->swapchain_image_views.count; i++) {
    printf("Destroying image view %d\n", i);
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
