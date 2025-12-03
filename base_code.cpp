#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#define TINYOBJLOADER_IMPLEMENTATION // define this in only *one* .cc
// Optional. define TINYOBJLOADER_USE_MAPBOX_EARCUT gives robust triangulation. Requires C++11
#define TINYOBJLOADER_USE_MAPBOX_EARCUT
#include "tiny_obj_loader.h"

#include <vulkan/vulkan_core.h>
#if defined(__INTELLISENSE__) || !defined(USE_CPP20_MODULES)
#include <vulkan/vulkan_raii.hpp>
#else
import vulkan_hpp;
#endif
#include <vulkan/vulkan_handles.hpp>
#define GLFW_INCLUDE_VULKAN // REQUIRED only for GLFW CreateWindowSurface.
#include <GLFW/glfw3.h>
#include <vulkan/vulkan_enums.hpp>

// #define GLM_FORCE_DEFAULT_ALIGNED_GENTYPES
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#define GLM_FORCE_RADIANS
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/hash.hpp>

#include <algorithm>
#include <array>
#include <assert.h>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <unordered_map>
#include <vector>

constexpr uint32_t WIDTH = 800;
constexpr uint32_t HEIGHT = 600;
const std::string MODEL_PATH = "models/viking_room.obj";
const std::string TEXTURE_PATH = "textures/viking_room.png";
constexpr int MAX_FRAMES_IN_FLIGHT = 2;

// const std::vector<char const*> validationLayers = {
// "VK_LAYER_KHRONOS_validation"
// };

const std::vector<const char*> validationLayers = {
    "VK_LAYER_KHRONOS_validation"
};

// #ifdef NDEBUG
// constexpr bool enableValidationLayers = false;
// #else
// constexpr bool enableValidationLayers = true;
// #endif

struct AppInfo {
    bool dynamicRenderingSupported = false;
    bool timelineSemaphoresSupported = false;
    bool synchronization2Supported = false;
};

struct UniformBufferObject {
    // glm::vec2 foo; // alignment test
    // alignas(16) glm::mat4 model;
    glm::mat4 model;
    glm::mat4 view;
    glm::mat4 proj;
};

struct Vertex {
    glm::vec3 pos;
    glm::vec3 color;
    glm::vec2 texCoord;

    static vk::VertexInputBindingDescription getBindingDescription()
    {
        return vk::VertexInputBindingDescription()
            .setBinding(0)
            .setStride(sizeof(Vertex))
            .setInputRate(vk::VertexInputRate::eVertex);
    }

    static std::array<vk::VertexInputAttributeDescription, 3>
    getAttributeDescriptions()
    {
        return {
            vk::VertexInputAttributeDescription(
                0, 0, vk::Format::eR32G32B32Sfloat, offsetof(Vertex, pos)),
            vk::VertexInputAttributeDescription(
                1, 0, vk::Format::eR32G32B32Sfloat, offsetof(Vertex, color)),
            vk::VertexInputAttributeDescription(
                2, 0, vk::Format::eR32G32Sfloat, offsetof(Vertex, texCoord))
        };
    }

    bool operator==(const Vertex& other) const
    {
        return pos == other.pos && color == other.color && texCoord == other.texCoord;
    }
};

template <>
struct std::hash<Vertex> {
    size_t operator()(Vertex const& vertex) const noexcept
    {
        return ((hash<glm::vec3>()(vertex.pos) ^ (hash<glm::vec3>()(vertex.color) << 1)) >> 1) ^ (hash<glm::vec2>()(vertex.texCoord) << 1);
    }
};

class HelloTriangleApplication {
public:
    void run()
    {
        initWindow();
        initVulkan();
        mainLoop();
        cleanup();
    }

private:
    AppInfo appInfo;

    GLFWwindow* window = nullptr;
    vk::raii::Context context;
    vk::raii::Instance instance = nullptr;
    vk::raii::DebugUtilsMessengerEXT debugMessenger = nullptr;
    vk::raii::SurfaceKHR surface = nullptr;
    vk::raii::PhysicalDevice physicalDevice = nullptr;
    vk::raii::Device device = nullptr;
    uint32_t queueIndex = ~0;
    vk::raii::Queue queue = nullptr;
    vk::raii::SwapchainKHR swapChain = nullptr;
    std::vector<vk::Image> swapChainImages;
    vk::SurfaceFormatKHR swapChainSurfaceFormat;
    vk::Extent2D swapChainExtent;
    std::vector<vk::raii::ImageView> swapChainImageViews;

    // Traditional render pass (fallback for non-dynamic rendering)
    vk::raii::RenderPass renderPass = nullptr;
    std::vector<vk::raii::Framebuffer> swapChainFramebuffers;

    vk::raii::PipelineLayout pipelineLayout = nullptr;
    vk::raii::DescriptorSetLayout descriptorSetLayout = nullptr;
    vk::raii::Pipeline graphicsPipeline = nullptr;

    vk::raii::CommandPool commandPool = nullptr;
    std::vector<vk::raii::CommandBuffer> commandBuffers;

    std::vector<vk::raii::Semaphore> presentCompleteSemaphores;
    std::vector<vk::raii::Semaphore> renderFinishedSemaphores;
    std::vector<vk::raii::Fence> inFlightFences;

    vk::raii::Buffer vertexBuffer = nullptr;
    vk::raii::Buffer indexBuffer = nullptr;
    vk::raii::DeviceMemory vertexBufferMemory = nullptr;
    vk::raii::DeviceMemory indexBufferMemory = nullptr;

    std::vector<vk::raii::Buffer> uniformBuffers;
    std::vector<vk::raii::DeviceMemory> uniformBuffersMemory;
    std::vector<void*> uniformBuffersMapped;

    vk::raii::DescriptorPool descriptorPool = nullptr;
    std::vector<vk::raii::DescriptorSet> descriptorSets;

    uint32_t mipLevels = 0;
    vk::raii::Image textureImage = nullptr;
    vk::raii::DeviceMemory textureImageMemory = nullptr;
    vk::raii::ImageView textureImageView = nullptr;
    vk::raii::Sampler textureSampler = nullptr;

    vk::raii::Image depthImage = nullptr;
    vk::raii::DeviceMemory depthImageMemory = nullptr;
    vk::raii::ImageView depthImageView = nullptr;

    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;
    std::unordered_map<Vertex, uint32_t> uniqueVertices {};

    vk::SampleCountFlagBits msaaSamples = vk::SampleCountFlagBits::e1;
    vk::raii::Image colorImage = nullptr;
    vk::raii::DeviceMemory colorImageMemory = nullptr;
    vk::raii::ImageView colorImageView = nullptr;

    // const std::vector<Vertex> vertices = {
    // { { -0.5f, -0.5f, 0.0f }, { 1.0f, 0.0f, 0.0f }, { 0.0f, 0.0f } },
    // { { 0.5f, -0.5f, 0.0f }, { 0.0f, 1.0f, 0.0f }, { 1.0f, 0.0f } },
    // { { 0.5f, 0.5f, 0.0f }, { 0.0f, 0.0f, 1.0f }, { 1.0f, 1.0f } },
    // { { -0.5f, 0.5f, 0.0f }, { 1.0f, 1.0f, 1.0f }, { 0.0f, 1.0f } },

    // { { -0.5f, -0.5f, -0.5f }, { 1.0f, 0.0f, 0.0f }, { 0.0f, 0.0f } },
    // { { 0.5f, -0.5f, -0.5f }, { 0.0f, 1.0f, 0.0f }, { 1.0f, 0.0f } },
    // { { 0.5f, 0.5f, -0.5f }, { 0.0f, 0.0f, 1.0f }, { 1.0f, 1.0f } },
    // { { -0.5f, 0.5f, -0.5f }, { 1.0f, 1.0f, 1.0f }, { 0.0f, 1.0f } }
    // };

    // const std::vector<uint16_t> indices = {
    // 0, 1, 2, 2, 3, 0,
    // 4, 5, 6, 6, 7, 4
    // };

    uint32_t currentFrame = 0;
    uint32_t semaphoreIndex = 0;
    bool framebufferResized = false;

    std::vector<const char*> requiredDeviceExtension = {
        vk::KHRSwapchainExtensionName,
        vk::KHRSpirv14ExtensionName,
        vk::KHRSynchronization2ExtensionName,
        vk::KHRCreateRenderpass2ExtensionName
    };

    void initWindow()
    {
        glfwInit();

        glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
        glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);

        window = glfwCreateWindow(WIDTH, HEIGHT, "Vulkan", nullptr, nullptr);

