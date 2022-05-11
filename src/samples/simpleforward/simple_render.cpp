#include "simple_render.h"
#include "../../utils/input_definitions.h"

#include <geom/vk_mesh.h>
#include <vk_pipeline.h>
#include <vk_buffers.h>

SimpleRender::SimpleRender(uint32_t a_width, uint32_t a_height) : m_width(a_width), m_height(a_height)
{
#ifdef NDEBUG
  m_enableValidation = false;
#else
  m_enableValidation = true;
#endif
}

void SimpleRender::SetupDeviceFeatures()
{
  // m_enabledDeviceFeatures.fillModeNonSolid = VK_TRUE;
  m_enabledDeviceFeatures.geometryShader   = VK_TRUE;
}

void SimpleRender::SetupDeviceExtensions()
{
  m_deviceExtensions.push_back(VK_KHR_SWAPCHAIN_EXTENSION_NAME);
  m_deviceExtensions.push_back(VK_KHR_SHADER_NON_SEMANTIC_INFO_EXTENSION_NAME);
}

void SimpleRender::SetupValidationLayers()
{
  m_validationLayers.push_back("VK_LAYER_KHRONOS_validation");
  m_validationLayers.push_back("VK_LAYER_LUNARG_monitor");
}

void SimpleRender::InitVulkan(const char** a_instanceExtensions, uint32_t a_instanceExtensionsCount, uint32_t a_deviceId)
{
  for(size_t i = 0; i < a_instanceExtensionsCount; ++i)
  {
    m_instanceExtensions.push_back(a_instanceExtensions[i]);
  }

  SetupValidationLayers();
  VK_CHECK_RESULT(volkInitialize());
  CreateInstance();
  volkLoadInstance(m_instance);

  CreateDevice(a_deviceId);
  volkLoadDevice(m_device);

  m_commandPool = vk_utils::createCommandPool(m_device, m_queueFamilyIDXs.graphics,
                                              VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT);

  m_cmdBuffersDrawMain.reserve(m_framesInFlight);
  m_cmdBuffersDrawMain = vk_utils::createCommandBuffers(m_device, m_commandPool, m_framesInFlight);

  m_frameFences.resize(m_framesInFlight);
  VkFenceCreateInfo fenceInfo = {};
  fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
  fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;
  for (size_t i = 0; i < m_framesInFlight; i++)
  {
    VK_CHECK_RESULT(vkCreateFence(m_device, &fenceInfo, nullptr, &m_frameFences[i]));
  }

  m_ImageSampler = vk_utils::createSampler(
    m_device, VK_FILTER_LINEAR, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK);

  m_pScnMgr = std::make_shared<SceneManager>(m_device, m_physicalDevice, m_queueFamilyIDXs.transfer,
                                             m_queueFamilyIDXs.graphics, false);
}

void SimpleRender::InitPresentation(VkSurfaceKHR &a_surface, bool initGUI)
{
  m_surface = a_surface;

  m_presentationResources.queue = m_swapchain.CreateSwapChain(m_physicalDevice, m_device, m_surface,
                                                              m_width, m_height, m_framesInFlight, m_vsync);
  m_presentationResources.currentFrame = 0;

  VkSemaphoreCreateInfo semaphoreInfo = {};
  semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
  VK_CHECK_RESULT(vkCreateSemaphore(m_device, &semaphoreInfo, nullptr, &m_presentationResources.imageAvailable));
  VK_CHECK_RESULT(vkCreateSemaphore(m_device, &semaphoreInfo, nullptr, &m_presentationResources.renderingFinished));
  m_screenRenderPass = vk_utils::createDefaultRenderPass(m_device, m_swapchain.GetFormat());

  std::vector<VkFormat> depthFormats = {
      VK_FORMAT_D32_SFLOAT,
      VK_FORMAT_D32_SFLOAT_S8_UINT,
      VK_FORMAT_D24_UNORM_S8_UINT,
      VK_FORMAT_D16_UNORM_S8_UINT,
      VK_FORMAT_D16_UNORM
  };
  vk_utils::getSupportedDepthFormat(m_physicalDevice, depthFormats, &m_depthBuffer.format);
  m_depthBuffer  = vk_utils::createDepthTexture(m_device, m_physicalDevice, m_width, m_height, m_depthBuffer.format);
  m_frameBuffers = vk_utils::createFrameBuffers(m_device, m_swapchain, m_screenRenderPass, m_depthBuffer.view);

  CreateGBuffer();
  //CreateShadowmap();
  CreatePostFx();

  if(initGUI)
    m_pGUIRender = std::make_shared<ImGuiRender>(m_instance, m_device, m_physicalDevice, m_queueFamilyIDXs.graphics, m_graphicsQueue, m_swapchain);
}

void SimpleRender::CreateInstance()
{
  VkApplicationInfo appInfo = {};
  appInfo.sType              = VK_STRUCTURE_TYPE_APPLICATION_INFO;
  appInfo.pNext              = nullptr;
  appInfo.pApplicationName   = "VkRender";
  appInfo.applicationVersion = VK_MAKE_VERSION(0, 1, 0);
  appInfo.pEngineName        = "SimpleForward";
  appInfo.engineVersion      = VK_MAKE_VERSION(0, 1, 0);
  appInfo.apiVersion         = VK_MAKE_VERSION(1, 2, 0);

  m_instance = vk_utils::createInstance(m_enableValidation, m_validationLayers, m_instanceExtensions, &appInfo);

  if (m_enableValidation)
    vk_utils::initDebugReportCallback(m_instance, &debugReportCallbackFn, &m_debugReportCallback);
}

void SimpleRender::CreateDevice(uint32_t a_deviceId)
{
  SetupDeviceExtensions();
  m_physicalDevice = vk_utils::findPhysicalDevice(m_instance, true, a_deviceId, m_deviceExtensions);

  SetupDeviceFeatures();
  m_device = vk_utils::createLogicalDevice(m_physicalDevice, m_validationLayers, m_deviceExtensions,
                                           m_enabledDeviceFeatures, m_queueFamilyIDXs,
                                           VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_TRANSFER_BIT);

  vkGetDeviceQueue(m_device, m_queueFamilyIDXs.graphics, 0, &m_graphicsQueue);
  vkGetDeviceQueue(m_device, m_queueFamilyIDXs.transfer, 0, &m_transferQueue);
}

vk_utils::DescriptorMaker& SimpleRender::GetDescMaker()
{
  if(m_pBindings == nullptr)
  {
    std::vector<std::pair<VkDescriptorType, uint32_t> > dtypes = {
      {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1},
      {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 4},
      {VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, 4},
      {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1}
    };
    m_pBindings = std::make_unique<vk_utils::DescriptorMaker>(m_device, dtypes, 5);
  }

  return *m_pBindings;
}

