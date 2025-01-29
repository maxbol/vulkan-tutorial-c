#include "vulkan/vulkan_core.h"
#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "arrays.h"

#define DEBUG true
#define MAX_LAYER_COUNT 20

const uint32_t HEIGHT = 600;
const uint32_t WIDTH = 800;

#define error(...)                                                             \
  {                                                                            \
    char out[4096];                                                            \
    int offset = snprintf(out, 4096, "ERROR: %s:%d: ", __FILE__, __LINE__);    \
    snprintf(out + offset, 4096 - offset, __VA_ARGS__);                        \
    fprintf(stderr, "%s\n", out);                                              \
    exit(EXIT_FAILURE);                                                        \
  }

typedef struct {
  GLFWwindow *window;
  VkInstance instance;
} app_t;

typedef struct {
  const char **items;
  size_t count;
  size_t capacity;
} extension_names_da_t;

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

void create_instance(app_t *app) {
  VkApplicationInfo app_info = {0};
  app_info.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
  app_info.pApplicationName = "Hello Triangle";
  app_info.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
  app_info.pEngineName = "No Engine";
  app_info.engineVersion = VK_MAKE_VERSION(1, 0, 0);
  app_info.apiVersion = VK_API_VERSION_1_0;

  uint32_t glfw_required_extension_count = 0;
  const char **glfw_extensions =
      glfwGetRequiredInstanceExtensions(&glfw_required_extension_count);

  extension_names_da_t required_extensions = {0};
  for (uint32_t i = 0; i < glfw_required_extension_count; i++) {
    da_append(required_extensions, glfw_extensions[i]);
  }
  da_append(required_extensions, VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME);

  // TODO(2025-01-29, Max Bolotin): Why is glfwAvailableExtensionsCount always
  // 0?
  uint32_t glfw_available_extensions_count = 0;
  vkEnumerateInstanceExtensionProperties(NULL, &glfw_available_extensions_count,
                                         NULL);

  VkExtensionProperties glfw_available_extensions[1024];
  vkEnumerateInstanceExtensionProperties(NULL, &glfw_available_extensions_count,
                                         glfw_available_extensions);

  printf("Vulkan extensions support:\n");
  for (uint32_t i = 0; i < glfw_available_extensions_count; i++) {
    printf("  %s\n", glfw_available_extensions[i].extensionName);
  }

#ifdef DEBUG
  bool enable_validation_layers = true;
#else
  bool enable_validation_layers = false;
#endif

  const char *validation_layers[] = {"VK_LAYER_KHRONOS_validation"};

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

  if (enable_validation_layers) {
    create_info.enabledLayerCount =
        sizeof(validation_layers) / sizeof(const char *);
    create_info.ppEnabledLayerNames = validation_layers;
  } else {
    create_info.enabledLayerCount = 0;
  }

  VkResult result =
      vkCreateInstance(&create_info, NULL, &app->instance) != VK_SUCCESS;

  if (result != VK_SUCCESS) {
    printf("result: %d\n", result);
    error("failed to create vulkan instance!");
  }
}

void init_window(app_t *app) {
  glfwInit();

  // Don't create an OpenGL context
  glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
  glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);

  app->window = glfwCreateWindow(WIDTH, HEIGHT, "Vulkan", NULL, NULL);
}

void init_vulkan(app_t *app) { create_instance(app); }

void main_loop(app_t *app) {
  while (!glfwWindowShouldClose(app->window)) {
    glfwPollEvents();
  }
}

void cleanup(app_t *app) {
  vkDestroyInstance(app->instance, NULL);
  glfwDestroyWindow(app->window);
  glfwTerminate();
}

void run(void) {
  app_t app = {0};

  init_window(&app);
  init_vulkan(&app);
  main_loop(&app);
  cleanup(&app);
}

int main(void) {
  run();
  return EXIT_SUCCESS;
}