        glfwSetWindowUserPointer(window, this);
        glfwSetFramebufferSizeCallback(window, framebufferResizeCallback);
    }

    void initVulkan()
    {
        createInstance();
        setupDebugMessenger();
        createSurface();
        pickPhysicalDevice();
        detectFeatureSupport();
        // msaaSamples = getMaxUsableSampleCount(); -> Move to pickPhysicalDevice
        createLogicalDevice();
        createSwapChain();
        createImageViews();

        // Create traditional render pass if dynamic rendering is not supported
        if (!appInfo.dynamicRenderingSupported) {
            createRenderPass();
            createFramebuffers();
        }

        createDescriptorSetLayout();
        createGraphicsPipeline();
        createCommandPool();
        createColorResources();
        createDepthResources();
        createTextureImage();
        createTextureImageView();
        createTextureSampler();
        loadModel();
        createVertexBuffer();
        createIndexBuffer();
        createUniformBuffers();
        createDescriptorPool();
        createDescriptorSets();
        createCommandBuffers();
        createSyncObjects();

        // Print feature support summary
        std::cout << "\nFeature support summary:\n";
        std::cout << "- Dynamic Rendering: " << (appInfo.dynamicRenderingSupported ? "Yes" : "No") << "\n";
        std::cout << "- Timeline Semaphores: " << (appInfo.timelineSemaphoresSupported ? "Yes" : "No") << "\n";
        std::cout << "- Synchronization2: " << (appInfo.synchronization2Supported ? "Yes" : "No") << "\n";
    }

    void mainLoop()
    {
        while (!glfwWindowShouldClose(window)) {
            glfwPollEvents();
            drawFrame();
            // std::cout << "Vertices size: " << vertices.size() << "\n";
            // std::cout << "Indices size: " << indices.size() << "\n";
        }
        device.waitIdle(); // wait for device to finish operations before destroying
                           // resources
    }

    void cleanup()
    {
        cleanupSwapChain();

        glfwDestroyWindow(window);
        glfwTerminate();
    }

    void createInstance()
    {
        constexpr vk::ApplicationInfo appInfo = vk::ApplicationInfo()
                                                    .setPApplicationName("Hello Triangle")
                                                    .setApplicationVersion(VK_MAKE_VERSION(1, 0, 0))
                                                    .setPEngineName("No Engine")
                                                    .setEngineVersion(VK_MAKE_VERSION(1, 0, 0))
                                                    .setApiVersion(vk::ApiVersion14);

        // Get the required layers
        std::vector<char const*> requiredLayers;
        // if (enableValidationLayers) {
        //     requiredLayers.assign(validationLayers.begin(), validationLayers.end());
        // }

        // Check if the required layers are supported by the Vulkan implementation.
        auto layerProperties = context.enumerateInstanceLayerProperties();
        for (auto const& requiredLayer : requiredLayers) {
            if (std::ranges::none_of(
                    layerProperties, [requiredLayer](auto const& layerProperty) {
                        return strcmp(layerProperty.layerName, requiredLayer) == 0;
                    })) {
                throw std::runtime_error("Required layer not supported: " + std::string(requiredLayer));
            }
        }

        // Get the required extensions.
        auto requiredExtensions = getRequiredExtensions();

        // Check if the required extensions are supported by the Vulkan
        // implementation.
        auto extensionProperties = context.enumerateInstanceExtensionProperties();
        for (auto const& requiredExtension : requiredExtensions) {
            if (std::ranges::none_of(
                    extensionProperties,
                    [requiredExtension](auto const& extensionProperty) {
                        return strcmp(extensionProperty.extensionName,
                                   requiredExtension)
                            == 0;
                    })) {
                throw std::runtime_error("Required extension not supported: " + std::string(requiredExtension));
            }
        }

        vk::InstanceCreateInfo createInfo = vk::InstanceCreateInfo()
                                                .setPApplicationInfo(&appInfo)
                                                .setEnabledLayerCount(static_cast<uint32_t>(requiredLayers.size()))
                                                .setPpEnabledLayerNames(requiredLayers.data())
                                                .setEnabledExtensionCount(
                                                    static_cast<uint32_t>(requiredExtensions.size()))
                                                .setPpEnabledExtensionNames(requiredExtensions.data());

        instance = vk::raii::Instance(context, createInfo);
    }

    void setupDebugMessenger()
    {
        // Always set up the debug messenger
        // It will only be used if validation layers are enabled via vulkanconfig

        // vk::DebugUtilsMessageSeverityFlagsEXT severityFlags(
        //     vk::DebugUtilsMessageSeverityFlagBitsEXT::eVerbose | vk::DebugUtilsMessageSeverityFlagBitsEXT::eWarning | vk::DebugUtilsMessageSeverityFlagBitsEXT::eError);

        // vk::DebugUtilsMessageTypeFlagsEXT messageTypeFlags(
        //     vk::DebugUtilsMessageTypeFlagBitsEXT::eGeneral | vk::DebugUtilsMessageTypeFlagBitsEXT::ePerformance | vk::DebugUtilsMessageTypeFlagBitsEXT::eValidation);

        // vk::DebugUtilsMessengerCreateInfoEXT debugUtilsMessengerCreateInfoEXT = vk::DebugUtilsMessengerCreateInfoEXT()
        //                                                                             .setMessageSeverity(severityFlags)
        //                                                                             .setMessageType(messageTypeFlags)
        //                                                                             .setPfnUserCallback(&debugCallback);

        // try {
        //     debugMessenger = instance.createDebugUtilsMessengerEXT(debugUtilsMessengerCreateInfoEXT);
        // } catch (vk::SystemError& err) {
        //     // If the debug utils extension is not available, this will fail
        //     // That's okay; it just means validation layers aren't enabled
        //     std::cout << "Debug messenger not available. Validation layers may not be enabled." << std::endl;
        // }

        vk::DebugUtilsMessageSeverityFlagsEXT severityFlags(
            vk::DebugUtilsMessageSeverityFlagBitsEXT::eVerbose | vk::DebugUtilsMessageSeverityFlagBitsEXT::eWarning | vk::DebugUtilsMessageSeverityFlagBitsEXT::eError);

        vk::DebugUtilsMessageTypeFlagsEXT messageTypeFlags(
            vk::DebugUtilsMessageTypeFlagBitsEXT::eGeneral | vk::DebugUtilsMessageTypeFlagBitsEXT::ePerformance | vk::DebugUtilsMessageTypeFlagBitsEXT::eValidation);

        vk::DebugUtilsMessengerCreateInfoEXT debugUtilsMessengerCreateInfoEXT = vk::DebugUtilsMessengerCreateInfoEXT()
                                                                                    .setMessageSeverity(severityFlags)
                                                                                    .setMessageType(messageTypeFlags)
                                                                                    .setPfnUserCallback(&debugCallback);

        debugMessenger = instance.createDebugUtilsMessengerEXT(debugUtilsMessengerCreateInfoEXT);
    }

    void createSurface()
    {
        VkSurfaceKHR _surface;
        if (glfwCreateWindowSurface(*instance, window, nullptr, &_surface) != 0) {
            throw std::runtime_error("failed to create window surface!");
        }
        surface = vk::raii::SurfaceKHR(instance, _surface);
    }

    void pickPhysicalDevice()
    {
        std::vector<vk::raii::PhysicalDevice> devices = instance.enumeratePhysicalDevices();
        const auto devIter = std::ranges::find_if(
            devices,
            [&](auto const& device) {
                // Check if any of the queue families support graphics operations
                auto queueFamilies = device.getQueueFamilyProperties();
                bool supportsGraphics = std::ranges::any_of(queueFamilies, [](auto const& qfp) { return !!(qfp.queueFlags & vk::QueueFlagBits::eGraphics); });

                // Check if all required device extensions are available
                auto availableDeviceExtensions = device.enumerateDeviceExtensionProperties();
                bool supportsAllRequiredExtensions = std::ranges::all_of(requiredDeviceExtension,
                    [&availableDeviceExtensions](auto const& requiredDeviceExtension) {
                        return std::ranges::any_of(availableDeviceExtensions,
                            [requiredDeviceExtension](auto const& availableDeviceExtension) { return strcmp(availableDeviceExtension.extensionName, requiredDeviceExtension) == 0; });
                    });

                return supportsGraphics && supportsAllRequiredExtensions;
            });
        if (devIter != devices.end()) {
            physicalDevice = *devIter;
            msaaSamples = getMaxUsableSampleCount();
        } else {
            throw std::runtime_error("failed to find a suitable GPU!");
        }
    }

    void createLogicalDevice()
    {
        std::vector<vk::QueueFamilyProperties> queueFamilyProperties = physicalDevice.getQueueFamilyProperties();

        // get the first index into queueFamilyProperties which supports both
        // graphics and present
        for (uint32_t qfpIndex = 0; qfpIndex < queueFamilyProperties.size();
            qfpIndex++) {
            if ((queueFamilyProperties[qfpIndex].queueFlags & vk::QueueFlagBits::eGraphics) && physicalDevice.getSurfaceSupportKHR(qfpIndex, *surface)) {
                // found a queue family that supports both graphics and present
                queueIndex = qfpIndex;
                break;
            }
        }
        if (queueIndex == ~0) {
            throw std::runtime_error(
                "Could not find a queue for graphics and present -> terminating");
        }

        vk::StructureChain<vk::PhysicalDeviceFeatures2,
            vk::PhysicalDeviceVulkan11Features,
            vk::PhysicalDeviceVulkan13Features,
            vk::PhysicalDeviceExtendedDynamicStateFeaturesEXT,
            vk::PhysicalDeviceSwapchainMaintenance1FeaturesKHR // EDIT
            >
            featureChain;

        featureChain.get<vk::PhysicalDeviceVulkan11Features>().shaderDrawParameters = true;
        featureChain.get<vk::PhysicalDeviceVulkan13Features>().synchronization2 = true;
        featureChain.get<vk::PhysicalDeviceVulkan13Features>().dynamicRendering = true;
        featureChain.get<vk::PhysicalDeviceExtendedDynamicStateFeaturesEXT>().extendedDynamicState = true;
        featureChain.get<vk::PhysicalDeviceFeatures2>().features.setSamplerAnisotropy(true);
        featureChain.get<vk::PhysicalDeviceSwapchainMaintenance1FeaturesKHR>().swapchainMaintenance1 = true; // EDIT

        // create a Device
        float queuePriority = 0.5f;
        vk::DeviceQueueCreateInfo deviceQueueCreateInfo = vk::DeviceQueueCreateInfo()
                                                              .setQueueFamilyIndex(queueIndex)
                                                              .setQueueCount(1)
                                                              .setPQueuePriorities(&queuePriority);

        vk::DeviceCreateInfo deviceCreateInfo = vk::DeviceCreateInfo()
                                                    .setPNext(&featureChain.get<vk::PhysicalDeviceFeatures2>())
                                                    .setQueueCreateInfoCount(1)
                                                    .setPQueueCreateInfos(&deviceQueueCreateInfo)
                                                    .setEnabledExtensionCount(
                                                        static_cast<uint32_t>(requiredDeviceExtension.size()))
                                                    .setPpEnabledExtensionNames(requiredDeviceExtension.data());

        device = vk::raii::Device(physicalDevice, deviceCreateInfo);
        queue = vk::raii::Queue(device, queueIndex, 0);
    }

    void createSwapChain()
    {
        auto surfaceCapabilities = physicalDevice.getSurfaceCapabilitiesKHR(*surface);
        swapChainExtent = chooseSwapExtent(surfaceCapabilities);
        swapChainSurfaceFormat = chooseSwapSurfaceFormat(physicalDevice.getSurfaceFormatsKHR(*surface));
        vk::SwapchainCreateInfoKHR swapChainCreateInfo = vk::SwapchainCreateInfoKHR()
                                                             .setSurface(*surface)
                                                             .setMinImageCount(chooseSwapMinImageCount(surfaceCapabilities))
                                                             .setImageFormat(swapChainSurfaceFormat.format)
                                                             .setImageColorSpace(swapChainSurfaceFormat.colorSpace)
                                                             .setImageExtent(swapChainExtent)
                                                             .setImageArrayLayers(1)
                                                             .setImageUsage(vk::ImageUsageFlagBits::eColorAttachment)
                                                             .setImageSharingMode(vk::SharingMode::eExclusive)
                                                             .setPreTransform(surfaceCapabilities.currentTransform)
                                                             .setCompositeAlpha(vk::CompositeAlphaFlagBitsKHR::eOpaque)
                                                             .setPresentMode(chooseSwapPresentMode(
                                                                 physicalDevice.getSurfacePresentModesKHR(*surface)))
                                                             .setClipped(true);

        swapChain = vk::raii::SwapchainKHR(device, swapChainCreateInfo);
        swapChainImages = swapChain.getImages();
    }

    void createImageViews()
    {
        assert(swapChainImageViews.empty());

        vk::ImageViewCreateInfo imageViewCreateInfo = vk::ImageViewCreateInfo()
                                                          .setViewType(vk::ImageViewType::e2D)
                                                          .setFormat(swapChainSurfaceFormat.format)
                                                          .setSubresourceRange(vk::ImageSubresourceRange()
                                                                  .setAspectMask(vk::ImageAspectFlagBits::eColor)
                                                                  .setBaseMipLevel(0)
                                                                  .setLevelCount(1)
                                                                  .setBaseArrayLayer(0)
                                                                  .setLayerCount(1));

        for (auto& image : swapChainImages) {
            imageViewCreateInfo.image = image;
            swapChainImageViews.emplace_back(device, imageViewCreateInfo);
        }
    }

    void createGraphicsPipeline()
    {
        vk::raii::ShaderModule shaderModule = createShaderModule(readFile("shaders/slang.spv"));

        vk::PipelineShaderStageCreateInfo vertShaderStageInfo = vk::PipelineShaderStageCreateInfo()
                                                                    .setStage(vk::ShaderStageFlagBits::eVertex)
                                                                    .setModule(shaderModule)
                                                                    .setPName("vertMain");
        vk::PipelineShaderStageCreateInfo fragShaderStageInfo = vk::PipelineShaderStageCreateInfo()
                                                                    .setStage(vk::ShaderStageFlagBits::eFragment)
                                                                    .setModule(shaderModule)
                                                                    .setPName("fragMain");

        vk::PipelineShaderStageCreateInfo shaderStages[] = { vertShaderStageInfo,
            fragShaderStageInfo };

        auto bindingDescription = Vertex::getBindingDescription();
        auto attributeDescriptions = Vertex::getAttributeDescriptions();
        vk::PipelineVertexInputStateCreateInfo vertexInputInfo = vk::PipelineVertexInputStateCreateInfo()
                                                                     .setVertexBindingDescriptionCount(1)
                                                                     .setPVertexBindingDescriptions(&bindingDescription)
                                                                     .setVertexAttributeDescriptionCount(attributeDescriptions.size())
                                                                     .setPVertexAttributeDescriptions(attributeDescriptions.data());

        vk::PipelineInputAssemblyStateCreateInfo inputAssembly = vk::PipelineInputAssemblyStateCreateInfo().setTopology(
            vk::PrimitiveTopology::eTriangleList);

        vk::PipelineViewportStateCreateInfo viewportState = vk::PipelineViewportStateCreateInfo()
                                                                .setViewportCount(1)
                                                                .setScissorCount(1);

        vk::PipelineRasterizationStateCreateInfo rasterizer = vk::PipelineRasterizationStateCreateInfo()
                                                                  .setDepthClampEnable(vk::False)
                                                                  .setRasterizerDiscardEnable(vk::False)
                                                                  .setPolygonMode(vk::PolygonMode::eFill)
                                                                  .setCullMode(vk::CullModeFlagBits::eBack)
                                                                  .setFrontFace(vk::FrontFace::eCounterClockwise)
                                                                  .setDepthBiasEnable(vk::False)
                                                                  .setDepthBiasConstantFactor(0.0f)
                                                                  .setDepthBiasClamp(0.0f)
                                                                  .setDepthBiasSlopeFactor(1.f)
                                                                  .setLineWidth(1.f);

        vk::PipelineMultisampleStateCreateInfo multisampling = vk::PipelineMultisampleStateCreateInfo()
                                                                   .setRasterizationSamples(msaaSamples)
                                                                   .setSampleShadingEnable(vk::False);

        vk::PipelineDepthStencilStateCreateInfo depthStencil = vk::PipelineDepthStencilStateCreateInfo()
                                                                   .setDepthTestEnable(vk::True)
                                                                   .setDepthWriteEnable(vk::True)
                                                                   .setDepthCompareOp(vk::CompareOp::eLess)
                                                                   .setDepthBoundsTestEnable(vk::False)
                                                                   .setStencilTestEnable(vk::False);

        vk::PipelineColorBlendAttachmentState colorBlendAttachment = vk::PipelineColorBlendAttachmentState()
                                                                         .setBlendEnable(vk::False)
                                                                         .setColorWriteMask(vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG | vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA);

        vk::PipelineColorBlendStateCreateInfo colorBlending = vk::PipelineColorBlendStateCreateInfo()
                                                                  .setLogicOpEnable(vk::False)
                                                                  .setLogicOp(vk::LogicOp::eCopy)
                                                                  .setAttachmentCount(1)
                                                                  .setPAttachments(&colorBlendAttachment);

        std::vector dynamicStates = { vk::DynamicState::eViewport,
            vk::DynamicState::eScissor };
        vk::PipelineDynamicStateCreateInfo dynamicState = vk::PipelineDynamicStateCreateInfo()
                                                              .setDynamicStateCount(static_cast<uint32_t>(dynamicStates.size()))
                                                              .setPDynamicStates(dynamicStates.data());

        vk::PipelineLayoutCreateInfo pipelineLayoutInfo = vk::PipelineLayoutCreateInfo()
                                                              .setSetLayoutCount(1)
                                                              .setPSetLayouts(&*descriptorSetLayout)
                                                              .setPushConstantRangeCount(0);

        pipelineLayout = vk::raii::PipelineLayout(device, pipelineLayoutInfo);

        vk::Format depthFormat = findDepthFormat();

        vk::StructureChain<vk::GraphicsPipelineCreateInfo,
            vk::PipelineRenderingCreateInfo>
            pipelineCreateInfoChain;

        pipelineCreateInfoChain.get<vk::GraphicsPipelineCreateInfo>().stageCount = 2;
        pipelineCreateInfoChain.get<vk::GraphicsPipelineCreateInfo>().pStages = shaderStages;
        pipelineCreateInfoChain.get<vk::GraphicsPipelineCreateInfo>().pVertexInputState = &vertexInputInfo;
        pipelineCreateInfoChain.get<vk::GraphicsPipelineCreateInfo>().pInputAssemblyState = &inputAssembly;
        pipelineCreateInfoChain.get<vk::GraphicsPipelineCreateInfo>().pViewportState = &viewportState;
        pipelineCreateInfoChain.get<vk::GraphicsPipelineCreateInfo>().pRasterizationState = &rasterizer;
        pipelineCreateInfoChain.get<vk::GraphicsPipelineCreateInfo>().pMultisampleState = &multisampling;
        pipelineCreateInfoChain.get<vk::GraphicsPipelineCreateInfo>().pDepthStencilState = &depthStencil;
        pipelineCreateInfoChain.get<vk::GraphicsPipelineCreateInfo>().pColorBlendState = &colorBlending;
        pipelineCreateInfoChain.get<vk::GraphicsPipelineCreateInfo>().pDynamicState = &dynamicState;
        pipelineCreateInfoChain.get<vk::GraphicsPipelineCreateInfo>().layout = pipelineLayout;
        pipelineCreateInfoChain.get<vk::GraphicsPipelineCreateInfo>().renderPass = nullptr;

        pipelineCreateInfoChain.get<vk::PipelineRenderingCreateInfo>().colorAttachmentCount = 1;
        pipelineCreateInfoChain.get<vk::PipelineRenderingCreateInfo>().pColorAttachmentFormats = &swapChainSurfaceFormat.format;
        pipelineCreateInfoChain.get<vk::PipelineRenderingCreateInfo>().depthAttachmentFormat = findDepthFormat();

        if (appInfo.dynamicRenderingSupported) {
            std::cout << "Configuring pipeline for dynamic rendering\n";
        } else {
            std::cout << "Configuring pipeline for traditional render pass\n";
            pipelineCreateInfoChain.unlink<vk::PipelineRenderingCreateInfo>();
            pipelineCreateInfoChain.get<vk::GraphicsPipelineCreateInfo>().renderPass = *renderPass;
        }

        graphicsPipeline = vk::raii::Pipeline(
            device, nullptr,
            pipelineCreateInfoChain.get<vk::GraphicsPipelineCreateInfo>());
    }

    vk::Format findSupportedFormat(const std::vector<vk::Format>& candidates, vk::ImageTiling tiling, vk::FormatFeatureFlags features) const
    {
        for (const auto format : candidates) {
            vk::FormatProperties props = physicalDevice.getFormatProperties(format);

            if (tiling == vk::ImageTiling::eLinear && (props.linearTilingFeatures & features) == features) {
                return format;
            }
            if (tiling == vk::ImageTiling::eOptimal && (props.optimalTilingFeatures & features) == features) {
                return format;
            }
        }

        throw std::runtime_error("failed to find supported format!");
    }

    [[nodiscard]] vk::Format findDepthFormat() const
    {
        return findSupportedFormat(
            { vk::Format::eD32Sfloat, vk::Format::eD32SfloatS8Uint, vk::Format::eD24UnormS8Uint },
            vk::ImageTiling::eOptimal,
            vk::FormatFeatureFlagBits::eDepthStencilAttachment);
    }

    void createCommandPool()
    {
        vk::CommandPoolCreateInfo poolInfo = vk::CommandPoolCreateInfo()
                                                 .setFlags(vk::CommandPoolCreateFlagBits::eResetCommandBuffer)
                                                 .setQueueFamilyIndex(queueIndex);

        commandPool = vk::raii::CommandPool(device, poolInfo);
    }

    void createCommandBuffers()
    {
        vk::CommandBufferAllocateInfo allocInfo = vk::CommandBufferAllocateInfo()
                                                      .setCommandPool(commandPool)
                                                      .setLevel(vk::CommandBufferLevel::ePrimary)
                                                      .setCommandBufferCount(MAX_FRAMES_IN_FLIGHT);

        commandBuffers = vk::raii::CommandBuffers(device, allocInfo);
    }

    void recordCommandBuffer(uint32_t imageIndex)
    {
        commandBuffers[currentFrame].begin({});

        vk::ClearValue clearColor = vk::ClearColorValue(0.0f, 0.0f, 0.0f, 1.0f);
        vk::ClearValue clearDepth = vk::ClearDepthStencilValue(1.0f, 0);
        std::array<vk::ClearValue, 2> clearValues = { clearColor, clearDepth };

        if (appInfo.dynamicRenderingSupported) {
            // Transition attachments to the correct layout
            if (appInfo.synchronization2Supported) {
                // Use Synchronization2 API for image transitions
                vk::ImageMemoryBarrier2 colorBarrier = vk::ImageMemoryBarrier2()
                                                           .setSrcStageMask(vk::PipelineStageFlagBits2::eColorAttachmentOutput)
                                                           .setSrcAccessMask(vk::AccessFlagBits2::eColorAttachmentWrite)
                                                           .setDstStageMask(vk::PipelineStageFlagBits2::eColorAttachmentOutput)
                                                           .setDstAccessMask(vk::AccessFlagBits2::eColorAttachmentWrite)
                                                           .setOldLayout(vk::ImageLayout::eUndefined)
                                                           .setNewLayout(vk::ImageLayout::eColorAttachmentOptimal)
                                                           .setImage(*colorImage)
                                                           .setSubresourceRange({ vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1 });

                vk::ImageMemoryBarrier2 depthBarrier = vk::ImageMemoryBarrier2()
                                                           .setSrcStageMask(vk::PipelineStageFlagBits2::eEarlyFragmentTests | vk::PipelineStageFlagBits2::eLateFragmentTests)
                                                           .setSrcAccessMask(vk::AccessFlagBits2::eDepthStencilAttachmentWrite)
                                                           .setDstStageMask(vk::PipelineStageFlagBits2::eEarlyFragmentTests | vk::PipelineStageFlagBits2::eLateFragmentTests)
                                                           .setDstAccessMask(vk::AccessFlagBits2::eDepthStencilAttachmentWrite)
                                                           .setOldLayout(vk::ImageLayout::eUndefined)
                                                           .setNewLayout(vk::ImageLayout::eDepthStencilAttachmentOptimal)
                                                           .setImage(*depthImage)
                                                           .setSubresourceRange({ vk::ImageAspectFlagBits::eDepth, 0, 1, 0, 1 });

                vk::ImageMemoryBarrier2 swapchainBarrier = vk::ImageMemoryBarrier2()
                                                               .setSrcStageMask(vk::PipelineStageFlagBits2::eColorAttachmentOutput)
                                                               .setSrcAccessMask(vk::AccessFlagBits2::eNone)
                                                               .setDstStageMask(vk::PipelineStageFlagBits2::eColorAttachmentOutput)
                                                               .setDstAccessMask(vk::AccessFlagBits2::eColorAttachmentWrite)
                                                               .setOldLayout(vk::ImageLayout::eUndefined)
                                                               .setNewLayout(vk::ImageLayout::eColorAttachmentOptimal)
                                                               .setImage(swapChainImages[imageIndex])
                                                               .setSubresourceRange({ vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1 });

                std::array<vk::ImageMemoryBarrier2, 3> barriers = { colorBarrier, depthBarrier, swapchainBarrier };
                vk::DependencyInfo dependencyInfo = vk::DependencyInfo()
                                                        .setImageMemoryBarrierCount(static_cast<uint32_t>(barriers.size()))
                                                        .setPImageMemoryBarriers(barriers.data());

                commandBuffers[currentFrame].pipelineBarrier2(dependencyInfo);
            } else {
                // Use traditional synchronization API
                vk::ImageMemoryBarrier colorBarrier = vk::ImageMemoryBarrier()
                                                          .setSrcAccessMask(vk::AccessFlagBits::eNone)
                                                          .setDstAccessMask(vk::AccessFlagBits::eColorAttachmentWrite)
                                                          .setOldLayout(vk::ImageLayout::eUndefined)
                                                          .setNewLayout(vk::ImageLayout::eColorAttachmentOptimal)
                                                          .setSrcQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
                                                          .setDstQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
                                                          .setImage(*colorImage)
                                                          .setSubresourceRange({ vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1 });

                vk::ImageMemoryBarrier depthBarrier = vk::ImageMemoryBarrier()
                                                          .setSrcAccessMask(vk::AccessFlagBits::eNone)
                                                          .setDstAccessMask(vk::AccessFlagBits::eDepthStencilAttachmentWrite)
                                                          .setOldLayout(vk::ImageLayout::eUndefined)
                                                          .setNewLayout(vk::ImageLayout::eDepthStencilAttachmentOptimal)
                                                          .setSrcQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
                                                          .setDstQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
                                                          .setImage(*depthImage)
                                                          .setSubresourceRange({ vk::ImageAspectFlagBits::eDepth, 0, 1, 0, 1 });

                vk::ImageMemoryBarrier swapchainBarrier = vk::ImageMemoryBarrier()
                                                              .setSrcAccessMask(vk::AccessFlagBits::eNone)
                                                              .setDstAccessMask(vk::AccessFlagBits::eColorAttachmentWrite)
                                                              .setOldLayout(vk::ImageLayout::eUndefined)
                                                              .setNewLayout(vk::ImageLayout::eColorAttachmentOptimal)
                                                              .setSrcQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
                                                              .setDstQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
                                                              .setImage(swapChainImages[imageIndex])
                                                              .setSubresourceRange({ vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1 });

                std::array<vk::ImageMemoryBarrier, 3> barriers = { colorBarrier, depthBarrier, swapchainBarrier };
                commandBuffers[currentFrame].pipelineBarrier(
                    vk::PipelineStageFlagBits::eTopOfPipe,
                    vk::PipelineStageFlagBits::eColorAttachmentOutput | vk::PipelineStageFlagBits::eEarlyFragmentTests,
                    vk::DependencyFlagBits::eByRegion,
                    {},
                    {},
                    barriers);
            }

            // Setup rendering attachments
            vk::RenderingAttachmentInfo colorAttachment = vk::RenderingAttachmentInfo()
                                                              .setImageView(*colorImageView)
                                                              .setImageLayout(vk::ImageLayout::eColorAttachmentOptimal)
                                                              .setResolveMode(vk::ResolveModeFlagBits::eAverage)
                                                              .setResolveImageView(*swapChainImageViews[imageIndex])
                                                              .setResolveImageLayout(vk::ImageLayout::eColorAttachmentOptimal)
                                                              .setLoadOp(vk::AttachmentLoadOp::eClear)
                                                              .setStoreOp(vk::AttachmentStoreOp::eStore)
                                                              .setClearValue(clearColor);

            vk::RenderingAttachmentInfo depthAttachment = vk::RenderingAttachmentInfo()
                                                              .setImageView(*depthImageView)
                                                              .setImageLayout(vk::ImageLayout::eDepthStencilAttachmentOptimal)
                                                              .setLoadOp(vk::AttachmentLoadOp::eClear)
                                                              .setStoreOp(vk::AttachmentStoreOp::eDontCare)
                                                              .setClearValue(clearDepth);

            vk::RenderingInfo renderingInfo = vk::RenderingInfo()
                                                  .setRenderArea({ { 0, 0 }, swapChainExtent })
                                                  .setLayerCount(1)
                                                  .setColorAttachmentCount(1)
                                                  .setPColorAttachments(&colorAttachment)
                                                  .setPDepthAttachment(&depthAttachment);

            commandBuffers[currentFrame].beginRendering(renderingInfo);
        } else {
            // Use traditional render pass
            std::cout << "Recording command buffer with traditional render pass\n";

            vk::RenderPassBeginInfo renderPassInfo = vk::RenderPassBeginInfo()
                                                         .setRenderPass(*renderPass)
                                                         .setFramebuffer(*swapChainFramebuffers[imageIndex])
                                                         .setRenderArea({ { 0, 0 }, swapChainExtent })
                                                         .setClearValueCount(static_cast<uint32_t>(clearValues.size()))
                                                         .setPClearValues(clearValues.data());

            commandBuffers[currentFrame].beginRenderPass(renderPassInfo, vk::SubpassContents::eInline);
        }

        // Common rendering commands
        commandBuffers[currentFrame].bindPipeline(vk::PipelineBindPoint::eGraphics, *graphicsPipeline);
        commandBuffers[currentFrame].setViewport(0, vk::Viewport(0.0f, 0.0f, static_cast<float>(swapChainExtent.width), static_cast<float>(swapChainExtent.height), 0.0f, 1.0f));
        commandBuffers[currentFrame].setScissor(0, vk::Rect2D(vk::Offset2D(0, 0), swapChainExtent));
        commandBuffers[currentFrame].bindVertexBuffers(0, *vertexBuffer, { 0 });
        commandBuffers[currentFrame].bindIndexBuffer(*indexBuffer, 0, vk::IndexType::eUint32);
        commandBuffers[currentFrame].bindDescriptorSets(vk::PipelineBindPoint::eGraphics, pipelineLayout, 0, *descriptorSets[currentFrame], nullptr);
        commandBuffers[currentFrame].drawIndexed(indices.size(), 1, 0, 0, 0);

        if (appInfo.dynamicRenderingSupported) {
            commandBuffers[currentFrame].endRendering();

            // Transition swapchain image to present layout
            if (appInfo.synchronization2Supported) {
                vk::ImageMemoryBarrier2 barrier = vk::ImageMemoryBarrier2()
                                                      .setSrcStageMask(vk::PipelineStageFlagBits2::eColorAttachmentOutput)
                                                      .setSrcAccessMask(vk::AccessFlagBits2::eColorAttachmentWrite)
                                                      .setDstStageMask(vk::PipelineStageFlagBits2::eBottomOfPipe)
                                                      .setDstAccessMask(vk::AccessFlagBits2::eNone)
                                                      .setOldLayout(vk::ImageLayout::eColorAttachmentOptimal)
                                                      .setNewLayout(vk::ImageLayout::ePresentSrcKHR)
                                                      .setImage(swapChainImages[imageIndex])
                                                      .setSubresourceRange({ vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1 });

                vk::DependencyInfo dependencyInfo = vk::DependencyInfo()
                                                        .setImageMemoryBarrierCount(1)
                                                        .setPImageMemoryBarriers(&barrier);

                commandBuffers[currentFrame].pipelineBarrier2(dependencyInfo);
            } else {
                vk::ImageMemoryBarrier barrier = vk::ImageMemoryBarrier()
                                                     .setSrcAccessMask(vk::AccessFlagBits::eColorAttachmentWrite)
                                                     .setDstAccessMask(vk::AccessFlagBits::eNone)
                                                     .setOldLayout(vk::ImageLayout::eColorAttachmentOptimal)
                                                     .setNewLayout(vk::ImageLayout::ePresentSrcKHR)
                                                     .setSrcQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
                                                     .setDstQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
                                                     .setImage(swapChainImages[imageIndex])
                                                     .setSubresourceRange({ vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1 });

                commandBuffers[currentFrame].pipelineBarrier(
                    vk::PipelineStageFlagBits::eColorAttachmentOutput,
                    vk::PipelineStageFlagBits::eBottomOfPipe,
                    vk::DependencyFlagBits::eByRegion,
                    {},
                    {},
                    { barrier });
            }
        } else {
            commandBuffers[currentFrame].endRenderPass();
        }

        commandBuffers[currentFrame].end();
    }

    // void recordCommandBuffer(uint32_t imageIndex)
    // {
    //     commandBuffers[currentFrame].begin({});
    //     // Before starting rendering, transition the swapchain image to

    //     // COLOR_ATTACHMENT_OPTIMAL
    //     transition_image_layout(
    //         swapChainImages[imageIndex],
    //         vk::ImageLayout::eUndefined,
    //         vk::ImageLayout::eColorAttachmentOptimal,
    //         {}, // srcAccessMask (no need to wait for previous operations)
    //         vk::AccessFlagBits2::eColorAttachmentWrite, // dstAccessMask
    //         vk::PipelineStageFlagBits2::eColorAttachmentOutput, // srcStage
    //         vk::PipelineStageFlagBits2::eColorAttachmentOutput, // dstStage
    //         vk::ImageAspectFlagBits::eColor);

    //     // Transition the multisampled color image to COLOR_ATTACHMENT_OPTIMAL
    //     transition_image_layout(
    //         *colorImage,
    //         vk::ImageLayout::eUndefined,
    //         vk::ImageLayout::eColorAttachmentOptimal,
    //         vk::AccessFlagBits2::eColorAttachmentWrite,
    //         vk::AccessFlagBits2::eColorAttachmentWrite,
    //         vk::PipelineStageFlagBits2::eColorAttachmentOutput,
    //         vk::PipelineStageFlagBits2::eColorAttachmentOutput,
    //         vk::ImageAspectFlagBits::eColor);

    //     // Transition depth image to depth attachment optimal layout
    //     transition_image_layout(
    //         *depthImage,
    //         vk::ImageLayout::eUndefined,
    //         vk::ImageLayout::eDepthAttachmentOptimal,
    //         vk::AccessFlagBits2::eDepthStencilAttachmentWrite,
    //         vk::AccessFlagBits2::eDepthStencilAttachmentWrite,
    //         vk::PipelineStageFlagBits2::eEarlyFragmentTests | vk::PipelineStageFlagBits2::eLateFragmentTests,
    //         vk::PipelineStageFlagBits2::eEarlyFragmentTests | vk::PipelineStageFlagBits2::eLateFragmentTests,
    //         vk::ImageAspectFlagBits::eDepth);

    //     vk::ClearValue clearColor = vk::ClearColorValue(0.0f, 0.0f, 0.0f, 1.0f);
    //     vk::ClearValue clearDepth = vk::ClearDepthStencilValue(1.0f, 0);

    //     // Color attachment (mutlisampled) with resolve attachment
    //     vk::RenderingAttachmentInfo colorAttachmentInfo = vk::RenderingAttachmentInfo()
    //                                                           .setImageView(colorImageView)
    //                                                           .setImageLayout(vk::ImageLayout::eColorAttachmentOptimal)
    //                                                           .setResolveMode(vk::ResolveModeFlagBits::eAverage)
    //                                                           .setResolveImageView(swapChainImageViews[imageIndex])
    //                                                           .setResolveImageLayout(vk::ImageLayout::eColorAttachmentOptimal)
    //                                                           .setLoadOp(vk::AttachmentLoadOp::eClear)
    //                                                           .setStoreOp(vk::AttachmentStoreOp::eStore)
    //                                                           .setClearValue(clearColor);

    //     vk::RenderingAttachmentInfo depthAttachmentInfo = vk::RenderingAttachmentInfo()
    //                                                           .setImageView(depthImageView)
    //                                                           .setImageLayout(vk::ImageLayout::eDepthAttachmentOptimal)
    //                                                           .setLoadOp(vk::AttachmentLoadOp::eClear)
    //                                                           .setStoreOp(vk::AttachmentStoreOp::eDontCare)
    //                                                           .setClearValue(clearDepth);

    //     vk::RenderingInfo renderingInfo = vk::RenderingInfo()
    //                                           .setRenderArea(vk::Rect2D { vk::Offset2D { 0, 0 }, swapChainExtent })
    //                                           .setLayerCount(1)
    //                                           .setColorAttachmentCount(1)
    //                                           .setPColorAttachments(&colorAttachmentInfo)
    //                                           .setPDepthAttachment(&depthAttachmentInfo);

    //     commandBuffers[currentFrame].beginRendering(renderingInfo);
    //     commandBuffers[currentFrame].bindPipeline(vk::PipelineBindPoint::eGraphics,
    //         *graphicsPipeline);
    //     commandBuffers[currentFrame].setViewport(
    //         0,
    //         vk::Viewport(0.0f, 0.0f, static_cast<float>(swapChainExtent.width),
    //             static_cast<float>(swapChainExtent.height), 0.0f, 1.0f));
    //     commandBuffers[currentFrame].setScissor(
    //         0, vk::Rect2D(vk::Offset2D(0, 0), swapChainExtent));

    //     commandBuffers[currentFrame].bindVertexBuffers(0, *vertexBuffer, { 0 });

    //     commandBuffers[currentFrame].bindIndexBuffer(
    //         *indexBuffer, 0, vk::IndexTypeValue<decltype(indices)::value_type>::value); // vk::IndexType::eUint32

    //     commandBuffers[currentFrame].bindDescriptorSets(vk::PipelineBindPoint::eGraphics,
    //         pipelineLayout, 0, *descriptorSets[currentFrame], nullptr);

    //     commandBuffers[currentFrame].drawIndexed(indices.size(), 1, 0, 0, 0);

    //     // commandBuffers[currentFrame].draw(3, 1, 0, 0);
    //     commandBuffers[currentFrame].endRendering();
    //     // After rendering, transition the swapchain image to PRESENT_SRC
    //     transition_image_layout(
    //         swapChainImages[imageIndex],
    //         vk::ImageLayout::eColorAttachmentOptimal,
    //         vk::ImageLayout::ePresentSrcKHR,
    //         vk::AccessFlagBits2::eColorAttachmentWrite, // srcAccessMask
    //         {}, // dstAccessMask
    //         vk::PipelineStageFlagBits2::eColorAttachmentOutput, // srcStage
    //         vk::PipelineStageFlagBits2::eBottomOfPipe, // dstStage
    //         vk::ImageAspectFlagBits::eColor);
    //     commandBuffers[currentFrame].end();
    // }

    // void transition_image_layout(
    //     vk::Image image,
    //     vk::ImageLayout old_layout,
    //     vk::ImageLayout new_layout,
    //     vk::AccessFlags2 src_access_mask,
    //     vk::AccessFlags2 dst_access_mask,
    //     vk::PipelineStageFlags2 src_stage_mask,
    //     vk::PipelineStageFlags2 dst_stage_mask,
    //     vk::ImageAspectFlagBits image_aspect_flags)
    // {
    //     vk::ImageMemoryBarrier2 barrier = vk::ImageMemoryBarrier2()
    //                                           .setDstStageMask(dst_stage_mask)
    //                                           .setSrcStageMask(src_stage_mask)
    //                                           .setSrcAccessMask(src_access_mask)
    //                                           .setDstAccessMask(dst_access_mask)
    //                                           .setOldLayout(old_layout)
    //                                           .setNewLayout(new_layout)
    //                                           .setSrcQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
    //                                           .setDstQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
    //                                           .setImage(image)
    //                                           .setSubresourceRange(
    //                                               vk::ImageSubresourceRange()
    //                                                   .setAspectMask(image_aspect_flags)
    //                                                   .setBaseMipLevel(0)
    //                                                   .setLevelCount(1)
    //                                                   .setBaseArrayLayer(0)
    //                                                   .setLayerCount(1));

    //     vk::DependencyInfo dependency_info = vk::DependencyInfo()
    //                                              .setDependencyFlags({})
    //                                              .setImageMemoryBarrierCount(1)
    //                                              .setPImageMemoryBarriers(&barrier);

    //     commandBuffers[currentFrame].pipelineBarrier2(dependency_info);
    // }

    void createSyncObjects()
    {
        presentCompleteSemaphores.clear();
        renderFinishedSemaphores.clear();
        inFlightFences.clear();

        for (size_t i = 0; i < swapChainImages.size(); i++) {
            presentCompleteSemaphores.emplace_back(device, vk::SemaphoreCreateInfo());
            renderFinishedSemaphores.emplace_back(device, vk::SemaphoreCreateInfo());
        }

        for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
            inFlightFences.emplace_back(
                device, vk::FenceCreateInfo().setFlags(vk::FenceCreateFlagBits::eSignaled));
        }
    }

    void endSingleTimeCommands(vk::raii::CommandBuffer& commandBuffer)
    {
        commandBuffer.end();

        vk::SubmitInfo submitInfo = vk::SubmitInfo()
                                        .setCommandBufferCount(1)
                                        .setPCommandBuffers(&*commandBuffer);
        queue.submit(submitInfo, nullptr);
        queue.waitIdle();
    }

    void copyBuffer(vk::raii::Buffer& srcBuffer, vk::raii::Buffer& dstBuffer, vk::DeviceSize size)
    {
        vk::CommandBufferAllocateInfo allocInfo = vk::CommandBufferAllocateInfo()
                                                      .setCommandPool(commandPool)
                                                      .setLevel(vk::CommandBufferLevel::ePrimary)
                                                      .setCommandBufferCount(1);
        vk::raii::CommandBuffer commandCopyBuffer = std::move(device.allocateCommandBuffers(allocInfo).front());
        commandCopyBuffer.begin(vk::CommandBufferBeginInfo().setFlags(vk::CommandBufferUsageFlagBits::eOneTimeSubmit));
        commandCopyBuffer.copyBuffer(*srcBuffer, *dstBuffer, vk::BufferCopy().setSize(size));
        commandCopyBuffer.end();
        queue.submit(vk::SubmitInfo().setCommandBufferCount(1).setPCommandBuffers(&*commandCopyBuffer), nullptr);
        queue.waitIdle();
    }

    void drawFrame()
    {
        while (vk::Result::eTimeout == device.waitForFences(*inFlightFences[currentFrame], vk::True, UINT64_MAX))
            ;
        device.resetFences(*inFlightFences[currentFrame]);

        auto [result, imageIndex] = swapChain.acquireNextImage(
            UINT64_MAX, *presentCompleteSemaphores[currentFrame], nullptr); // semaphoreIndex

        if (result == vk::Result::eErrorOutOfDateKHR) {
            recreateSwapChain();
            return;
        }

        if (result != vk::Result::eSuccess && result != vk::Result::eSuboptimalKHR) {
            throw std::runtime_error("Failed to acquire swap chain image!");
        }
        updateUniformBuffer(currentFrame);

        commandBuffers[currentFrame].reset();
        recordCommandBuffer(imageIndex);

        vk::PipelineStageFlags waitDestinationStageMask(
            vk::PipelineStageFlagBits::eColorAttachmentOutput);
        const vk::SubmitInfo submitInfo = vk::SubmitInfo()
                                              .setWaitSemaphoreCount(1)
                                              .setPWaitSemaphores(&*presentCompleteSemaphores[currentFrame]) // semaphoreIndex
                                              .setPWaitDstStageMask(&waitDestinationStageMask)
                                              .setCommandBufferCount(1)
                                              .setPCommandBuffers(&*commandBuffers[currentFrame])
                                              .setSignalSemaphoreCount(1)
                                              .setPSignalSemaphores(&*renderFinishedSemaphores[imageIndex]);

        queue.submit(submitInfo, *inFlightFences[currentFrame]);

        try {
            const vk::PresentInfoKHR presentInfoKHR = vk::PresentInfoKHR()
                                                          .setWaitSemaphoreCount(1)
                                                          .setSwapchainCount(1)
                                                          .setPWaitSemaphores(&*renderFinishedSemaphores[imageIndex])
                                                          .setPSwapchains(&*swapChain)
                                                          .setPImageIndices(&imageIndex);
            result = queue.presentKHR(presentInfoKHR);
            if (result == vk::Result::eErrorOutOfDateKHR || result == vk::Result::eSuboptimalKHR || framebufferResized) {
                framebufferResized = false;
                recreateSwapChain();
            } else if (result != vk::Result::eSuccess) {
                throw std::runtime_error("failed to present swap chain image!");
            }
        } catch (const vk::SystemError& e) {
            if (e.code().value() == static_cast<int>(vk::Result::eErrorOutOfDateKHR)) {
                recreateSwapChain();
                return;
            } else {
                throw;
            }
        }

        semaphoreIndex = (semaphoreIndex + 1) % presentCompleteSemaphores.size();
        currentFrame = (currentFrame + 1) % MAX_FRAMES_IN_FLIGHT;
    }

    [[nodiscard]] vk::raii::ShaderModule
    createShaderModule(const std::vector<char>& code) const
    {
        vk::ShaderModuleCreateInfo createInfo = vk::ShaderModuleCreateInfo()
                                                    .setCodeSize(code.size() * sizeof(char))
                                                    .setPCode(reinterpret_cast<const uint32_t*>(code.data()));

        vk::raii::ShaderModule shaderModule { device, createInfo };

        return shaderModule;
    }

    static uint32_t chooseSwapMinImageCount(
        vk::SurfaceCapabilitiesKHR const& surfaceCapabilities)
    {
        auto minImageCount = std::max(3u, surfaceCapabilities.minImageCount);
        if ((0 < surfaceCapabilities.maxImageCount) && (surfaceCapabilities.maxImageCount < minImageCount)) {
            minImageCount = surfaceCapabilities.maxImageCount;
        }
        return minImageCount;
    }

    static vk::SurfaceFormatKHR chooseSwapSurfaceFormat(
        const std::vector<vk::SurfaceFormatKHR>& availableFormats)
    {
        assert(!availableFormats.empty());
        const auto formatIt = std::ranges::find_if(availableFormats, [](const auto& format) {
            return format.format == vk::Format::eB8G8R8A8Srgb && format.colorSpace == vk::ColorSpaceKHR::eSrgbNonlinear;
        });
        return formatIt != availableFormats.end() ? *formatIt : availableFormats[0];
    }

    static vk::PresentModeKHR chooseSwapPresentMode(
        const std::vector<vk::PresentModeKHR>& availablePresentModes)
    {
        assert(std::ranges::any_of(availablePresentModes, [](auto presentMode) {
            return presentMode == vk::PresentModeKHR::eFifo;
        }));
        return std::ranges::any_of(availablePresentModes,
                   [](const vk::PresentModeKHR value) {
                       return vk::PresentModeKHR::eMailbox == value;
                   })
            ? vk::PresentModeKHR::eMailbox
            : vk::PresentModeKHR::eFifo;
    }

    vk::Extent2D
    chooseSwapExtent(const vk::SurfaceCapabilitiesKHR& capabilities)
    {
        if (capabilities.currentExtent.width != 0xFFFFFFFF) {
            return capabilities.currentExtent;
        }
        int width, height;
        glfwGetFramebufferSize(window, &width, &height);

        return { std::clamp<uint32_t>(width, capabilities.minImageExtent.width,
                     capabilities.maxImageExtent.width),
            std::clamp<uint32_t>(height, capabilities.minImageExtent.height,
                capabilities.maxImageExtent.height) };
    }

    [[nodiscard]] std::vector<const char*> getRequiredExtensions() const
    {
        // Get the required extensions from GLFW
        uint32_t glfwExtensionCount = 0;
        auto glfwExtensions = glfwGetRequiredInstanceExtensions(&glfwExtensionCount);
        std::vector extensions(glfwExtensions, glfwExtensions + glfwExtensionCount);

        // Check if the debug utils extension is available
        std::vector<vk::ExtensionProperties> props = context.enumerateInstanceExtensionProperties();
        bool debugUtilsAvailable = std::ranges::any_of(props,
            [](vk::ExtensionProperties const& ep) {
                return strcmp(ep.extensionName, vk::EXTDebugUtilsExtensionName) == 0;
            });

        // Always include the debug utils extension if available
        // This allows validation layers to be enabled via vulkanconfig
        if (debugUtilsAvailable) {
            extensions.push_back(vk::EXTDebugUtilsExtensionName);
            std::cout << "VK_EXT_debug_utils extension is available. Validation layers should work." << std::endl;
        } else {
            std::cout << "VK_EXT_debug_utils extension not available. Validation layers may not work." << std::endl;
        }

        return extensions;
    }

    static VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback(
        vk::DebugUtilsMessageSeverityFlagBitsEXT severity,
        vk::DebugUtilsMessageTypeFlagsEXT type,
        const vk::DebugUtilsMessengerCallbackDataEXT* pCallbackData, void*)
    {
        if (severity == vk::DebugUtilsMessageSeverityFlagBitsEXT::eError || severity == vk::DebugUtilsMessageSeverityFlagBitsEXT::eWarning) {
            std::cerr << "VALIDATION LAYER: TYPE " << to_string(type)
                      << " MSG: " << pCallbackData->pMessage << std::endl;
        }

        return vk::False;
    }

    static std::vector<char> readFile(const std::string& filename)
    {
        std::ifstream file(filename, std::ios::ate | std::ios::binary);
        if (!file.is_open()) {
            throw std::runtime_error("failed to open file!");
        }
        std::vector<char> buffer(file.tellg());
        file.seekg(0, std::ios::beg);
        file.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
        file.close();
        return buffer;
    }

    void cleanupSwapChain()
    {
        swapChainFramebuffers.clear();
        swapChainImageViews.clear();
        swapChain = nullptr;
    }

    void recreateSwapChain()
    {
        int width = 0, height = 0;
        glfwGetFramebufferSize(window, &width, &height);
        while (width == 0 || height == 0) {
            glfwGetFramebufferSize(window, &width, &height);
            glfwWaitEvents();
        }

        device.waitIdle();

        cleanupSwapChain();
        createSwapChain();
        createImageViews();

        // Recreate traditional render pass and framebuffers if dynamic rendering is not supported
        if (!appInfo.dynamicRenderingSupported) {
            createRenderPass();
            createFramebuffers();
        }

        createColorResources();
        createDepthResources();
    }

    static void framebufferResizeCallback(GLFWwindow* window, int width,
        int height)
    {
        auto app = reinterpret_cast<HelloTriangleApplication*>(
            glfwGetWindowUserPointer(window));
        app->framebufferResized = true;
    }

    void createVertexBuffer()
    {
        vk::DeviceSize bufferSize = sizeof(vertices[0]) * vertices.size();

        vk::BufferCreateInfo stagingInfo = vk::BufferCreateInfo()
                                               .setSize(bufferSize)
                                               .setUsage(vk::BufferUsageFlagBits::eTransferSrc)
                                               .setSharingMode(vk::SharingMode::eExclusive);

        vk::raii::Buffer stagingBuffer(device, stagingInfo);
        vk::MemoryRequirements memRequirementsStaging = stagingBuffer.getMemoryRequirements();

        vk::MemoryAllocateInfo memoryAllocateInfoStaging = vk::MemoryAllocateInfo()
                                                               .setAllocationSize(memRequirementsStaging.size)
                                                               .setMemoryTypeIndex(
                                                                   findMemoryType(memRequirementsStaging.memoryTypeBits,
                                                                       vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent));

        vk::raii::DeviceMemory stagingBufferMemory(device,
            memoryAllocateInfoStaging);

        stagingBuffer.bindMemory(stagingBufferMemory, 0);
        void* dataStaging = stagingBufferMemory.mapMemory(0, stagingInfo.size);
        memcpy(dataStaging, vertices.data(), stagingInfo.size);
        stagingBufferMemory.unmapMemory();

        vk::BufferCreateInfo bufferInfo = vk::BufferCreateInfo()
                                              .setSize(bufferSize)
                                              .setUsage(vk::BufferUsageFlagBits::eVertexBuffer | vk::BufferUsageFlagBits::eTransferDst)
                                              .setSharingMode(vk::SharingMode::eExclusive);

        vertexBuffer = vk::raii::Buffer(device, bufferInfo);
        vk::MemoryRequirements memRequirements = vertexBuffer.getMemoryRequirements();

        vk::MemoryAllocateInfo memoryAllocateInfo = vk::MemoryAllocateInfo()
                                                        .setAllocationSize(memRequirements.size)
                                                        .setMemoryTypeIndex(
                                                            findMemoryType(memRequirements.memoryTypeBits,
                                                                vk::MemoryPropertyFlagBits::eDeviceLocal));

        vertexBufferMemory = vk::raii::DeviceMemory(device, memoryAllocateInfo);
        vertexBuffer.bindMemory(*vertexBufferMemory, 0);

        copyBuffer(stagingBuffer, vertexBuffer, bufferSize);
    }

    uint32_t findMemoryType(uint32_t typeFilter,
        vk::MemoryPropertyFlags properties)
    {

        vk::PhysicalDeviceMemoryProperties memProperties = physicalDevice.getMemoryProperties();
        for (uint32_t i = 0; i < memProperties.memoryTypeCount; i++) {
            if ((typeFilter & (1 << i)) && (memProperties.memoryTypes[i].propertyFlags & properties) == properties) {
                return i;
            }
        }

        throw std::runtime_error("Failed to find suitable memory type!");
    }

    void createBuffer(vk::DeviceSize size, vk::BufferUsageFlags usage,
        vk::MemoryPropertyFlags properties,
        vk::raii::Buffer& buffer,
        vk::raii::DeviceMemory& bufferMemory)
    {
        vk::BufferCreateInfo bufferInfo = vk::BufferCreateInfo().setSize(size).setUsage(usage).setSharingMode(
            vk::SharingMode::eExclusive);
        buffer = vk::raii::Buffer(device, bufferInfo);

        vk::MemoryRequirements memRequirements = buffer.getMemoryRequirements();
        vk::MemoryAllocateInfo allocInfo = vk::MemoryAllocateInfo()
                                               .setAllocationSize(memRequirements.size)
                                               .setMemoryTypeIndex(
                                                   findMemoryType(memRequirements.memoryTypeBits, properties));
        bufferMemory = vk::raii::DeviceMemory(device, allocInfo);

        buffer.bindMemory(*bufferMemory, 0);
    }

    void createIndexBuffer()
    {
        vk::DeviceSize bufferSize = sizeof(indices[0]) * indices.size();

        vk::raii::Buffer stagingBuffer({});
        vk::raii::DeviceMemory stagingBufferMemory({});
        createBuffer(bufferSize, vk::BufferUsageFlagBits::eTransferSrc,
            vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent,
            stagingBuffer, stagingBufferMemory);

        void* data = stagingBufferMemory.mapMemory(0, bufferSize);
        memcpy(data, indices.data(), (size_t)bufferSize);
        stagingBufferMemory.unmapMemory();

        createBuffer(bufferSize,
            vk::BufferUsageFlagBits::eTransferDst | vk::BufferUsageFlagBits::eIndexBuffer,
            vk::MemoryPropertyFlagBits::eDeviceLocal, indexBuffer,
            indexBufferMemory);

        copyBuffer(stagingBuffer, indexBuffer, bufferSize);
    }

    void createDescriptorSetLayout()
    {
        vk::DescriptorSetLayoutBinding uboLayoutBinding(0, vk::DescriptorType::eUniformBuffer,
            1, vk::ShaderStageFlagBits::eVertex, nullptr);

        std::array bindings = {
            vk::DescriptorSetLayoutBinding(0, vk::DescriptorType::eUniformBuffer, 1, vk::ShaderStageFlagBits::eVertex, nullptr),
            vk::DescriptorSetLayoutBinding(1, vk::DescriptorType::eCombinedImageSampler, 1, vk::ShaderStageFlagBits::eFragment, nullptr)
        };

        vk::DescriptorSetLayoutCreateInfo layoutInfo = vk::DescriptorSetLayoutCreateInfo()
                                                           .setBindingCount(bindings.size())
                                                           .setPBindings(bindings.data());
        descriptorSetLayout = vk::raii::DescriptorSetLayout(device, layoutInfo);
    }

    void createUniformBuffers()
    {
        uniformBuffers.clear();
        uniformBuffersMemory.clear();
        uniformBuffersMapped.clear();

        for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
            vk::DeviceSize bufferSize = sizeof(UniformBufferObject);
            vk::raii::Buffer buffer({});
            vk::raii::DeviceMemory bufferMem({});
            createBuffer(bufferSize, vk::BufferUsageFlagBits::eUniformBuffer, vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent, buffer, bufferMem);

            uniformBuffers.emplace_back(std::move(buffer));
            uniformBuffersMemory.emplace_back(std::move(bufferMem));
            uniformBuffersMapped.emplace_back(uniformBuffersMemory[i].mapMemory(0, bufferSize));
        }
    }

    void updateUniformBuffer(uint32_t currentImage)
    {
        static auto startTime = std::chrono::high_resolution_clock::now();

        auto currentTime = std::chrono::high_resolution_clock::now();
        float time = std::chrono::duration<float, std::chrono::seconds::period>(currentTime - startTime).count();

        UniformBufferObject ubo {};
        ubo.model = rotate(glm::mat4(1.0f), time * glm::radians(90.0f), glm::vec3(0.0f, 0.0f, 1.0f));
        ubo.view = lookAt(glm::vec3(2.0f, 2.0f, 2.0f), glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f, 0.0f, 1.0f));
        ubo.proj = glm::perspective(glm::radians(45.0f), static_cast<float>(swapChainExtent.width) / static_cast<float>(swapChainExtent.height), 0.1f, 100.0f);
        ubo.proj[1][1] *= -1;

        memcpy(uniformBuffersMapped[currentImage], &ubo, sizeof(ubo));
    }

    void createDescriptorPool()
    {

        std::array poolSize {
            vk::DescriptorPoolSize(vk::DescriptorType::eUniformBuffer, MAX_FRAMES_IN_FLIGHT),
            vk::DescriptorPoolSize(vk::DescriptorType::eCombinedImageSampler, MAX_FRAMES_IN_FLIGHT)
        };

        vk::DescriptorPoolCreateInfo poolInfo = vk::DescriptorPoolCreateInfo()
                                                    .setFlags(vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet)
                                                    .setMaxSets(MAX_FRAMES_IN_FLIGHT)
                                                    .setPoolSizeCount(static_cast<uint32_t>(poolSize.size()))
                                                    .setPPoolSizes(poolSize.data());
        descriptorPool = vk::raii::DescriptorPool(device, poolInfo);
    }

    void createDescriptorSets()
    {
        std::vector<vk::DescriptorSetLayout> layouts(MAX_FRAMES_IN_FLIGHT, *descriptorSetLayout);
        vk::DescriptorSetAllocateInfo allocInfo = vk::DescriptorSetAllocateInfo()
                                                      .setDescriptorPool(descriptorPool)
                                                      .setDescriptorSetCount(static_cast<uint32_t>(layouts.size()))
                                                      .setPSetLayouts(layouts.data());

        descriptorSets.clear();
        descriptorSets = device.allocateDescriptorSets(allocInfo);

        for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
            vk::DescriptorBufferInfo bufferInfo = vk::DescriptorBufferInfo()
                                                      .setBuffer(uniformBuffers[i])
                                                      .setOffset(0)
                                                      .setRange(sizeof(UniformBufferObject));
            vk::DescriptorImageInfo imageInfo = vk::DescriptorImageInfo()
                                                    .setSampler(textureSampler)
                                                    .setImageView(textureImageView)
                                                    .setImageLayout(vk::ImageLayout::eShaderReadOnlyOptimal);

            std::array descriptorWrites {
                vk::WriteDescriptorSet()
                    .setDstSet(descriptorSets[i])
                    .setDstBinding(0)
                    .setDstArrayElement(0)
                    .setDescriptorCount(1)
                    .setDescriptorType(vk::DescriptorType::eUniformBuffer)
                    .setPBufferInfo(&bufferInfo),
                vk::WriteDescriptorSet()
                    .setDstSet(descriptorSets[i])
                    .setDstBinding(1)
                    .setDstArrayElement(0)
                    .setDescriptorCount(1)
                    .setDescriptorType(vk::DescriptorType::eCombinedImageSampler)
                    .setPImageInfo(&imageInfo)
            };

            device.updateDescriptorSets(descriptorWrites, {});
        }
    }

    void createImage(uint32_t width, uint32_t height, uint32_t mipLevels, vk::SampleCountFlagBits numSamples, vk::Format format, vk::ImageTiling tiling, vk::ImageUsageFlags usage, vk::MemoryPropertyFlags properties, vk::raii::Image& image, vk::raii::DeviceMemory& imageMemory)
    {
        vk::ImageCreateInfo imageInfo = vk::ImageCreateInfo()
                                            .setImageType(vk::ImageType::e2D)
                                            .setSamples(numSamples)
                                            .setFormat(format)
                                            .setExtent({ width, height, 1 })
                                            .setMipLevels(mipLevels)
                                            .setArrayLayers(1)
                                            .setSamples(vk::SampleCountFlagBits::e1)
                                            .setTiling(tiling)
                                            .setUsage(usage)
                                            .setSharingMode(vk::SharingMode::eExclusive)
                                            .setInitialLayout(vk::ImageLayout::eUndefined);

        image = vk::raii::Image(device, imageInfo);

        vk::MemoryRequirements memRequirements = image.getMemoryRequirements();
        vk::MemoryAllocateInfo allocInfo = vk::MemoryAllocateInfo()
                                               .setAllocationSize(memRequirements.size)
                                               .setMemoryTypeIndex(findMemoryType(memRequirements.memoryTypeBits, properties));

        imageMemory = vk::raii::DeviceMemory(device, allocInfo);
        image.bindMemory(imageMemory, 0);
    };

    void createTextureImage()
    {
        int texWidth, texHeight, texChannels;
        stbi_uc* pixels = stbi_load(TEXTURE_PATH.c_str(), &texWidth, &texHeight, &texChannels, STBI_rgb_alpha);
        vk::DeviceSize imageSize = texWidth * texHeight * 4;
        mipLevels = static_cast<uint32_t>(std::floor(std::log2(std::max(texWidth, texHeight)))) + 1;
        // stbi_uc* pixels = stbi_load("textures/texture.jpg", &texWidth, &texHeight, &texChannels, STBI_rgb_alpha);

        if (!pixels) {
            std::cerr << "STB failed: " << stbi_failure_reason() << std::endl;
            throw std::runtime_error("Failed to load texture image!");
        }

        vk::raii::Buffer stagingBuffer({});
        vk::raii::DeviceMemory stagingBufferMemory({});
        createBuffer(imageSize, vk::BufferUsageFlagBits::eTransferSrc, vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent, stagingBuffer, stagingBufferMemory);

        void* data = stagingBufferMemory.mapMemory(0, imageSize);
        memcpy(data, pixels, imageSize);
        stagingBufferMemory.unmapMemory();

        stbi_image_free(pixels);

        createImage(texWidth, texHeight, mipLevels, vk::SampleCountFlagBits::e1, vk::Format::eR8G8B8A8Srgb, vk::ImageTiling::eOptimal,
            vk::ImageUsageFlagBits::eTransferSrc | vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eSampled, vk::MemoryPropertyFlagBits::eDeviceLocal,
            textureImage, textureImageMemory);

        transitionImageLayout(textureImage, vk::ImageLayout::eUndefined, vk::ImageLayout::eTransferDstOptimal, mipLevels);
        copyBufferToImage(stagingBuffer, textureImage, static_cast<uint32_t>(texWidth), static_cast<uint32_t>(texHeight));

        generateMipmaps(textureImage, vk::Format::eR8G8B8A8Srgb, texWidth, texHeight, mipLevels);

        // transitionImageLayout(textureImage, vk::ImageLayout::eTransferDstOptimal, vk::ImageLayout::eShaderReadOnlyOptimal, mipLevels);
    }

    void generateMipmaps(vk::raii::Image& image, vk::Format imageFormat, int32_t texWidth, int32_t texHeight, uint32_t mipLevels)
    {
        // Check if image format supports linear blit-ing
        vk::FormatProperties formatProperties = physicalDevice.getFormatProperties(imageFormat);

        if (!(formatProperties.optimalTilingFeatures & vk::FormatFeatureFlagBits::eSampledImageFilterLinear)) {
            throw std::runtime_error("texture image format does not support linear blitting!");
        }

        std::unique_ptr<vk::raii::CommandBuffer> commandBuffer = beginSingleTimeCommands();

        vk::ImageMemoryBarrier barrier = vk::ImageMemoryBarrier()
                                             .setSrcAccessMask(vk::AccessFlagBits::eTransferWrite)
                                             .setDstAccessMask(vk::AccessFlagBits::eTransferRead)
                                             .setOldLayout(vk::ImageLayout::eTransferDstOptimal)
                                             .setNewLayout(vk::ImageLayout::eTransferSrcOptimal)
                                             .setSrcQueueFamilyIndex(vk::QueueFamilyIgnored)
                                             .setDstQueueFamilyIndex(vk::QueueFamilyIgnored)
                                             .setImage(image)
                                             .setSubresourceRange(vk::ImageSubresourceRange()
                                                     .setAspectMask(vk::ImageAspectFlagBits::eColor)
                                                     .setBaseArrayLayer(0)
                                                     .setLayerCount(1)
                                                     .setLevelCount(1));

        int32_t mipWidth = texWidth;
        int32_t mipHeigth = texHeight;

        for (uint32_t i = 1; i < mipLevels; i++) {
            barrier.setSubresourceRange(vk::ImageSubresourceRange()
                                            .setBaseMipLevel(i - 1))
                .setOldLayout(vk::ImageLayout::eTransferDstOptimal)
                .setNewLayout(vk::ImageLayout::eTransferSrcOptimal)
                .setSrcAccessMask(vk::AccessFlagBits::eTransferWrite)
                .setDstAccessMask(vk::AccessFlagBits::eTransferRead);

            commandBuffer->pipelineBarrier(vk::PipelineStageFlagBits::eTransfer, vk::PipelineStageFlagBits::eTransfer, {}, {}, {}, barrier);

            vk::ArrayWrapper1D<vk::Offset3D, 2> offsets, dstOffsets;
            offsets[0] = vk::Offset3D(0, 0, 0);
            offsets[1] = vk::Offset3D(mipWidth, mipHeigth, 1);
            dstOffsets[0] = vk::Offset3D(0, 0, 0);
            dstOffsets[1] = vk::Offset3D(
                mipWidth > 1 ? mipWidth / 2 : 1,
                mipHeigth > 1 ? mipHeigth / 2 : 1,
                1);

            vk::ImageBlit blit = vk::ImageBlit()
                                     .setSrcSubresource({})
                                     .setSrcOffsets(offsets)
                                     .setDstSubresource({})
                                     .setDstOffsets(dstOffsets)
                                     .setSrcSubresource(
                                         vk::ImageSubresourceLayers(
                                             vk::ImageAspectFlagBits::eColor, i - 1, 0, 1))
                                     .setDstSubresource(
                                         vk::ImageSubresourceLayers(
                                             vk::ImageAspectFlagBits::eColor, i, 0, 1));

            commandBuffer->blitImage(image, vk::ImageLayout::eTransferSrcOptimal,
                image, vk::ImageLayout::eTransferDstOptimal, { blit }, vk::Filter::eLinear);

            barrier.setOldLayout(vk::ImageLayout::eTransferSrcOptimal)
                .setNewLayout(vk::ImageLayout::eShaderReadOnlyOptimal)
                .setSrcAccessMask(vk::AccessFlagBits::eTransferRead)
                .setDstAccessMask(vk::AccessFlagBits::eShaderRead);

            commandBuffer->pipelineBarrier(vk::PipelineStageFlagBits::eTransfer, vk::PipelineStageFlagBits::eFragmentShader, {}, {}, {}, barrier);

            if (mipWidth > 1) {
                mipWidth /= 2;
            }
            if (mipHeigth > 1) {
                mipHeigth /= 2;
            }
        }

        barrier.setSubresourceRange(vk::ImageSubresourceRange().setBaseMipLevel(mipLevels - 1))
            .setOldLayout(vk::ImageLayout::eTransferDstOptimal)
            .setNewLayout(vk::ImageLayout::eShaderReadOnlyOptimal)
            .setSrcAccessMask(vk::AccessFlagBits::eTransferWrite)
            .setDstAccessMask(vk::AccessFlagBits::eShaderRead);

        commandBuffer->pipelineBarrier(vk::PipelineStageFlagBits::eTransfer, vk::PipelineStageFlagBits::eFragmentShader, {}, {}, {}, barrier);

        endSingleTimeCommands(*commandBuffer);
    }

    std::unique_ptr<vk::raii::CommandBuffer> beginSingleTimeCommands()
    {
        vk::CommandBufferAllocateInfo allocInfo = vk::CommandBufferAllocateInfo()
                                                      .setCommandPool(commandPool)
                                                      .setLevel(vk::CommandBufferLevel::ePrimary)
                                                      .setCommandBufferCount(1);

        std::unique_ptr<vk::raii::CommandBuffer> commandBuffer = std::make_unique<vk::raii::CommandBuffer>(std::move(device.allocateCommandBuffers(allocInfo).front()));

        vk::CommandBufferBeginInfo beginInfo = vk::CommandBufferBeginInfo()
                                                   .setFlags(vk::CommandBufferUsageFlagBits::eOneTimeSubmit);
        commandBuffer->begin(beginInfo);

        return commandBuffer;
    }

    void transitionImageLayout(const vk::raii::Image& image, vk::ImageLayout oldLayout, vk::ImageLayout newLayout, uint32_t mipLevels)
    {
        auto commandBuffer = beginSingleTimeCommands();

        vk::ImageMemoryBarrier barrier = vk::ImageMemoryBarrier()
                                             .setOldLayout(oldLayout)
                                             .setNewLayout(newLayout)
                                             .setImage(image)
                                             .setSubresourceRange(vk::ImageSubresourceRange()
                                                     .setAspectMask(vk::ImageAspectFlagBits::eColor)
                                                     .setBaseMipLevel(0)
                                                     .setLevelCount(mipLevels)
                                                     .setBaseArrayLayer(0)
                                                     .setLayerCount(1));

        vk::PipelineStageFlags sourceStage;
        vk::PipelineStageFlags destinationStage;

        if (oldLayout == vk::ImageLayout::eUndefined && newLayout == vk::ImageLayout::eTransferDstOptimal) {
            barrier.srcAccessMask = {};
            barrier.dstAccessMask = vk::AccessFlagBits::eTransferWrite;

            sourceStage = vk::PipelineStageFlagBits::eTopOfPipe;
            destinationStage = vk::PipelineStageFlagBits::eTransfer;
        } else if (oldLayout == vk::ImageLayout::eTransferDstOptimal && newLayout == vk::ImageLayout::eShaderReadOnlyOptimal) {
            barrier.srcAccessMask = vk::AccessFlagBits::eTransferWrite;
            barrier.dstAccessMask = vk::AccessFlagBits::eShaderRead;

            sourceStage = vk::PipelineStageFlagBits::eTransfer;
            destinationStage = vk::PipelineStageFlagBits::eFragmentShader;
        } else {
            throw std::invalid_argument("unsupported layout transition!");
        }
        commandBuffer->pipelineBarrier(sourceStage, destinationStage, {}, {}, nullptr, barrier);
        endSingleTimeCommands(*commandBuffer);
    }

    void copyBufferToImage(const vk::raii::Buffer& buffer, vk::raii::Image& image, uint32_t width, uint32_t height)
    {
        std::unique_ptr<vk::raii::CommandBuffer> commandBuffer = beginSingleTimeCommands();
        vk::BufferImageCopy region = vk::BufferImageCopy()
                                         .setBufferOffset(0)
                                         .setBufferRowLength(0)
                                         .setBufferImageHeight(0)
                                         .setImageSubresource({ vk::ImageAspectFlagBits::eColor, 0, 0, 1 })
                                         .setImageOffset({ 0, 0, 0 })
                                         .setImageExtent({ width, height, 1 });

        commandBuffer->copyBufferToImage(buffer, image, vk::ImageLayout::eTransferDstOptimal, { region });
        endSingleTimeCommands(*commandBuffer);
    }

    void createTextureImageView()
    {
        textureImageView = createImageView(textureImage, vk::Format::eR8G8B8A8Srgb, vk::ImageAspectFlagBits::eColor, mipLevels);
    }

    vk::raii::ImageView createImageView(vk::raii::Image& image, vk::Format format, vk::ImageAspectFlags aspectFlags, uint32_t mipLevels)
    {

        vk::ImageViewCreateInfo viewInfo = vk::ImageViewCreateInfo()
                                               .setImage(image)
                                               .setViewType(vk::ImageViewType::e2D)
                                               .setFormat(format)
                                               .setSubresourceRange(vk::ImageSubresourceRange()
                                                       .setAspectMask(aspectFlags)
                                                       .setBaseMipLevel(0)
                                                       .setLevelCount(mipLevels)
                                                       .setBaseArrayLayer(0)
                                                       .setLayerCount(1));
        return vk::raii::ImageView(device, viewInfo);
    }

    void createTextureSampler()
    {
        vk::PhysicalDeviceProperties properties = physicalDevice.getProperties();
        vk::SamplerCreateInfo samplerInfo = vk::SamplerCreateInfo()
                                                .setMagFilter(vk::Filter::eLinear)
                                                .setMinFilter(vk::Filter::eLinear)
                                                // .setMinLod(static_cast<float>(mipLevels - 1))
                                                .setMipmapMode(vk::SamplerMipmapMode::eLinear)
                                                .setAddressModeU(vk::SamplerAddressMode::eRepeat)
                                                .setAddressModeV(vk::SamplerAddressMode::eRepeat)
                                                .setAddressModeW(vk::SamplerAddressMode::eRepeat)
                                                .setMipLodBias(0.0f)
                                                .setAnisotropyEnable(vk::True)
                                                .setMaxAnisotropy(properties.limits.maxSamplerAnisotropy)
                                                .setCompareEnable(vk::False)
                                                .setCompareOp(vk::CompareOp::eAlways);

        textureSampler = vk::raii::Sampler(device, samplerInfo);
    }

    void createDepthResources()
    {
        vk::Format depthFormat = findDepthFormat();
        createImage(swapChainExtent.width, swapChainExtent.height, 1, msaaSamples, depthFormat,
            vk::ImageTiling::eOptimal, vk::ImageUsageFlagBits::eDepthStencilAttachment,
            vk::MemoryPropertyFlagBits::eDeviceLocal, depthImage, depthImageMemory);
        depthImageView = createImageView(depthImage, depthFormat, vk::ImageAspectFlagBits::eDepth, 1);
    }

    vk::Format findSupportedFormat(const std::vector<vk::Format>& candidates, vk::ImageTiling tiling, vk::FormatFeatureFlags features)
    {
        auto formatIt = std::ranges::find_if(candidates, [&](auto const format) {
            vk::FormatProperties props = physicalDevice.getFormatProperties(format);
            return (((tiling == vk::ImageTiling::eLinear) && ((props.linearTilingFeatures & features) == features)) || ((tiling == vk::ImageTiling::eOptimal) && ((props.optimalTilingFeatures & features) == features)));
        });
        if (formatIt == candidates.end()) {
            throw std::runtime_error("failed to find supported format!");
        }
        return *formatIt;
    }

    vk::Format findDepthFormat()
    {
        return findSupportedFormat(
            { vk::Format::eD32Sfloat,
                vk::Format::eD32SfloatS8Uint,
                vk::Format::eD24UnormS8Uint },
            vk::ImageTiling::eOptimal,
            vk::FormatFeatureFlagBits::eDepthStencilAttachment);
    }

    bool hasStencilComponent(vk::Format format)
    {
        return format == vk::Format::eD32SfloatS8Uint || format == vk::Format::eD24UnormS8Uint;
    }

    void loadModel()
    {

        tinyobj::attrib_t attrib;
        std::vector<tinyobj::shape_t> shapes;
        std::vector<tinyobj::material_t> materials;
        std::string warn, err;

        if (!tinyobj::LoadObj(&attrib, &shapes, &materials, &warn, &err, MODEL_PATH.c_str())) {
            throw std::runtime_error(warn + err);
        }

        for (const auto& shape : shapes) {
            for (const auto& index : shape.mesh.indices) {
                Vertex vertex {};

                vertex.pos = {
                    attrib.vertices[3 * index.vertex_index + 0],
                    attrib.vertices[3 * index.vertex_index + 1],
                    attrib.vertices[3 * index.vertex_index + 2],
                };

                vertex.texCoord = {
                    attrib.texcoords[2 * index.texcoord_index + 0],
                    1.0f - attrib.texcoords[2 * index.texcoord_index + 1]
                };

                vertex.color = { 1.0f, 1.0f, 1.0f };

                if (uniqueVertices.count(vertex) == 0) {
                    uniqueVertices[vertex] = static_cast<uint32_t>(vertices.size());
                    vertices.push_back(vertex);
                }
                indices.push_back(uniqueVertices[vertex]);

                // vertices.push_back(vertex);
                // indices.push_back(indices.size());
            }
        }
    }

    vk::SampleCountFlagBits getMaxUsableSampleCount()
    {
        vk::PhysicalDeviceProperties physicalDeviceProperties = physicalDevice.getProperties();

        vk::SampleCountFlags counts = physicalDeviceProperties.limits.framebufferColorSampleCounts & physicalDeviceProperties.limits.framebufferDepthSampleCounts;
        if (counts & vk::SampleCountFlagBits::e64)
            return vk::SampleCountFlagBits::e64;
        if (counts & vk::SampleCountFlagBits::e32)
            return vk::SampleCountFlagBits::e32;
        if (counts & vk::SampleCountFlagBits::e16)
            return vk::SampleCountFlagBits::e16;
        if (counts & vk::SampleCountFlagBits::e8)
            return vk::SampleCountFlagBits::e8;
        if (counts & vk::SampleCountFlagBits::e4)
            return vk::SampleCountFlagBits::e4;
        if (counts & vk::SampleCountFlagBits::e2)
            return vk::SampleCountFlagBits::e2;

        return vk::SampleCountFlagBits::e1;
    }

    void createColorResources()
    {
        vk::Format colorFormat = swapChainSurfaceFormat.format;

        createImage(swapChainExtent.width, swapChainExtent.height, 1, msaaSamples, colorFormat, vk::ImageTiling::eOptimal, vk::ImageUsageFlagBits::eTransientAttachment | vk::ImageUsageFlagBits::eColorAttachment, vk::MemoryPropertyFlagBits::eDeviceLocal, colorImage, colorImageMemory);
        colorImageView = createImageView(colorImage, colorFormat, vk::ImageAspectFlagBits::eColor, 1);
    }

    void detectFeatureSupport()
    {
        // Get device properties to check Vulkan version
        vk::PhysicalDeviceProperties deviceProperties = physicalDevice.getProperties();
        // Get available extensions
        std::vector<vk::ExtensionProperties> availableExtensions = physicalDevice.enumerateDeviceExtensionProperties();

        // Check for dynamic rendering support
        if (deviceProperties.apiVersion >= VK_VERSION_1_3) {
            appInfo.dynamicRenderingSupported = true;
            std::cout << "Dynamic rendering supported via Vulkan 1.3\n";
        } else {
            // Check for the extension on older Vulkan versions
            for (const auto& extension : availableExtensions) {
                if (strcmp(extension.extensionName, VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME) == 0) {
                    appInfo.dynamicRenderingSupported = true;
                    std::cout << "Dynamic rendering supported via extension\n";
                    break;
                }
            }
        }

        // Check for timeline semaphores support
        if (deviceProperties.apiVersion >= VK_VERSION_1_2) {
            appInfo.timelineSemaphoresSupported = true;
            std::cout << "Timeline semaphores supported via Vulkan 1.2\n";
        } else {
            // Check for the extension on older Vulkan versions
            for (const auto& extension : availableExtensions) {
                if (strcmp(extension.extensionName, VK_KHR_TIMELINE_SEMAPHORE_EXTENSION_NAME) == 0) {
                    appInfo.timelineSemaphoresSupported = true;
                    std::cout << "Timeline semaphores supported via extension\n";
                    break;
                }
            }
        }

        // Check for synchronization2 support
        if (deviceProperties.apiVersion >= VK_VERSION_1_3) {
            appInfo.synchronization2Supported = true;
            std::cout << "Synchronization2 supported via Vulkan 1.3\n";
        } else {
            // Check for the extension on older Vulkan versions
            for (const auto& extension : availableExtensions) {
                if (strcmp(extension.extensionName, VK_KHR_SYNCHRONIZATION_2_EXTENSION_NAME) == 0) {
                    appInfo.synchronization2Supported = true;
                    std::cout << "Synchronization2 supported via extension\n";
                    break;
                }
            }
        }

        // Add required extensions based on feature support
        if (appInfo.dynamicRenderingSupported && deviceProperties.apiVersion < VK_VERSION_1_3) {
            requiredDeviceExtension.push_back(VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME);
        }

        if (appInfo.timelineSemaphoresSupported && deviceProperties.apiVersion < VK_VERSION_1_2) {
            requiredDeviceExtension.push_back(VK_KHR_TIMELINE_SEMAPHORE_EXTENSION_NAME);
        }

        if (appInfo.synchronization2Supported && deviceProperties.apiVersion < VK_VERSION_1_3) {
            requiredDeviceExtension.push_back(VK_KHR_SYNCHRONIZATION_2_EXTENSION_NAME);
        }
    }

    void createRenderPass()
    {
        if (appInfo.dynamicRenderingSupported) {
            // No render pass needed with dynamic rendering
            std::cout << "Using dynamic rendering, skipping render pass creation\n";
            return;
        }

        std::cout << "Creating traditional render pass\n";

        // Color attachment description
        vk::AttachmentDescription colorAttachment = vk::AttachmentDescription()
                                                        .setFormat(swapChainSurfaceFormat.format)
                                                        .setSamples(msaaSamples)
                                                        .setLoadOp(vk::AttachmentLoadOp::eClear)
                                                        .setStoreOp(vk::AttachmentStoreOp::eStore)
                                                        .setStencilLoadOp(vk::AttachmentLoadOp::eDontCare)
                                                        .setStencilStoreOp(vk::AttachmentStoreOp::eDontCare)
                                                        .setInitialLayout(vk::ImageLayout::eUndefined)
                                                        .setFinalLayout(vk::ImageLayout::eColorAttachmentOptimal);

        vk::AttachmentDescription depthAttachment = vk::AttachmentDescription()
                                                        .setFormat(findDepthFormat())
                                                        .setSamples(msaaSamples)
                                                        .setLoadOp(vk::AttachmentLoadOp::eClear)
                                                        .setStoreOp(vk::AttachmentStoreOp::eDontCare)
                                                        .setStencilLoadOp(vk::AttachmentLoadOp::eDontCare)
                                                        .setStencilStoreOp(vk::AttachmentStoreOp::eDontCare)
                                                        .setInitialLayout(vk::ImageLayout::eUndefined)
                                                        .setFinalLayout(vk::ImageLayout::eDepthStencilAttachmentOptimal);

        vk::AttachmentDescription colorAttachmentResolve = vk::AttachmentDescription()
                                                               .setFormat(swapChainSurfaceFormat.format)
                                                               .setSamples(vk::SampleCountFlagBits::e1)
                                                               .setLoadOp(vk::AttachmentLoadOp::eDontCare)
                                                               .setStoreOp(vk::AttachmentStoreOp::eStore)
                                                               .setStencilLoadOp(vk::AttachmentLoadOp::eDontCare)
                                                               .setStencilStoreOp(vk::AttachmentStoreOp::eDontCare)
                                                               .setInitialLayout(vk::ImageLayout::eUndefined)
                                                               .setFinalLayout(vk::ImageLayout::ePresentSrcKHR);

        // Subpass references
        vk::AttachmentReference colorAttachmentRef = vk::AttachmentReference()
                                                         .setAttachment(0)
                                                         .setLayout(vk::ImageLayout::eColorAttachmentOptimal);

        vk::AttachmentReference depthAttachmentRef = vk::AttachmentReference()
                                                         .setAttachment(1)
                                                         .setLayout(vk::ImageLayout::eDepthStencilAttachmentOptimal);

        vk::AttachmentReference colorAttachmentResolveRef = vk::AttachmentReference()
                                                                .setAttachment(2)
                                                                .setLayout(vk::ImageLayout::eColorAttachmentOptimal);

        // Subpass description
        vk::SubpassDescription subpass = vk::SubpassDescription()
                                             .setPipelineBindPoint(vk::PipelineBindPoint::eGraphics)
                                             .setColorAttachmentCount(1)
                                             .setPColorAttachments(&colorAttachmentRef)
                                             .setPResolveAttachments(&colorAttachmentResolveRef)
                                             .setPDepthStencilAttachment(&depthAttachmentRef);

        // Dependency to ensure proper image layout transitions
        vk::SubpassDependency dependency = vk::SubpassDependency()
                                               .setSrcSubpass(VK_SUBPASS_EXTERNAL)
                                               .setDstSubpass(0)
                                               .setSrcStageMask(vk::PipelineStageFlagBits::eColorAttachmentOutput | vk::PipelineStageFlagBits::eEarlyFragmentTests)
                                               .setDstStageMask(vk::PipelineStageFlagBits::eColorAttachmentOutput | vk::PipelineStageFlagBits::eEarlyFragmentTests)
                                               .setSrcAccessMask(vk::AccessFlagBits::eNone)
                                               .setDstAccessMask(vk::AccessFlagBits::eColorAttachmentWrite | vk::AccessFlagBits::eDepthStencilAttachmentWrite);

        // Create the render pass
        std::array attachments = { colorAttachment, depthAttachment, colorAttachmentResolve };
        vk::RenderPassCreateInfo renderPassInfo = vk::RenderPassCreateInfo()
                                                      .setAttachmentCount(static_cast<uint32_t>(attachments.size()))
                                                      .setPAttachments(attachments.data())
                                                      .setSubpassCount(1)
                                                      .setPSubpasses(&subpass)
                                                      .setDependencyCount(1)
                                                      .setPDependencies(&dependency);

        renderPass = vk::raii::RenderPass(device, renderPassInfo);
    }

    void createFramebuffers()
    {
        if (appInfo.dynamicRenderingSupported) {
            // No framebuffers needed with dynamic rendering
            std::cout << "Using dynamic rendering, skipping framebuffer creation\n";
            return;
        }

        std::cout << "Creating traditional framebuffers\n";

        swapChainFramebuffers.clear();

        for (size_t i = 0; i < swapChainImageViews.size(); i++) {
            std::array attachments = {
                *colorImageView,
                *depthImageView,
                *swapChainImageViews[i]
            };

            vk::FramebufferCreateInfo framebufferInfo = vk::FramebufferCreateInfo()
                                                            .setRenderPass(*renderPass)
                                                            .setAttachmentCount(static_cast<uint32_t>(attachments.size()))
                                                            .setPAttachments(attachments.data())
                                                            .setWidth(swapChainExtent.width)
                                                            .setHeight(swapChainExtent.height)
                                                            .setLayers(1);

            swapChainFramebuffers.emplace_back(device, framebufferInfo);
        }
    }
};

int main()
{
    try {
        HelloTriangleApplication app;
        app.run();
    } catch (const std::exception& e) {
        std::cerr << e.what() << std::endl;
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