void SimpleRender::SetupGBufferPipeline()
{
  auto& bindings = GetDescMaker();

  bindings.BindBegin(VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_VERTEX_BIT);
  bindings.BindBuffer(0, m_ubo, VK_NULL_HANDLE, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER);
  m_pBindings->BindBuffer(1, m_pScnMgr->GetMeshInfoBuffer());
  bindings.BindEnd(&m_graphicsDescriptorSet, &m_graphicsDescriptorSetLayout);

  auto make_deferred_pipeline = [this](const std::unordered_map<VkShaderStageFlagBits, std::string>& shader_paths)
  {
    vk_utils::GraphicsPipelineMaker maker;
    maker.LoadShaders(m_device, shader_paths);
    pipeline_data_t result;
    result.layout = maker.MakeLayout(m_device,
      {m_graphicsDescriptorSetLayout}, sizeof(pushConst));

    maker.SetDefaultState(m_width, m_height);

    std::array<VkPipelineColorBlendAttachmentState, 3> cba_state;

    cba_state.fill(VkPipelineColorBlendAttachmentState {
      .blendEnable    = VK_FALSE,
      .colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT,
    });

    maker.colorBlending.attachmentCount = static_cast<uint32_t>(cba_state.size());
    maker.colorBlending.pAttachments = cba_state.data();

    result.pipeline = maker.MakePipeline(m_device, m_pScnMgr->GetPipelineVertexInputStateCreateInfo(),
      m_gbuffer.renderpass, {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR});

    return result;
  };

  m_gBufferPipeline = make_deferred_pipeline(
    std::unordered_map<VkShaderStageFlagBits, std::string> {
      {VK_SHADER_STAGE_FRAGMENT_BIT, std::string{GBUFFER_FRAGMENT_SHADER_PATH} + ".spv"},
      {VK_SHADER_STAGE_VERTEX_BIT, std::string{GBUFFER_VERTEX_SHADER_PATH} + ".spv"}
    });
}

void SimpleRender::SetupShadingPipeline()
{
  auto& bindings = GetDescMaker();

  bindings.BindBegin(VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_VERTEX_BIT);
  bindings.BindBuffer(0, m_ubo, VK_NULL_HANDLE, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER);
  bindings.BindEnd(&m_lightingDescriptorSet, &m_lightingDescriptorSetLayout);


  if (m_lightingFragmentDescriptorSetLayout == nullptr)
  {
    bindings.BindBegin(VK_SHADER_STAGE_FRAGMENT_BIT);
    bindings.BindImage(0, m_gbuffer.color_layers[0].image.view,
      nullptr, VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT);
    bindings.BindImage(1, m_gbuffer.color_layers[1].image.view,
      nullptr, VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT);
    bindings.BindImage(2, m_gbuffer.color_layers[2].image.view,
      nullptr, VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT);
    bindings.BindImage(3, m_gbuffer.depth_stencil_layer.image.view,
      nullptr, VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT);
    bindings.BindEnd(&m_lightingFragmentDescriptorSet, &m_lightingFragmentDescriptorSetLayout);
  }
  else
  {
    std::array image_infos {
      VkDescriptorImageInfo {
        .sampler = nullptr,
        .imageView = m_gbuffer.color_layers[0].image.view,
        .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
      },
      VkDescriptorImageInfo {
        .sampler = nullptr,
        .imageView = m_gbuffer.color_layers[1].image.view,
        .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
      },
      VkDescriptorImageInfo {
        .sampler = nullptr,
        .imageView = m_gbuffer.color_layers[2].image.view,
        .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
      },
      VkDescriptorImageInfo {
        .sampler = nullptr,
        .imageView = m_gbuffer.depth_stencil_layer.image.view,
        .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
      },
    };

    std::array<VkWriteDescriptorSet, image_infos.size()> writes;

    for (std::size_t i = 0; i < image_infos.size(); ++i) {
      writes[i] = VkWriteDescriptorSet{
        .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
        .dstSet = m_lightingFragmentDescriptorSet,
        .dstBinding = static_cast<uint32_t>(i),
        .descriptorCount = 1,
        .descriptorType = VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT,
        .pImageInfo = image_infos.data() + i
      };
    }

    vkUpdateDescriptorSets(m_device, static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);
  }

  vk_utils::GraphicsPipelineMaker maker;

  maker.LoadShaders(m_device, std::unordered_map<VkShaderStageFlagBits, std::string> {
                                {VK_SHADER_STAGE_FRAGMENT_BIT, std::string{SHADING_FRAGMENT_SHADER_PATH} + ".spv"},
                                {VK_SHADER_STAGE_VERTEX_BIT, std::string{SHADING_VERTEX_SHADER_PATH} + ".spv"}
                              });

  m_shadingPipeline.layout = maker.MakeLayout(m_device,
    {m_lightingDescriptorSetLayout, m_lightingFragmentDescriptorSetLayout}, sizeof(pushConst));

																								  
  maker.SetDefaultState(m_width, m_height);

  maker.rasterizer.cullMode = VK_CULL_MODE_NONE;
  maker.depthStencilTest.depthTestEnable = false;

  maker.colorBlendAttachments = {VkPipelineColorBlendAttachmentState {
    .blendEnable = true,
    .srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA,
    .dstColorBlendFactor = VK_BLEND_FACTOR_ONE,
    .colorBlendOp = VK_BLEND_OP_ADD,
    .srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE,
    .dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO,
    .alphaBlendOp = VK_BLEND_OP_ADD,
    .colorWriteMask =
      VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT
  }};

  VkPipelineVertexInputStateCreateInfo in_info {
    .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
    .vertexBindingDescriptionCount = 0,
    .pVertexBindingDescriptions = nullptr,
    .vertexAttributeDescriptionCount = 0,
    .pVertexAttributeDescriptions = nullptr,
  };

  m_shadingPipeline.pipeline = maker.MakePipeline(m_device, in_info,
    m_gbuffer.renderpass, {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR}, vk_utils::IA_TList(), 1);
}

void SimpleRender::SetupShadowmapPipeline()
{
  //return;
  auto& bindings = GetDescMaker();

  bindings.BindBegin(VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_VERTEX_BIT);
  bindings.BindBuffer(0, m_ubo, VK_NULL_HANDLE, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER);
  m_pBindings->BindBuffer(1, m_pScnMgr->GetMeshInfoBuffer());
  bindings.BindEnd(&m_graphicsDescriptorSet, &m_graphicsDescriptorSetLayout);

  auto make_deferred_pipeline = [this](const std::unordered_map<VkShaderStageFlagBits, std::string>& shader_paths)
  {

    vk_utils::GraphicsPipelineMaker maker;
    maker.LoadShaders(m_device, shader_paths);
    pipeline_data_t result;
    result.layout = maker.MakeLayout(m_device,
      {m_graphicsDescriptorSetLayout}, sizeof(pushConst));

    maker.SetDefaultState(m_width, m_height);

    std::array<VkPipelineColorBlendAttachmentState, 3> cba_state;

    cba_state.fill(VkPipelineColorBlendAttachmentState {
      .blendEnable    = VK_FALSE,
      .colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT,
    });

    maker.colorBlending.attachmentCount = static_cast<uint32_t>(cba_state.size());
    maker.colorBlending.pAttachments = cba_state.data();

    result.pipeline = maker.MakePipeline(m_device, m_pScnMgr->GetPipelineVertexInputStateCreateInfo(),
      m_gbuffer.renderpass, {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR});

    return result;
  };

  m_gBufferPipeline = make_deferred_pipeline(
    std::unordered_map<VkShaderStageFlagBits, std::string> {
      {VK_SHADER_STAGE_FRAGMENT_BIT, std::string{GBUFFER_FRAGMENT_SHADER_PATH} + ".spv"},
      {VK_SHADER_STAGE_VERTEX_BIT, std::string{GBUFFER_VERTEX_SHADER_PATH} + ".spv"}
    });
}

