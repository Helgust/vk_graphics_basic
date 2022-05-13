#ifndef SIMPLE_RENDER_H
#define SIMPLE_RENDER_H

#define VK_NO_PROTOTYPES

#include "../../render/scene_mgr.h"
#include "../../render/render_common.h"
#include "../../render/render_gui.h"
#include "../../../resources/shaders/common.h"
#include <geom/vk_mesh.h>
#include <vk_descriptor_sets.h>
#include <vk_fbuf_attachment.h>
#include <vk_images.h>
#include <vk_swapchain.h>
#include <string>
#include <iostream>

struct GBufferLayer
{
  vk_utils::VulkanImageMem image{};
};

struct GBuffer
{
  std::vector<GBufferLayer> color_layers;
  GBufferLayer depth_stencil_layer;
  vk_utils::VulkanImageMem resolved;
  VkRenderPass renderpass{VK_NULL_HANDLE};
};

class SimpleRender : public IRender
{
public:
  const std::string GBUFFER_VERTEX_SHADER_PATH = "../resources/shaders/gbuffer.vert";
  const std::string GBUFFER_FRAGMENT_SHADER_PATH = "../resources/shaders/gbuffer.frag";
  
  const std::string SSAO_FRAGMENT_SHADER_PATH = "../resources/shaders/ssao.frag";

  const std::string SHADING_VERTEX_SHADER_PATH = "../resources/shaders/shading.vert";
  const std::string SHADING_FRAGMENT_SHADER_PATH = "../resources/shaders/shading.frag";

  const std::string POSTFX_FRAGMENT_SHADER_PATH = "../resources/shaders/postfx.frag";

  const std::string FULLSCREEN_QUAD3_VERTEX_SHADER_PATH = "../resources/shaders/quad3_vert.vert";

  const std::string VERTEX_SHADER_PATH = "../resources/shaders/simple.vert";
  const std::string FRAGMENT_SHADER_PATH = "../resources/shaders/simple.frag";

  const uint32_t POSTFX_DOWNSCALE_FACTOR = 4;

  static constexpr uint32_t SSAO_KERNEL_SIZE = 64;
  static constexpr uint32_t SSAO_KERNEL_SIZE_BYTES = 4*sizeof(float)*SSAO_KERNEL_SIZE;
  static constexpr uint32_t SSAO_NOISE_DIM = 8;
  static constexpr float SSAO_RADIUS = 0.5f;

  SimpleRender(uint32_t a_width, uint32_t a_height);
  ~SimpleRender() override  { Cleanup(); };

  inline uint32_t     GetWidth()      const override { return m_width; }
  inline uint32_t     GetHeight()     const override { return m_height; }
  inline VkInstance   GetVkInstance() const override { return m_instance; }
  void InitVulkan(const char** a_instanceExtensions, uint32_t a_instanceExtensionsCount, uint32_t a_deviceId) override;

  void InitPresentation(VkSurfaceKHR& a_surface, bool initGUI) override;

  void ProcessInput(const AppInput& input) override;
  void UpdateCamera(const Camera* cams, uint32_t a_camsCount) override;
  Camera GetCurrentCamera() override {return m_cam;}
  void UpdateView();

  void LoadScene(const char *path, bool transpose_inst_matrices) override;
  void DrawFrame(float a_time, DrawMode a_mode) override;

  void ClearPipeline(pipeline_data_t &pipeline);

  //////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

  // debugging utils
  //
  static VKAPI_ATTR VkBool32 VKAPI_CALL debugReportCallbackFn(
    VkDebugReportFlagsEXT                       flags,
    VkDebugReportObjectTypeEXT                  objectType,
    uint64_t                                    object,
    size_t                                      location,
    int32_t                                     messageCode,
    const char* pLayerPrefix,
    const char* pMessage,
    void* pUserData)
  {
    (void)flags;
    (void)objectType;
    (void)object;
    (void)location;
    (void)messageCode;
    (void)pUserData;
    std::cout << pLayerPrefix << ": " << pMessage << std::endl;
    return VK_FALSE;
  }

  VkDebugReportCallbackEXT m_debugReportCallback = nullptr;
protected:

  VkInstance       m_instance       = VK_NULL_HANDLE;
  VkCommandPool    m_commandPool    = VK_NULL_HANDLE;
  VkPhysicalDevice m_physicalDevice = VK_NULL_HANDLE;
  VkDevice         m_device         = VK_NULL_HANDLE;
  VkQueue          m_graphicsQueue  = VK_NULL_HANDLE;
  VkQueue          m_transferQueue  = VK_NULL_HANDLE;

  vk_utils::QueueFID_T m_queueFamilyIDXs {UINT32_MAX, UINT32_MAX, UINT32_MAX};

  struct
  {
    uint32_t    currentFrame      = 0u;
    VkQueue     queue             = VK_NULL_HANDLE;
    VkSemaphore imageAvailable    = VK_NULL_HANDLE;
    VkSemaphore renderingFinished = VK_NULL_HANDLE;
  } m_presentationResources;

  std::vector<VkFence> m_frameFences;
  std::vector<VkCommandBuffer> m_cmdBuffersDrawMain;

  PushConst pushConst;

  float4 meshColors[3] = {
      float4(1.f, 1.f, 1.f, 1.f),
      float4(1.f, 1.f,1.f, 1.f),
      float4(1.f, 1.f, 1.f, 1.f),
    };

