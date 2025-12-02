#if defined(__INTELLISENSE__) || !defined(USE_CPP20_MODULES)
#include <vulkan/vulkan_raii.hpp>
#else
import vulkan_hpp;
#endif
#define GLFW_INCLUDE_VULKAN // REQUIRED only for GLFW CreateWindowSurface.
#include <GLFW/glfw3.h>
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

// #define GLM_FORCE_DEFAULT_ALIGNED_GENTYPES
#include <algorithm>
#include <array>
#include <assert.h>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <iostream>
#include <limits>
#include <memory>
#include <stdexcept>
#include <vector>

constexpr uint32_t WIDTH = 800;
constexpr uint32_t HEIGHT = 600;

const std::vector<char const*> validationLayers = {
    "VK_LAYER_KHRONOS_validation"
};

#ifdef NDEBUG
constexpr bool enableValidationLayers = false;
#else
constexpr bool enableValidationLayers = true;
#endif

constexpr int MAX_FRAMES_IN_FLIGHT = 2;

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

    vk::raii::Image textureImage = nullptr;
    vk::raii::DeviceMemory textureImageMemory = nullptr;
    vk::raii::ImageView textureImageView = nullptr;
    vk::raii::Sampler textureSampler = nullptr;

    struct UniformBufferObject {
        // glm::vec2 foo; // alignment test
        // alignas(16) glm::mat4 model;
        glm::mat4 model;
        glm::mat4 view;
        glm::mat4 proj;
    };

    struct Vertex {
        glm::vec2 pos;
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
                vk::VertexInputAttributeDescription(0, 0, vk::Format::eR32G32Sfloat,
                    offsetof(Vertex, pos)),
                vk::VertexInputAttributeDescription(
                    1, 0, vk::Format::eR32G32B32A32Sfloat, offsetof(Vertex, color)),
                vk::VertexInputAttributeDescription(
                    2, 0, vk::Format::eR32G32Sfloat, offsetof(Vertex, texCoord))
            };
        }
    };

    const std::vector<Vertex> vertices = {
        { { -0.5f, -0.5f }, { 1.0f, 0.0f, 0.0f }, { 1.0f, 0.0f } },
        { { 0.5f, -0.5f }, { 0.0f, 1.0f, 0.0f }, { 0.0f, 0.0f } },
        { { 0.5f, 0.5f }, { 0.0f, 0.0f, 1.0f }, { 0.0f, 1.0f } },
        { { -0.5f, 0.5f }, { 1.0f, 1.0f, 1.0f }, { 1.0f, 1.0f } }
    };

    const std::vector<uint16_t> indices = { 0, 1, 2, 2, 3, 0 };

    uint32_t currentFrame = 0;
    uint32_t semaphoreIndex = 0;
    bool framebufferResized = false;

    std::vector<const char*> requiredDeviceExtension = {
        vk::KHRSwapchainExtensionName, vk::KHRSpirv14ExtensionName,
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
        createLogicalDevice();
        createSwapChain();
        createImageViews();
        createDescriptorSetLayout();
        createGraphicsPipeline();
        createCommandPool();
        createTextureImage();
        createTextureImageView();
        createTextureSampler();
        createVertexBuffer();
        createIndexBuffer();
        createUniformBuffers();
        createDescriptorPool();
        createDescriptorSets();
        createCommandBuffers();
        createSyncObjects();
    }

    void mainLoop()
    {
        while (!glfwWindowShouldClose(window)) {
            glfwPollEvents();
            drawFrame();
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
        if (enableValidationLayers) {
            requiredLayers.assign(validationLayers.begin(), validationLayers.end());
        }

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
        if (!enableValidationLayers)
            return;

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
        const auto devIter = std::ranges::find_if(devices, [&](auto const& device) {
            // Check if the device supports the Vulkan 1.3 API version
            bool supportsVulkan1_3 = device.getProperties().apiVersion >= VK_API_VERSION_1_3;

            // Check if any of the queue families support graphics operations
            auto queueFamilies = device.getQueueFamilyProperties();
            bool supportsGraphics = std::ranges::any_of(queueFamilies, [](auto const& qfp) {
                return !!(qfp.queueFlags & vk::QueueFlagBits::eGraphics);
            });

            // Check if all required device extensions are available
            auto availableDeviceExtensions = device.enumerateDeviceExtensionProperties();
            bool supportsAllRequiredExtensions = std::ranges::all_of(
                requiredDeviceExtension,
                [&availableDeviceExtensions](auto const& requiredDeviceExtension) {
                    return std::ranges::any_of(
                        availableDeviceExtensions,
                        [requiredDeviceExtension](
                            auto const& availableDeviceExtension) {
                            return strcmp(availableDeviceExtension.extensionName,
                                       requiredDeviceExtension)
                                == 0;
                        });
                });

            auto features = device.template getFeatures2<
                vk::PhysicalDeviceFeatures2, vk::PhysicalDeviceVulkan11Features,
                vk::PhysicalDeviceVulkan13Features,
                vk::PhysicalDeviceExtendedDynamicStateFeaturesEXT>();
            bool supportsRequiredFeatures = features.template get<vk::PhysicalDeviceVulkan11Features>()
                                                .shaderDrawParameters
                && features.template get<vk::PhysicalDeviceVulkan13Features>()
                       .synchronization2
                && features.template get<vk::PhysicalDeviceVulkan13Features>()
                       .dynamicRendering
                && features
                       .template get<vk::PhysicalDeviceExtendedDynamicStateFeaturesEXT>()
                       .extendedDynamicState;

            return supportsVulkan1_3 && supportsGraphics && supportsAllRequiredExtensions && supportsRequiredFeatures;
        });
        if (devIter != devices.end()) {
            physicalDevice = *devIter;
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
            vk::PhysicalDeviceExtendedDynamicStateFeaturesEXT>
            featureChain;

        featureChain.get<vk::PhysicalDeviceVulkan11Features>()
            .shaderDrawParameters
            = true;
        featureChain.get<vk::PhysicalDeviceVulkan13Features>().synchronization2 = true;
        featureChain.get<vk::PhysicalDeviceVulkan13Features>().dynamicRendering = true;
        featureChain.get<vk::PhysicalDeviceExtendedDynamicStateFeaturesEXT>()
            .extendedDynamicState
            = true;
        featureChain.get<vk::PhysicalDeviceFeatures2>().features.setSamplerAnisotropy(true);

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
                                                          .setSubresourceRange({ vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1 });

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
                                                                   .setRasterizationSamples(vk::SampleCountFlagBits::e1)
                                                                   .setSampleShadingEnable(vk::False);

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

        vk::StructureChain<vk::GraphicsPipelineCreateInfo,
            vk::PipelineRenderingCreateInfo>
            pipelineCreateInfoChain;

        pipelineCreateInfoChain.get<vk::GraphicsPipelineCreateInfo>().stageCount = 2;
        pipelineCreateInfoChain.get<vk::GraphicsPipelineCreateInfo>().pStages = shaderStages;
        pipelineCreateInfoChain.get<vk::GraphicsPipelineCreateInfo>()
            .pVertexInputState
            = &vertexInputInfo;
        pipelineCreateInfoChain.get<vk::GraphicsPipelineCreateInfo>()
            .pInputAssemblyState
            = &inputAssembly;
        pipelineCreateInfoChain.get<vk::GraphicsPipelineCreateInfo>()
            .pViewportState
            = &viewportState;
        pipelineCreateInfoChain.get<vk::GraphicsPipelineCreateInfo>()
            .pRasterizationState
            = &rasterizer;
        pipelineCreateInfoChain.get<vk::GraphicsPipelineCreateInfo>()
            .pMultisampleState
            = &multisampling;
        pipelineCreateInfoChain.get<vk::GraphicsPipelineCreateInfo>()
            .pColorBlendState
            = &colorBlending;
        pipelineCreateInfoChain.get<vk::GraphicsPipelineCreateInfo>()
            .pDynamicState
            = &dynamicState;
        pipelineCreateInfoChain.get<vk::GraphicsPipelineCreateInfo>().layout = pipelineLayout;
        pipelineCreateInfoChain.get<vk::GraphicsPipelineCreateInfo>().renderPass = nullptr;

        pipelineCreateInfoChain.get<vk::PipelineRenderingCreateInfo>()
            .colorAttachmentCount
            = 1;
        pipelineCreateInfoChain.get<vk::PipelineRenderingCreateInfo>()
            .pColorAttachmentFormats
            = &swapChainSurfaceFormat.format;

        graphicsPipeline = vk::raii::Pipeline(
            device, nullptr,
            pipelineCreateInfoChain.get<vk::GraphicsPipelineCreateInfo>());
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
        // Before starting rendering, transition the swapchain image to
        // COLOR_ATTACHMENT_OPTIMAL
        transition_image_layout(
            imageIndex, vk::ImageLayout::eUndefined,
            vk::ImageLayout::eColorAttachmentOptimal,
            {}, // srcAccessMask (no need to wait for previous operations)
            vk::AccessFlagBits2::eColorAttachmentWrite, // dstAccessMask
            vk::PipelineStageFlagBits2::eColorAttachmentOutput, // srcStage
            vk::PipelineStageFlagBits2::eColorAttachmentOutput // dstStage
        );
        vk::ClearValue clearColor = vk::ClearColorValue(0.0f, 0.0f, 0.0f, 1.0f);
        vk::RenderingAttachmentInfo attachmentInfo = vk::RenderingAttachmentInfo()
                                                         .setImageView(swapChainImageViews[imageIndex])
                                                         .setImageLayout(vk::ImageLayout::eColorAttachmentOptimal)
                                                         .setLoadOp(vk::AttachmentLoadOp::eClear)
                                                         .setStoreOp(vk::AttachmentStoreOp::eStore)
                                                         .setClearValue(clearColor);

        vk::RenderingInfo renderingInfo = vk::RenderingInfo()
                                              .setRenderArea(vk::Rect2D { vk::Offset2D { 0, 0 }, swapChainExtent })
                                              .setLayerCount(1)
                                              .setColorAttachmentCount(1)
                                              .setPColorAttachments(&attachmentInfo);

        commandBuffers[currentFrame].beginRendering(renderingInfo);
        commandBuffers[currentFrame].bindPipeline(vk::PipelineBindPoint::eGraphics,
            *graphicsPipeline);
        commandBuffers[currentFrame].setViewport(
            0,
            vk::Viewport(0.0f, 0.0f, static_cast<float>(swapChainExtent.width),
                static_cast<float>(swapChainExtent.height), 0.0f, 1.0f));
        commandBuffers[currentFrame].setScissor(
            0, vk::Rect2D(vk::Offset2D(0, 0), swapChainExtent));

        commandBuffers[currentFrame].bindVertexBuffers(0, *vertexBuffer, { 0 });
        commandBuffers[currentFrame].bindIndexBuffer(
            *indexBuffer, 0,
            vk::IndexTypeValue<decltype(indices)::value_type>::value); // vk::IndexType::eUint16
        commandBuffers[currentFrame].bindDescriptorSets(vk::PipelineBindPoint::eGraphics,
            pipelineLayout, 0, *descriptorSets[currentFrame], nullptr);
        commandBuffers[currentFrame].drawIndexed(indices.size(), 1, 0, 0, 0);

        // commandBuffers[currentFrame].draw(3, 1, 0, 0);
        commandBuffers[currentFrame].endRendering();
        // After rendering, transition the swapchain image to PRESENT_SRC
        transition_image_layout(
            imageIndex, vk::ImageLayout::eColorAttachmentOptimal,
            vk::ImageLayout::ePresentSrcKHR,
            vk::AccessFlagBits2::eColorAttachmentWrite, // srcAccessMask
            {}, // dstAccessMask
            vk::PipelineStageFlagBits2::eColorAttachmentOutput, // srcStage
            vk::PipelineStageFlagBits2::eBottomOfPipe // dstStage
        );
        commandBuffers[currentFrame].end();
    }

    void transition_image_layout(uint32_t imageIndex, vk::ImageLayout old_layout,
        vk::ImageLayout new_layout,
        vk::AccessFlags2 src_access_mask,
        vk::AccessFlags2 dst_access_mask,
        vk::PipelineStageFlags2 src_stage_mask,
        vk::PipelineStageFlags2 dst_stage_mask)
    {
        vk::ImageMemoryBarrier2 barrier = vk::ImageMemoryBarrier2()
                                              .setDstStageMask(dst_stage_mask)
                                              .setSrcStageMask(src_stage_mask)
                                              .setSrcAccessMask(src_access_mask)
                                              .setDstAccessMask(dst_access_mask)
                                              .setOldLayout(old_layout)
                                              .setNewLayout(new_layout)
                                              .setSrcQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
                                              .setDstQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
                                              .setImage(swapChainImages[imageIndex])
                                              .setSubresourceRange(
                                                  vk::ImageSubresourceRange()
                                                      .setAspectMask(vk::ImageAspectFlagBits::eColor)
                                                      .setBaseMipLevel(0)
                                                      .setLevelCount(1)
                                                      .setBaseArrayLayer(0)
                                                      .setLayerCount(1));

        vk::DependencyInfo dependency_info = vk::DependencyInfo()
                                                 .setDependencyFlags({})
                                                 .setImageMemoryBarrierCount(1)
                                                 .setPImageMemoryBarriers(&barrier);

        commandBuffers[currentFrame].pipelineBarrier2(dependency_info);
    }

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
                device, vk::FenceCreateInfo { vk::FenceCreateFlagBits::eSignaled });
        }
    }

    void drawFrame()
    {
        while (vk::Result::eTimeout == device.waitForFences({ inFlightFences[currentFrame] }, vk::True, UINT64_MAX))
            ;
        auto [result, imageIndex] = swapChain.acquireNextImage(
            UINT64_MAX, presentCompleteSemaphores[semaphoreIndex], nullptr);

        if (result == vk::Result::eErrorOutOfDateKHR) {
            recreateSwapChain();
            return;
        }

        if (result != vk::Result::eSuccess && result != vk::Result::eSuboptimalKHR) {
            throw std::runtime_error("Failed to acquire swap chain image!");
        }

        device.resetFences({ *inFlightFences[currentFrame] });
        commandBuffers[currentFrame].reset();
        recordCommandBuffer(imageIndex);

        updateUniformBuffer(currentFrame);

        vk::PipelineStageFlags waitDestinationStageMask(
            vk::PipelineStageFlagBits::eColorAttachmentOutput);
        const vk::SubmitInfo submitInfo = vk::SubmitInfo()
                                              .setWaitSemaphoreCount(1)
                                              .setPWaitSemaphores(&*presentCompleteSemaphores[semaphoreIndex])
                                              .setPWaitDstStageMask(&waitDestinationStageMask)
                                              .setCommandBufferCount(1)
                                              .setPCommandBuffers(&*commandBuffers[currentFrame])
                                              .setSignalSemaphoreCount(1)
                                              .setPSignalSemaphores(&*renderFinishedSemaphores[imageIndex]);

        queue.submit(submitInfo, *inFlightFences[currentFrame]);

        const vk::PresentInfoKHR presentInfoKHR = vk::PresentInfoKHR()
                                                      .setWaitSemaphoreCount(1)
                                                      .setPWaitSemaphores(&*renderFinishedSemaphores[imageIndex])
                                                      .setSwapchainCount(1)
                                                      .setPSwapchains(&*swapChain)
                                                      .setPImageIndices(&imageIndex);

        result = queue.presentKHR(presentInfoKHR);

        if (result == vk::Result::eErrorOutOfDateKHR || result == vk::Result::eSuboptimalKHR || framebufferResized) {
            framebufferResized = false;
            recreateSwapChain();
        } else if (result != vk::Result::eSuccess) {
            throw std::runtime_error("failed to present swap chain image!");
        }

        switch (result) {
        case vk::Result::eSuccess:
            break;
        case vk::Result::eSuboptimalKHR:
            std::cout
                << "vk::Queue::presentKHR returned vk::Result::eSuboptimalKHR !\n";
            break;
        default:
            break; // an unexpected result is returned!
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

    std::vector<const char*> getRequiredExtensions()
    {
        uint32_t glfwExtensionCount = 0;
        auto glfwExtensions = glfwGetRequiredInstanceExtensions(&glfwExtensionCount);

        std::vector extensions(glfwExtensions, glfwExtensions + glfwExtensionCount);
        if (enableValidationLayers) {
            extensions.push_back(vk::EXTDebugUtilsExtensionName);
        }

        return extensions;
    }

    static VKAPI_ATTR vk::Bool32 VKAPI_CALL debugCallback(
        vk::DebugUtilsMessageSeverityFlagBitsEXT severity,
        vk::DebugUtilsMessageTypeFlagsEXT type,
        const vk::DebugUtilsMessengerCallbackDataEXT* pCallbackData, void*)
    {
        if (severity == vk::DebugUtilsMessageSeverityFlagBitsEXT::eError || severity == vk::DebugUtilsMessageSeverityFlagBitsEXT::eWarning) {
            std::cerr << "validation layer: type " << to_string(type)
                      << " msg: " << pCallbackData->pMessage << std::endl;
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
        // vk::DeviceSize bufferSize = sizeof(vertices[0]) * vertices.size();
        // createBuffer(bufferSize, vk::BufferUsageFlagBits::eVertexBuffer,
        //              vk::MemoryPropertyFlagBits::eHostVisible |
        //                  vk::MemoryPropertyFlagBits::eHostCoherent,
        //              vertexBuffer, vertexBufferMemory);

        // void *data = vertexBufferMemory.mapMemory(0, bufferSize);
        // memcpy(data, vertices.data(), (size_t) bufferSize);
        // vertexBufferMemory.unmapMemory();

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

    // void copyBuffer(vk::raii::Buffer& srcBuffer, vk::raii::Buffer& dstBuffer,
    // vk::DeviceSize size)
    // {

    // vk::CommandBufferAllocateInfo allocInfo = vk::CommandBufferAllocateInfo()
    //   .setCommandPool(commandPool)
    //   .setLevel(vk::CommandBufferLevel::ePrimary)
    //   .setCommandBufferCount(1);

    // vk::raii::CommandBuffer commandCopyBuffer = std::move(device.allocateCommandBuffers(allocInfo).front());

    // commandCopyBuffer.begin(vk::CommandBufferBeginInfo().setFlags(
    // vk::CommandBufferUsageFlagBits::eOneTimeSubmit));
    // commandCopyBuffer.copyBuffer(srcBuffer, dstBuffer,
    // vk::BufferCopy(0, 0, size));
    // commandCopyBuffer.end();

    // queue.submit(vk::SubmitInfo().setCommandBufferCount(1).setPCommandBuffers(
    //  &*commandCopyBuffer),
    // nullptr);
    // queue.waitIdle();
    // }

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
        ubo.proj = glm::perspective(glm::radians(45.0f), static_cast<float>(swapChainExtent.width) / static_cast<float>(swapChainExtent.height), 0.1f, 10.0f);
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

    void createImage(uint32_t width, uint32_t height, vk::Format format, vk::ImageTiling tiling, vk::ImageUsageFlags usage, vk::MemoryPropertyFlags properties, vk::raii::Image& image, vk::raii::DeviceMemory& imageMemory)
    {
        vk::ImageCreateInfo imageInfo = vk::ImageCreateInfo()
                                            .setImageType(vk::ImageType::e2D)
                                            .setFormat(format)
                                            .setExtent({ width, height, 1 })
                                            .setMipLevels(1)
                                            .setArrayLayers(1)
                                            .setSamples(vk::SampleCountFlagBits::e1)
                                            .setTiling(tiling)
                                            .setUsage(usage)
                                            .setSharingMode(vk::SharingMode::eExclusive);

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
        stbi_uc* pixels = stbi_load("textures/texture.jpg", &texWidth, &texHeight, &texChannels, STBI_rgb_alpha);
        vk::DeviceSize imageSize = texWidth * texHeight * 4;

        if (!pixels) {
            throw std::runtime_error("failed to load texture image!");
        }

        vk::raii::Buffer stagingBuffer({});
        vk::raii::DeviceMemory stagingBufferMemory({});
        createBuffer(imageSize, vk::BufferUsageFlagBits::eTransferSrc, vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent, stagingBuffer, stagingBufferMemory);

        void* data = stagingBufferMemory.mapMemory(0, imageSize);
        memcpy(data, pixels, imageSize);
        stagingBufferMemory.unmapMemory();

        stbi_image_free(pixels);

        createImage(texWidth, texHeight, vk::Format::eR8G8B8A8Srgb, vk::ImageTiling::eOptimal, vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eSampled, vk::MemoryPropertyFlagBits::eDeviceLocal, textureImage, textureImageMemory);

        transitionImageLayout(textureImage, vk::ImageLayout::eUndefined, vk::ImageLayout::eTransferDstOptimal);
        copyBufferToImage(stagingBuffer, textureImage, static_cast<uint32_t>(texWidth), static_cast<uint32_t>(texHeight));
        transitionImageLayout(textureImage, vk::ImageLayout::eTransferDstOptimal, vk::ImageLayout::eShaderReadOnlyOptimal);
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

    void transitionImageLayout(const vk::raii::Image& image, vk::ImageLayout oldLayout, vk::ImageLayout newLayout)
    {
        auto commandBuffer = beginSingleTimeCommands();

        vk::ImageMemoryBarrier barrier = vk::ImageMemoryBarrier()
                                             .setOldLayout(oldLayout)
                                             .setNewLayout(newLayout)
                                             .setImage(image)
                                             .setSubresourceRange({ vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1 });

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
        textureImageView = createImageView(textureImage, vk::Format::eR8G8B8A8Srgb);
    }

    vk::raii::ImageView createImageView(vk::raii::Image& image, vk::Format format)
    {
        vk::ImageViewCreateInfo viewInfo = vk::ImageViewCreateInfo()
                                               .setImage(image)
                                               .setViewType(vk::ImageViewType::e2D)
                                               .setFormat(format)
                                               .setSubresourceRange({ vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1 });
        return vk::raii::ImageView(device, viewInfo);
    }

    void createTextureSampler()
    {
        vk::PhysicalDeviceProperties properties = physicalDevice.getProperties();
        vk::SamplerCreateInfo samplerInfo = vk::SamplerCreateInfo()
                                                .setMagFilter(vk::Filter::eLinear)
                                                .setMinFilter(vk::Filter::eLinear)
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