void SimpleRender::SetupPostfxPipeline()
{
  auto& bindings = GetDescMaker();

  bindings.BindBegin(VK_SHADER_STAGE_FRAGMENT_BIT);
  bindings.BindBuffer(0, m_ubo, nullptr, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER);
  bindings.BindImage(1, m_gbuffer.resolved.view, m_ImageSampler, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
  bindings.BindEnd(&m_postFxDescriptorSet, &m_postFxDescriptorSetLayout);


  vk_utils::GraphicsPipelineMaker maker;

  maker.LoadShaders(m_device, {
    {VK_SHADER_STAGE_VERTEX_BIT, std::string{FULLSCREEN_QUAD3_VERTEX_SHADER_PATH} + ".spv"},
    {VK_SHADER_STAGE_FRAGMENT_BIT, std::string{POSTFX_FRAGMENT_SHADER_PATH} + ".spv"},
  });

  m_postFxPipeline.layout = maker.MakeLayout(m_device,
    {m_postFxDescriptorSetLayout}, sizeof(pushConst));

  maker.SetDefaultState(m_width, m_height);

  m_postFxPipeline.pipeline = maker.MakePipeline(m_device,
    VkPipelineVertexInputStateCreateInfo {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
    },
    m_postFxRenderPass, {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR}, vk_utils::IA_TList(), 0);
}

void SimpleRender::CreateUniformBuffer()
{
  VkMemoryRequirements memReq;
  m_ubo = vk_utils::createBuffer(m_device, sizeof(UniformParams), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, &memReq);

  VkMemoryAllocateInfo allocateInfo = {};
  allocateInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
  allocateInfo.pNext = nullptr;
  allocateInfo.allocationSize = memReq.size;
  allocateInfo.memoryTypeIndex = vk_utils::findMemoryType(memReq.memoryTypeBits,
                                                          VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                                          VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                                                          m_physicalDevice);
  VK_CHECK_RESULT(vkAllocateMemory(m_device, &allocateInfo, nullptr, &m_uboAlloc));

  VK_CHECK_RESULT(vkBindBufferMemory(m_device, m_ubo, m_uboAlloc, 0));

  vkMapMemory(m_device, m_uboAlloc, 0, sizeof(m_uniforms), 0, &m_uboMappedMem);

  m_uniforms.lightPos = LiteMath::float3(0.0f, 0.0f, 0.0f);
  m_uniforms.baseColor = LiteMath::float3(0.9f, 0.92f, 1.0f);
  m_uniforms.animateLightColor = true;

  UpdateUniformBuffer(0.0f);
}

void SimpleRender::UpdateUniformBuffer(float a_time)
{
// most uniforms are updated in GUI -> SetupGUIElements()
  m_uniforms.time = a_time;
  m_uniforms.lightMatrix = ortoMatrix(-m_light_radius, m_light_radius, -m_light_radius, m_light_radius, -m_light_length / 2, m_light_length / 2)
                           * LiteMath::lookAt({0, 0, 0}, m_light_direction * 10.0f, {0, 1, 0});
  m_uniforms.screenWidth = m_width;
  m_uniforms.screenHeight = m_height;
  memcpy(m_uboMappedMem, &m_uniforms, sizeof(m_uniforms));
}

void SimpleRender::BuildCommandBufferSimple(VkCommandBuffer a_cmdBuff, VkFramebuffer a_frameBuff,
                                            VkImageView, VkPipeline a_pipeline)
{
  vkResetCommandBuffer(a_cmdBuff, 0);

  VkCommandBufferBeginInfo beginInfo = {};
  beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
  beginInfo.flags = VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT;

  VK_CHECK_RESULT(vkBeginCommandBuffer(a_cmdBuff, &beginInfo));

  vk_utils::setDefaultViewport(a_cmdBuff, static_cast<float>(m_width), static_cast<float>(m_height));
  vk_utils::setDefaultScissor(a_cmdBuff, m_width, m_height);

  std::array mainPassClearValues {
      VkClearValue {
        .color = {{0.0f, 0.0f, 0.0f, 1.0f}}
      },
      VkClearValue {
        .color = {{0.0f, 0.0f, 0.0f, 1.0f}}
      },
      VkClearValue {
        .color = {{0.0f, 0.0f, 0.0f, 1.0f}}
      },
      VkClearValue {
        .depthStencil = {1.0f, 0}
      },
      VkClearValue {
        .color = {{0.0f, 0.0f, 0.0f, 1.0f}}
      },
    };

  ///// draw final scene to screen
  {
    VkRenderPassBeginInfo renderPassInfo = {};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    renderPassInfo.renderPass = m_gbuffer.renderpass;
    renderPassInfo.framebuffer = m_mainPassFrameBuffer;
    renderPassInfo.renderArea.offset = {0, 0};
    renderPassInfo.renderArea.extent = m_swapchain.GetExtent();

    renderPassInfo.clearValueCount = static_cast<uint32_t>(mainPassClearValues.size());
    renderPassInfo.pClearValues = &mainPassClearValues[0];

    vkCmdBeginRenderPass(a_cmdBuff, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);
    {
      vkCmdBindPipeline(a_cmdBuff, VK_PIPELINE_BIND_POINT_GRAPHICS, m_gBufferPipeline.pipeline);
      vkCmdBindDescriptorSets(a_cmdBuff, VK_PIPELINE_BIND_POINT_GRAPHICS, m_gBufferPipeline.layout, 0, 1, &m_graphicsDescriptorSet, 0, VK_NULL_HANDLE);

      VkShaderStageFlags stageFlags = (VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT);

      VkDeviceSize zero_offset = 0u;
      VkBuffer vertexBuf       = m_pScnMgr->GetVertexBuffer();
      VkBuffer indexBuf        = m_pScnMgr->GetIndexBuffer();

      vkCmdBindVertexBuffers(a_cmdBuff, 0, 1, &vertexBuf, &zero_offset);
      vkCmdBindIndexBuffer(a_cmdBuff, indexBuf, 0, VK_INDEX_TYPE_UINT32);

      for (uint32_t i = 0; i < m_pScnMgr->InstancesNum(); ++i)
      {
        auto inst = m_pScnMgr->GetInstanceInfo(i);

        pushConst.instanceID = inst.mesh_id;
        pushConst.model = m_pScnMgr->GetInstanceMatrix(i);
        pushConst.color = meshColors[i % 3];
        vkCmdPushConstants(a_cmdBuff, m_gBufferPipeline.layout, stageFlags, 0, sizeof(pushConst), &pushConst);

        auto mesh_info = m_pScnMgr->GetMeshInfo(inst.mesh_id);
        vkCmdDrawIndexed(a_cmdBuff, mesh_info.m_indNum, 1, mesh_info.m_indexOffset, mesh_info.m_vertexOffset, 0);
      }

      vkCmdNextSubpass(a_cmdBuff, VK_SUBPASS_CONTENTS_INLINE);
      {
        vkCmdBindPipeline(a_cmdBuff, VK_PIPELINE_BIND_POINT_GRAPHICS, m_shadingPipeline.pipeline);

        std::array dsets {m_lightingDescriptorSet, m_lightingFragmentDescriptorSet};
        vkCmdBindDescriptorSets(a_cmdBuff, VK_PIPELINE_BIND_POINT_GRAPHICS, m_shadingPipeline.layout, 0,
          static_cast<uint32_t>(dsets.size()), dsets.data(), 0, VK_NULL_HANDLE);

        VkShaderStageFlags stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
        vkCmdPushConstants(a_cmdBuff, m_shadingPipeline.layout, stageFlags, 0, sizeof(pushConst), &pushConst);

        vkCmdDraw(a_cmdBuff, 3, 1, 0, 0);
      }

    }
    vkCmdEndRenderPass(a_cmdBuff);
  }

  //post-fx
  std::array postFxClearValues {
      VkClearValue {
        .color = {{0.0f, 0.0f, 0.0f, 1.0f}}
      },
  };

  VkRenderPassBeginInfo postFxInfo{
      .sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
      .renderPass = m_postFxRenderPass,
      .framebuffer = a_frameBuff,
      .renderArea = {
        .offset = {0, 0},
        .extent = m_swapchain.GetExtent(),
      },
      .clearValueCount = static_cast<uint32_t>(mainPassClearValues.size()),
      .pClearValues = mainPassClearValues.data(),
    };

  vkCmdBeginRenderPass(a_cmdBuff, &postFxInfo, VK_SUBPASS_CONTENTS_INLINE);
  {
    vkCmdBindPipeline(a_cmdBuff, VK_PIPELINE_BIND_POINT_GRAPHICS, m_postFxPipeline.pipeline);
    vkCmdPushConstants(a_cmdBuff, m_postFxPipeline.layout, VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_VERTEX_BIT,
        0, sizeof(pushConst), &pushConst);
    vkCmdBindDescriptorSets(a_cmdBuff, VK_PIPELINE_BIND_POINT_GRAPHICS, m_postFxPipeline.layout,
        0, 1, &m_postFxDescriptorSet, 0, nullptr);

    vkCmdDraw(a_cmdBuff, 3, 1, 0, 0);
  }
  vkCmdEndRenderPass(a_cmdBuff);

  VK_CHECK_RESULT(vkEndCommandBuffer(a_cmdBuff));
}


void SimpleRender::CleanupPipelineAndSwapchain()
{
  if (!m_cmdBuffersDrawMain.empty())
  {
    vkFreeCommandBuffers(m_device, m_commandPool, static_cast<uint32_t>(m_cmdBuffersDrawMain.size()),
                         m_cmdBuffersDrawMain.data());
    m_cmdBuffersDrawMain.clear();
  }

  for (size_t i = 0; i < m_frameFences.size(); i++)
  {
    vkDestroyFence(m_device, m_frameFences[i], nullptr);
  }
  m_frameFences.clear();

  ClearGBuffer();
  //ClearShadowmap();
  ClearPostFx();

  vk_utils::deleteImg(m_device, &m_depthBuffer);
  
  if(m_depthBuffer.mem != VK_NULL_HANDLE)
  {
    vkFreeMemory(m_device, m_depthBuffer.mem, nullptr);
    m_depthBuffer.mem = VK_NULL_HANDLE;
  }

  for (size_t i = 0; i < m_frameBuffers.size(); i++)
  {
    vkDestroyFramebuffer(m_device, m_frameBuffers[i], nullptr);
  }
  m_frameBuffers.clear();

  if(m_screenRenderPass != VK_NULL_HANDLE)
  {
    vkDestroyRenderPass(m_device, m_screenRenderPass, nullptr);
    m_screenRenderPass = VK_NULL_HANDLE;
  }

  m_swapchain.Cleanup();
}

void SimpleRender::RecreateSwapChain()
{
  vkDeviceWaitIdle(m_device);

  ClearPipeline(m_gBufferPipeline);
  ClearPipeline(m_shadingPipeline);
  ClearPipeline(m_shadowmapPipeline);
  ClearPipeline(m_postFxPipeline);

  CleanupPipelineAndSwapchain();
  auto oldImagesNum = m_swapchain.GetImageCount();
  m_presentationResources.queue = m_swapchain.CreateSwapChain(m_physicalDevice, m_device, m_surface, m_width, m_height,
    oldImagesNum, m_vsync);

  CreateGBuffer();
  //CreateShadowmap();
  CreatePostFx();
  SetupGBufferPipeline();
  SetupShadingPipeline();
  //SetupShadowmapPipeline();
  SetupPostfxPipeline();

  std::vector<VkFormat> depthFormats = {
      VK_FORMAT_D32_SFLOAT,
      VK_FORMAT_D32_SFLOAT_S8_UINT,
      VK_FORMAT_D24_UNORM_S8_UINT,
      VK_FORMAT_D16_UNORM_S8_UINT,
      VK_FORMAT_D16_UNORM
  };                                                            
  vk_utils::getSupportedDepthFormat(m_physicalDevice, depthFormats, &m_depthBuffer.format);
  
  m_screenRenderPass = vk_utils::createDefaultRenderPass(m_device, m_swapchain.GetFormat());
  m_depthBuffer      = vk_utils::createDepthTexture(m_device, m_physicalDevice, m_width, m_height, m_depthBuffer.format);
  m_frameBuffers     = vk_utils::createFrameBuffers(m_device, m_swapchain, m_screenRenderPass, m_depthBuffer.view);

  m_frameFences.resize(m_framesInFlight);
  VkFenceCreateInfo fenceInfo = {};
  fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
  fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;
  for (size_t i = 0; i < m_framesInFlight; i++)
  {
    VK_CHECK_RESULT(vkCreateFence(m_device, &fenceInfo, nullptr, &m_frameFences[i]));
  }

  m_cmdBuffersDrawMain = vk_utils::createCommandBuffers(m_device, m_commandPool, m_framesInFlight);
  // for (uint32_t i = 0; i < m_swapchain.GetImageCount(); ++i)
  // {
  //   BuildCommandBufferSimple(m_cmdBuffersDrawMain[i], m_frameBuffers[i],
  //                            m_swapchain.GetAttachment(i).view, m_basicForwardPipeline.pipeline);
  // }

  m_pGUIRender->OnSwapchainChanged(m_swapchain);
}

void SimpleRender::Cleanup()
{
  if(m_pGUIRender)
  {
    m_pGUIRender = nullptr;
    ImGui::DestroyContext();
  }
  CleanupPipelineAndSwapchain();
  if(m_surface != VK_NULL_HANDLE)
  {
    vkDestroySurfaceKHR(m_instance, m_surface, nullptr);
    m_surface = VK_NULL_HANDLE;
  }
  if (m_ImageSampler != VK_NULL_HANDLE)
  {
    vkDestroySampler(m_device, m_ImageSampler, nullptr);
    m_ImageSampler = VK_NULL_HANDLE;
  }


  ClearPipeline(m_gBufferPipeline);
  ClearPipeline(m_shadingPipeline);

  if (m_basicForwardPipeline.pipeline != VK_NULL_HANDLE)
  {
    vkDestroyPipeline(m_device, m_basicForwardPipeline.pipeline, nullptr);
    m_basicForwardPipeline.pipeline = VK_NULL_HANDLE;
  }
  if (m_basicForwardPipeline.layout != VK_NULL_HANDLE)
  {
    vkDestroyPipelineLayout(m_device, m_basicForwardPipeline.layout, nullptr);
    m_basicForwardPipeline.layout = VK_NULL_HANDLE;
  }

  if (m_presentationResources.imageAvailable != VK_NULL_HANDLE)
  {
    vkDestroySemaphore(m_device, m_presentationResources.imageAvailable, nullptr);
    m_presentationResources.imageAvailable = VK_NULL_HANDLE;
  }
  if (m_presentationResources.renderingFinished != VK_NULL_HANDLE)
  {
    vkDestroySemaphore(m_device, m_presentationResources.renderingFinished, nullptr);
    m_presentationResources.renderingFinished = VK_NULL_HANDLE;
  }

  if (m_commandPool != VK_NULL_HANDLE)
  {
    vkDestroyCommandPool(m_device, m_commandPool, nullptr);
    m_commandPool = VK_NULL_HANDLE;
  }

  if(m_ubo != VK_NULL_HANDLE)
  {
    vkDestroyBuffer(m_device, m_ubo, nullptr);
    m_ubo = VK_NULL_HANDLE;
  }

  if(m_uboAlloc != VK_NULL_HANDLE)
  {
    vkFreeMemory(m_device, m_uboAlloc, nullptr);
    m_uboAlloc = VK_NULL_HANDLE;
  }

  m_pBindings = nullptr;
  m_pScnMgr   = nullptr;

  if(m_device != VK_NULL_HANDLE)
  {
    vkDestroyDevice(m_device, nullptr);
    m_device = VK_NULL_HANDLE;
  }

  if(m_debugReportCallback != VK_NULL_HANDLE)
  {
    vkDestroyDebugReportCallbackEXT(m_instance, m_debugReportCallback, nullptr);
    m_debugReportCallback = VK_NULL_HANDLE;
  }

  if(m_instance != VK_NULL_HANDLE)
  {
    vkDestroyInstance(m_instance, nullptr);
    m_instance = VK_NULL_HANDLE;
  }
}

void SimpleRender::ProcessInput(const AppInput &input)
{
  // add keyboard controls here
  // camera movement is processed separately

  // recreate pipeline to reload shaders
  if(input.keyPressed[GLFW_KEY_B])
  {
#ifdef WIN32
    std::system("cd ../resources/shaders && python compile_simple_render_shaders.py");
#else
    std::system("cd ../resources/shaders && python3 compile_simple_render_shaders.py");
#endif

    SetupGBufferPipeline();
    SetupShadingPipeline();
    SetupPostfxPipeline();

    // for (uint32_t i = 0; i < m_framesInFlight; ++i)
    // {
    //   BuildCommandBufferSimple(m_cmdBuffersDrawMain[i], m_frameBuffers[i],
    //                            m_swapchain.GetAttachment(i).view, m_basicForwardPipeline.pipeline);
    // }
  }

}

void SimpleRender::UpdateCamera(const Camera* cams, uint32_t a_camsCount)
{
  assert(a_camsCount > 0);
  m_cam = cams[0];
  UpdateView();
}

void SimpleRender::UpdateView()
{
  const float aspect   = float(m_width) / float(m_height);
  auto mProjFix        = OpenglToVulkanProjectionMatrixFix();
  auto mProj           = projectionMatrix(m_cam.fov, aspect, 0.1f, 1000.0f);
  auto mLookAt         = LiteMath::lookAt(m_cam.pos, m_cam.lookAt, m_cam.up);
  auto mWorldViewProj  = mProjFix * mProj;
  m_uniforms.proj = mWorldViewProj;
  m_uniforms.view = mLookAt;
}

void SimpleRender::LoadScene(const char* path, bool transpose_inst_matrices)
{
  m_pScnMgr->LoadSceneXML(path, transpose_inst_matrices);

  CreateUniformBuffer();
  SetupGBufferPipeline();
  SetupShadingPipeline();
  SetupPostfxPipeline();

  auto loadedCam = m_pScnMgr->GetCamera(0);
  m_cam.fov = loadedCam.fov;
  m_cam.pos = float3(loadedCam.pos);
  m_cam.up  = float3(loadedCam.up);
  m_cam.lookAt = float3(loadedCam.lookAt);
  m_cam.tdist  = loadedCam.farPlane;

  UpdateView();

  // for (uint32_t i = 0; i < m_framesInFlight; ++i)
  // {
  //   BuildCommandBufferSimple(m_cmdBuffersDrawMain[i], m_frameBuffers[i],
  //                            m_swapchain.GetAttachment(i).view, m_basicForwardPipeline.pipeline);
  // }
}

void SimpleRender::ClearPipeline(pipeline_data_t &pipeline)
{
  // if we are recreating pipeline (for example, to reload shaders)
  // we need to cleanup old pipeline
  if(pipeline.layout != VK_NULL_HANDLE)
  {
    vkDestroyPipelineLayout(m_device, pipeline.layout, nullptr);
    pipeline.layout = VK_NULL_HANDLE;
  }
  if(pipeline.pipeline != VK_NULL_HANDLE)
  {
    vkDestroyPipeline(m_device, pipeline.pipeline, nullptr);
    pipeline.pipeline = VK_NULL_HANDLE;
  }
}

void SimpleRender::DrawFrameSimple()
{
  vkWaitForFences(m_device, 1, &m_frameFences[m_presentationResources.currentFrame], VK_TRUE, UINT64_MAX);
  vkResetFences(m_device, 1, &m_frameFences[m_presentationResources.currentFrame]);

  uint32_t imageIdx;
  m_swapchain.AcquireNextImage(m_presentationResources.imageAvailable, &imageIdx);

  auto currentCmdBuf = m_cmdBuffersDrawMain[m_presentationResources.currentFrame];

  VkSemaphore waitSemaphores[] = {m_presentationResources.imageAvailable};
  VkPipelineStageFlags waitStages[] = {VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};

  BuildCommandBufferSimple(currentCmdBuf, m_frameBuffers[imageIdx], m_swapchain.GetAttachment(imageIdx).view,
                           m_basicForwardPipeline.pipeline);

  VkSubmitInfo submitInfo = {};
  submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
  submitInfo.waitSemaphoreCount = 1;
  submitInfo.pWaitSemaphores = waitSemaphores;
  submitInfo.pWaitDstStageMask = waitStages;
  submitInfo.commandBufferCount = 1;
  submitInfo.pCommandBuffers = &currentCmdBuf;

  VkSemaphore signalSemaphores[] = {m_presentationResources.renderingFinished};
  submitInfo.signalSemaphoreCount = 1;
  submitInfo.pSignalSemaphores = signalSemaphores;

  VK_CHECK_RESULT(vkQueueSubmit(m_graphicsQueue, 1, &submitInfo, m_frameFences[m_presentationResources.currentFrame]));

  VkResult presentRes = m_swapchain.QueuePresent(m_presentationResources.queue, imageIdx,
                                                 m_presentationResources.renderingFinished);

  if (presentRes == VK_ERROR_OUT_OF_DATE_KHR || presentRes == VK_SUBOPTIMAL_KHR)
  {
    RecreateSwapChain();
  }
  else if (presentRes != VK_SUCCESS)
  {
    RUN_TIME_ERROR("Failed to present swapchain image");
  }

  m_presentationResources.currentFrame = (m_presentationResources.currentFrame + 1) % m_framesInFlight;

  vkQueueWaitIdle(m_presentationResources.queue);
}

void SimpleRender::DrawFrame(float a_time, DrawMode a_mode)
{
  UpdateUniformBuffer(a_time);
  switch (a_mode)
  {
  case DrawMode::WITH_GUI:
    SetupGUIElements();
    DrawFrameWithGUI();
    break;
  case DrawMode::NO_GUI:
    DrawFrameSimple();
    break;
  default:
    DrawFrameSimple();
  }
}


/////////////////////////////////

void SimpleRender::SetupGUIElements()
{
  ImGui_ImplVulkan_NewFrame();
  ImGui_ImplGlfw_NewFrame();
  ImGui::NewFrame();
  {
//    ImGui::ShowDemoWindow();
    ImGui::Begin("Simple render settings");

    ImGui::ColorEdit3("Meshes base color", m_uniforms.baseColor.M, ImGuiColorEditFlags_PickerHueWheel | ImGuiColorEditFlags_NoInputs);
    ImGui::Checkbox("Animate light source color", &m_uniforms.animateLightColor);
    ImGui::SliderFloat3("Light source position", m_uniforms.lightPos.M, -10.f, 10.f);

    ImGui::Text("(%.2f, %.2f, %.2f)", m_light_direction.x, m_light_direction.y, m_light_direction.z);
    ImGui::Text("Application average %.3f ms/frame (%.1f FPS)", 1000.0f / ImGui::GetIO().Framerate, ImGui::GetIO().Framerate);

    ImGui::NewLine();

    ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f),"Press 'B' to recompile and reload shaders");
    ImGui::Text("Changing bindings is not supported.");
    ImGui::Text("Vertex shader path: %s", VERTEX_SHADER_PATH.c_str());
    ImGui::Text("Fragment shader path: %s", FRAGMENT_SHADER_PATH.c_str());
    ImGui::End();
  }

  // Rendering
  ImGui::Render();
}