  // struct
  // {
  //   LiteMath::float4x4 projView;
  //   LiteMath::float4x4 model;
  // } pushConst2M;

  UniformParams m_uniforms {};
  VkBuffer m_ubo = VK_NULL_HANDLE;
  VkDeviceMemory m_uboAlloc = VK_NULL_HANDLE;
  void* m_uboMappedMem = nullptr;

  VkSampler m_ImageSampler;

  VkDeviceMemory m_indirectRenderingMemory = VK_NULL_HANDLE;

  pipeline_data_t m_basicForwardPipeline {};
  pipeline_data_t m_gBufferPipeline {};
  pipeline_data_t m_shadingPipeline {};
  pipeline_data_t m_shadowmapPipeline {};
  pipeline_data_t m_postFxPipeline {};
  pipeline_data_t m_ssaoPipeline;

  VkDescriptorSet m_graphicsDescriptorSet = VK_NULL_HANDLE;
  VkDescriptorSetLayout m_graphicsDescriptorSetLayout = VK_NULL_HANDLE;

  VkDescriptorSet m_lightingDescriptorSet = VK_NULL_HANDLE;
  VkDescriptorSetLayout m_lightingDescriptorSetLayout = VK_NULL_HANDLE;

  VkDescriptorSet m_lightingFragmentDescriptorSet = VK_NULL_HANDLE;
  VkDescriptorSetLayout m_lightingFragmentDescriptorSetLayout = VK_NULL_HANDLE;

  VkDescriptorSet m_postFxDescriptorSet = VK_NULL_HANDLE;
  VkDescriptorSetLayout m_postFxDescriptorSetLayout = VK_NULL_HANDLE;

  VkDescriptorSet m_ssaoDescriptorSet = VK_NULL_HANDLE;
  VkDescriptorSetLayout m_ssaoDescriptorSetLayout = VK_NULL_HANDLE;

  VkDescriptorSet m_dSet = VK_NULL_HANDLE;
  VkDescriptorSetLayout m_dSetLayout = VK_NULL_HANDLE;
  

  
  VkRenderPass m_screenRenderPass = VK_NULL_HANDLE; // main renderpass
  VkRenderPass m_postFxRenderPass;

  std::vector<VkFramebuffer> m_frameBuffers;
  
  VkFramebuffer m_prePostFxFramebuffer;

  VkRenderPass m_prePostFxRenderPass;
  vk_utils::VulkanImageMem m_ssaoImage;
  vk_utils::VulkanImageMem m_ssaoNoise;
  VkSampler m_noiseSampler;
  VkBuffer m_ssaoKernel = VK_NULL_HANDLE;

  std::shared_ptr<vk_utils::DescriptorMaker> m_pBindings = nullptr;
  vk_utils::DescriptorMaker& GetDescMaker();

  // *** presentation
  VkSurfaceKHR m_surface = VK_NULL_HANDLE;
  VulkanSwapChain m_swapchain;
  VkFramebuffer m_mainPassFrameBuffer;
  vk_utils::VulkanImageMem m_depthBuffer{};
  // ***

  // *** GUI
  std::shared_ptr<IRenderGUI> m_pGUIRender;
  virtual void SetupGUIElements();
  void DrawFrameWithGUI();
  //

  Camera   m_cam;
  uint32_t m_width  = 1024u;
  uint32_t m_height = 1024u;
  uint32_t m_framesInFlight  = 2u;
  bool m_vsync = false;
  bool m_ssao = true;
  float m_sunAngle = 0.5f;

  VkPhysicalDeviceFeatures m_enabledDeviceFeatures = {};
  std::vector<const char*> m_deviceExtensions      = {};
  std::vector<const char*> m_instanceExtensions    = {};

  bool m_enableValidation;
  std::vector<const char*> m_validationLayers;

  std::shared_ptr<SceneManager> m_pScnMgr;

  GBuffer m_gbuffer;

  GBufferLayer m_shadow_map;
  std::vector<VkFramebuffer> m_shadowMapFrameBuffers;
  VkRenderPass m_shadowMapRenderPass;

  vec3 m_light_direction = {0.0f, 0.0f, -1.0f};
  vec3 m_light_position = {0.0f, 0.0f, 0.0f};
  float m_light_radius = 50;
  float m_light_length = 1000;

  void DrawFrameSimple();

  void CreateInstance();
  void CreateDevice(uint32_t a_deviceId);

  void BuildCommandBufferSimple(VkCommandBuffer cmdBuff, VkFramebuffer frameBuff,
                                VkImageView a_targetImageView, VkPipeline a_pipeline);
  virtual void SetupGBufferPipeline();
  virtual void SetupShadingPipeline();
  virtual void SetupPostfxPipeline();
  void SetupShadowmapPipeline();

  void CleanupPipelineAndSwapchain();
  void RecreateSwapChain();

  void CreateUniformBuffer();
  void UpdateUniformBuffer(float a_time);

  void Cleanup();

  void SetupDeviceFeatures();
  void SetupDeviceExtensions();
  void SetupValidationLayers();

  void ClearGBuffer();
  void ClearPostFx();
  void CreateGBuffer();
  void CreatePostFx();

  void ClearShadowmap();
  void CreateShadowmap();
};


#endif //SIMPLE_RENDER_H