void SimpleRender::DrawFrameWithGUI()
{
  vkWaitForFences(m_device, 1, &m_frameFences[m_presentationResources.currentFrame], VK_TRUE, UINT64_MAX);
  vkResetFences(m_device, 1, &m_frameFences[m_presentationResources.currentFrame]);

  uint32_t imageIdx;
  auto result = m_swapchain.AcquireNextImage(m_presentationResources.imageAvailable, &imageIdx);
  if (result == VK_ERROR_OUT_OF_DATE_KHR)
  {
    RecreateSwapChain();
    return;
  }
  else if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR)
  {
    RUN_TIME_ERROR("Failed to acquire the next swapchain image!");
  }

  auto currentCmdBuf = m_cmdBuffersDrawMain[m_presentationResources.currentFrame];

  VkSemaphore waitSemaphores[] = {m_presentationResources.imageAvailable};
  VkPipelineStageFlags waitStages[] = {VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};

  BuildCommandBufferSimple(currentCmdBuf, m_frameBuffers[imageIdx], m_swapchain.GetAttachment(imageIdx).view,
    m_basicForwardPipeline.pipeline);

  ImDrawData* pDrawData = ImGui::GetDrawData();
  auto currentGUICmdBuf = m_pGUIRender->BuildGUIRenderCommand(imageIdx, pDrawData);

  std::vector<VkCommandBuffer> submitCmdBufs = { currentCmdBuf, currentGUICmdBuf};

  VkSubmitInfo submitInfo = {};
  submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
  submitInfo.waitSemaphoreCount = 1;
  submitInfo.pWaitSemaphores = waitSemaphores;
  submitInfo.pWaitDstStageMask = waitStages;
  submitInfo.commandBufferCount = (uint32_t)submitCmdBufs.size();
  submitInfo.pCommandBuffers = submitCmdBufs.data();

  VkSemaphore signalSemaphores[] = {m_presentationResources.renderingFinished};
  submitInfo.signalSemaphoreCount = 1;
  submitInfo.pSignalSemaphores = signalSemaphores;

  VK_CHECK_RESULT(vkQueueSubmit(m_graphicsQueue, 1, &submitInfo, m_frameFences[m_presentationResources.currentFrame]));

  VkResult presentRes = m_swapchain.QueuePresent(m_presentationResources.queue, imageIdx,
    m_presentationResources.renderingFinished);

  if (presentRes == VK_ERROR_OUT_OF_DATE_KHR || presentRes == VK_SUBOPTIMAL_KHR)
  {
    RecreateSwapChain();
  }
  else if (presentRes != VK_SUCCESS)
  {
    RUN_TIME_ERROR("Failed to present swapchain image");
  }

  m_presentationResources.currentFrame = (m_presentationResources.currentFrame + 1) % m_framesInFlight;

  vkQueueWaitIdle(m_presentationResources.queue);
}

void SimpleRender::ClearGBuffer()
{
  if (m_gbuffer.renderpass != VK_NULL_HANDLE)
  {
    vkDestroyRenderPass(m_device, m_gbuffer.renderpass, nullptr);
    m_gbuffer.renderpass = VK_NULL_HANDLE;
  }

  if (m_mainPassFrameBuffer != VK_NULL_HANDLE)
  {
    //vkDestroyFramebuffer(m_device, fb, nullptr);
    vkDestroyFramebuffer(m_device, m_mainPassFrameBuffer, nullptr);
    m_mainPassFrameBuffer = VK_NULL_HANDLE;
  }

  m_frameBuffers.clear();

  auto clearLayer = [this](GBufferLayer& layer)
  {
    // if (layer.image.view != VK_NULL_HANDLE)
    // {
    //   vkDestroyImageView(m_device, layer.image.view, nullptr);
    //   layer.image.view = VK_NULL_HANDLE;
    // }

    // if (layer.image.image != VK_NULL_HANDLE)
    // {
    //   vkDestroyImage(m_device, layer.image.image, nullptr);
    //   layer.image.image = VK_NULL_HANDLE;
    // }

    // if (layer.image.mem != VK_NULL_HANDLE)
    // {
    //   vkFreeMemory(m_device, layer.image.mem, nullptr);
    //   layer.image.mem = nullptr;
    // }
    vk_utils::deleteImg(m_device, &layer.image);
  };

  for (auto& layer : m_gbuffer.color_layers)
  {
    clearLayer(layer);
  }

  m_gbuffer.color_layers.clear();

  clearLayer(m_gbuffer.depth_stencil_layer);
}

void SimpleRender::ClearPostFx()
{
  for (auto framebuf : m_frameBuffers)
  {
    vkDestroyFramebuffer(m_device, framebuf, nullptr);
  }

  m_frameBuffers.clear();

  if (m_postFxRenderPass != VK_NULL_HANDLE)
  {
    vkDestroyRenderPass(m_device, m_postFxRenderPass, nullptr);
    m_postFxRenderPass = VK_NULL_HANDLE;
  }
}

void SimpleRender::CreateGBuffer()
{
  auto makeLayer = [this](VkFormat format, VkImageUsageFlagBits usage) {
    GBufferLayer result{};

    if (usage & VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT) {
      result.image.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    }

    if (usage & VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT) {
      result.image.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    }

    vk_utils::createImgAllocAndBind(m_device, m_physicalDevice, m_width, m_height,
      format, usage, &result.image);

    return result;
  };

  std::array layers {
    // Normal
    std::tuple{VK_FORMAT_R16G16B16A16_SFLOAT,
      VkImageUsageFlagBits(VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT)},
    // Tangent
    std::tuple{VK_FORMAT_R16G16B16A16_SFLOAT,
      VkImageUsageFlagBits(VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT)},
    // Albedo
    std::tuple{VK_FORMAT_R8G8B8A8_UNORM,
      VkImageUsageFlagBits(VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT)},
  };

  m_gbuffer.color_layers.reserve(layers.size());

  for (auto[format, usage] : layers) {
    m_gbuffer.color_layers.push_back(makeLayer(format, usage));
  }


  std::vector<VkFormat> depthFormats = {
    VK_FORMAT_D32_SFLOAT,
    VK_FORMAT_D32_SFLOAT_S8_UINT,
    VK_FORMAT_D24_UNORM_S8_UINT,
    VK_FORMAT_D16_UNORM_S8_UINT,
    VK_FORMAT_D16_UNORM,
  };
  VkFormat dformat;
  vk_utils::getSupportedDepthFormat(m_physicalDevice, depthFormats, &dformat);

  m_gbuffer.depth_stencil_layer = makeLayer(dformat,
    VkImageUsageFlagBits(VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT));

  m_gbuffer.resolved.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
  vk_utils::createImgAllocAndBind(m_device, m_physicalDevice, m_width, m_height,
    VK_FORMAT_R8G8B8A8_SRGB, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, &m_gbuffer.resolved);


  // Renderpass
  {
    std::array<VkAttachmentDescription, layers.size() + 2> attachmentDescs;

    attachmentDescs.fill(VkAttachmentDescription {
      .samples = VK_SAMPLE_COUNT_1_BIT,
      .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
      .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
      .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
      .stencilStoreOp = VK_ATTACHMENT_STORE_OP_STORE,
      .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
      .finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
    });

    // Color GBuffer layers
    for (std::size_t i = 0; i < layers.size(); ++i) {
      attachmentDescs[i].format = m_gbuffer.color_layers[i].image.format;
    }

    // Depth layer
    {
      auto& depth = attachmentDescs[layers.size()];
      depth.format = m_gbuffer.depth_stencil_layer.image.format;
      depth.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    }
    // Present image
    {
      auto& present = attachmentDescs[layers.size() + 1];
      present.format = m_gbuffer.resolved.format;
      present.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
      present.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    }

    std::array<VkAttachmentReference, layers.size()> gBufferColorRefs;
    for (std::size_t i = 0; i < layers.size(); ++i)
    {
      gBufferColorRefs[i] = VkAttachmentReference
        {static_cast<uint32_t>(i), VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    }

    VkAttachmentReference depthRef
      {static_cast<uint32_t>(layers.size()), VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};

    std::array resolveColorRefs {
      VkAttachmentReference
      {static_cast<uint32_t>(layers.size() + 1), VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL}
    };

    std::array<VkAttachmentReference, layers.size() + 1> resolveInputRefs;
    for (std::size_t i = 0; i <= layers.size(); ++i)
    {
      resolveInputRefs[i] = VkAttachmentReference
        {static_cast<uint32_t>(i), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    }


    std::array subpasses {
      VkSubpassDescription {
        .pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
        .colorAttachmentCount = static_cast<uint32_t>(gBufferColorRefs.size()),
        .pColorAttachments = gBufferColorRefs.data(),
        .pDepthStencilAttachment = &depthRef,
      },
      VkSubpassDescription {
        .pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
        .inputAttachmentCount = static_cast<uint32_t>(resolveInputRefs.size()),
        .pInputAttachments = resolveInputRefs.data(),
        .colorAttachmentCount = static_cast<uint32_t>(resolveColorRefs.size()),
        .pColorAttachments = resolveColorRefs.data(),
      }
    };

    // Use subpass dependencies for attachment layout transitions
    std::array dependencies {
      VkSubpassDependency {
        .srcSubpass = 0,
        .dstSubpass = 1,
        // Source is gbuffer being written
        .srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
        // Destination is reading gbuffer as input attachments in fragment shader
        .dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
        .srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
        .dstAccessMask = VK_ACCESS_INPUT_ATTACHMENT_READ_BIT,
        .dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT,
      },
      VkSubpassDependency {
        .srcSubpass = 1,
        .dstSubpass = VK_SUBPASS_EXTERNAL,
        .srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
        .dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
        .srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
        .dstAccessMask = VK_ACCESS_SHADER_READ_BIT,
        .dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT,
      }
    };

    VkRenderPassCreateInfo renderPassInfo {
      .sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
      .attachmentCount = static_cast<uint32_t>(attachmentDescs.size()),
      .pAttachments = attachmentDescs.data(),
      .subpassCount = static_cast<uint32_t>(subpasses.size()),
      .pSubpasses = subpasses.data(),
      .dependencyCount = static_cast<uint32_t>(dependencies.size()),
      .pDependencies = dependencies.data(),
    };

    VK_CHECK_RESULT(vkCreateRenderPass(m_device, &renderPassInfo, nullptr, &m_gbuffer.renderpass));
  }

  // Framebuffers

    std::array<VkImageView, layers.size() + 2> attachments;
    for (std::size_t j = 0; j < layers.size(); ++j)
    {
      attachments[j] = m_gbuffer.color_layers[j].image.view;
    }

    attachments[layers.size()] = m_gbuffer.depth_stencil_layer.image.view;
    attachments.back() = m_gbuffer.resolved.view;

    VkFramebufferCreateInfo fbufCreateInfo {
    .sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
    .pNext = NULL,
    .renderPass = m_gbuffer.renderpass,
    .attachmentCount = static_cast<uint32_t>(attachments.size()),
    .pAttachments = attachments.data(),
    .width = m_width,
    .height = m_height,
    .layers = 1,
  };
    VK_CHECK_RESULT(vkCreateFramebuffer(m_device, &fbufCreateInfo, nullptr, &m_mainPassFrameBuffer));
}

void SimpleRender::ClearShadowmap()
{
  if (m_shadowMapRenderPass != VK_NULL_HANDLE)
  {
    vkDestroyRenderPass(m_device, m_shadowMapRenderPass, nullptr);
    m_shadowMapRenderPass = VK_NULL_HANDLE;
  }

  for (auto fb : m_shadowMapFrameBuffers)
  {
    vkDestroyFramebuffer(m_device, fb, nullptr);
  }

  m_shadowMapFrameBuffers.clear();

  auto clearLayer = [this](GBufferLayer& layer)
  {
    if (layer.image.view != VK_NULL_HANDLE)
    {
      vkDestroyImageView(m_device, layer.image.view, nullptr);
      layer.image.view = VK_NULL_HANDLE;
    }

    if (layer.image.image != VK_NULL_HANDLE)
    {
      vkDestroyImage(m_device, layer.image.image, nullptr);
      layer.image.image = VK_NULL_HANDLE;
    }

    if (layer.image.mem != VK_NULL_HANDLE)
    {
      vkFreeMemory(m_device, layer.image.mem, nullptr);
      layer.image.mem = nullptr;
    }
  };

  clearLayer(m_shadow_map);
}

void SimpleRender::CreatePostFx()
{
  // Renderpass
  {
    std::array attachmentDescs{
      VkAttachmentDescription {
        .format = m_swapchain.GetFormat(),
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
        .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
        // no stencil in present img
        .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
        .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        .finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
      },
    };

    std::array postfxColorRefs{
      VkAttachmentReference{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL},
    };

    std::array subpasses {
      VkSubpassDescription {
        .pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
        .colorAttachmentCount = static_cast<uint32_t>(postfxColorRefs.size()),
        .pColorAttachments = postfxColorRefs.data(),
      },
    };

    // Use subpass dependencies for attachment layout transitions
    std::array dependencies {
      VkSubpassDependency {
        .srcSubpass = VK_SUBPASS_EXTERNAL,
        .dstSubpass = 0,
        // Source is THE PRESENT SEMAPHORE BEING SIGNALED ON THIS PRECISE STAGE!!!!!
        .srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
        // Destination is swapchain image being filled with gbuffer resolution
        .dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
        // Semaphore waiting doesn't do any memory ops
        .srcAccessMask = {},
        .dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
        .dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT,
      },
    };

    VkRenderPassCreateInfo renderPassInfo {
      .sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
      .attachmentCount = static_cast<uint32_t>(attachmentDescs.size()),
      .pAttachments = attachmentDescs.data(),
      .subpassCount = static_cast<uint32_t>(subpasses.size()),
      .pSubpasses = subpasses.data(),
      .dependencyCount = static_cast<uint32_t>(dependencies.size()),
      .pDependencies = dependencies.data(),
    };

    VK_CHECK_RESULT(vkCreateRenderPass(m_device, &renderPassInfo, nullptr, &m_postFxRenderPass));
  }

  // Framebuffer
  m_frameBuffers.resize(m_swapchain.GetImageCount());
  for (uint32_t i = 0; i < m_frameBuffers.size(); ++i)
  {
    std::array attachments{
      m_swapchain.GetAttachment(i).view,
    };

    VkFramebufferCreateInfo fbufCreateInfo {
      .sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
      .pNext = NULL,
      .renderPass = m_postFxRenderPass,
      .attachmentCount = static_cast<uint32_t>(attachments.size()),
      .pAttachments = attachments.data(),
      .width = m_width,
      .height = m_height,
      .layers = 1,
    };

    VK_CHECK_RESULT(vkCreateFramebuffer(m_device, &fbufCreateInfo, nullptr, &m_frameBuffers[i]));
  }
}

void SimpleRender::CreateShadowmap()
{
  auto makeLayer = [this](VkFormat format, VkImageUsageFlagBits usage) {
    GBufferLayer result{};

    if (usage & VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT) {
      result.image.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    }

    if (usage & VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT) {
      result.image.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    }

    vk_utils::createImgAllocAndBind(m_device, m_physicalDevice, m_width, m_height,
      format, usage, &result.image);

    return result;
  };

  std::vector<VkFormat> depthFormats = {
    VK_FORMAT_D32_SFLOAT,
    VK_FORMAT_D32_SFLOAT_S8_UINT,
    VK_FORMAT_D24_UNORM_S8_UINT,
    VK_FORMAT_D16_UNORM_S8_UINT,
    VK_FORMAT_D16_UNORM,
  };
  VkFormat dformat;
  vk_utils::getSupportedDepthFormat(m_physicalDevice, depthFormats, &dformat);

  m_shadow_map = makeLayer(dformat,
    VkImageUsageFlagBits(VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT));



  // Renderpass
  {
    VkAttachmentDescription depthAttachmentDesc = {
      .samples = VK_SAMPLE_COUNT_1_BIT,
      .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
      .storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
      .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
      .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
      .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
      .finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
    };

    // Depth layer
    {
      auto& depth = depthAttachmentDesc;
      depth.format = m_shadow_map.image.format;
      depth.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
      depth.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    }

    VkAttachmentReference depthRef
      {static_cast<uint32_t>(0), VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};


    std::array subpasses {
      VkSubpassDescription {
        .pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
        .colorAttachmentCount = 0,
        .pDepthStencilAttachment = &depthRef
      }
    };

    // Use subpass dependencies for attachment layout transitions
    std::array dependencies {
      VkSubpassDependency {
        .srcSubpass = VK_SUBPASS_EXTERNAL,
        .dstSubpass = 0,
        // Source is THE PRESENT SEMAPHORE BEING SIGNALED ON THIS PRECISE STAGE!!!!!
        .srcStageMask = VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
        // Destination is swapchain image being filled with gbuffer resolution
        .dstStageMask = VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
        // Semaphore waiting doesn't do any memory ops
        .srcAccessMask = {},
        .dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
        .dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT,
      }
    };

    VkRenderPassCreateInfo renderPassInfo {
      .sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
      .attachmentCount = 1,
      .pAttachments = &depthAttachmentDesc,
      .subpassCount = static_cast<uint32_t>(subpasses.size()),
      .pSubpasses = subpasses.data(),
      .dependencyCount = static_cast<uint32_t>(dependencies.size()),
      .pDependencies = dependencies.data(),
    };

    VK_CHECK_RESULT(vkCreateRenderPass(m_device, &renderPassInfo, nullptr, &m_shadowMapRenderPass));
  }

  // Framebuffers
  m_shadowMapFrameBuffers.resize(m_swapchain.GetImageCount());
  for (std::size_t i = 0; i < m_frameBuffers.size(); ++i)
  {
    std::array<VkImageView, 1> attachments;

    attachments[0] = m_shadow_map.image.view;

    VkFramebufferCreateInfo fbufCreateInfo {
      .sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
      .pNext = NULL,
      .renderPass = m_shadowMapRenderPass,
      .attachmentCount = static_cast<uint32_t>(attachments.size()),
      .pAttachments = attachments.data(),
      .width = m_width,
      .height = m_height,
      .layers = 1,
    };
    VK_CHECK_RESULT(vkCreateFramebuffer(m_device, &fbufCreateInfo, nullptr, &m_shadowMapFrameBuffers[i]));
  }
}