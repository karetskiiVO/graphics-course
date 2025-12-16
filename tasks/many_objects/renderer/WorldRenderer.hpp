#pragma once

#include <etna/Image.hpp>
#include <etna/Sampler.hpp>
#include <etna/Buffer.hpp>
#include <etna/GraphicsPipeline.hpp>
#include <glm/glm.hpp>

#include "scene/SceneManager.hpp"
#include "wsi/Keyboard.hpp"

#include "FramePacket.hpp"

class WorldRenderer
{
public:
  static const size_t maxInstanceCount = 4096;

  WorldRenderer();

  void loadScene(std::filesystem::path path);

  void loadShaders();
  void allocateResources(glm::uvec2 swapchain_resolution);
  void setupPipelines(vk::Format swapchain_format);

  void debugInput(const Keyboard& kb);
  void update(const FramePacket& packet);
  void drawGui();
  void renderWorld(
    vk::CommandBuffer cmd_buf, 
    vk::Image target_image, 
    vk::ImageView target_image_view
  );

  bool visible(
    const Bounds& bounds, 
    const glm::mat4& gTransform, 
    const glm::mat4& transform
  ) {
    static glm::vec3 corners[] = {
      { 1.0f, -1.0f, -1.0f},
      {-1.0f,  1.0f, -1.0f},
      {-1.0f, -1.0f,  1.0f},
      {-1.0f, -1.0f, -1.0f},
      { 1.0f,  1.0f,  1.0f},
      { 1.0f,  1.0f, -1.0f},
      { 1.0f, -1.0f,  1.0f},
      {-1.0f,  1.0f,  1.0f},
    };

    auto minpos = glm::vec3{
      std::numeric_limits<float>::max(),
      std::numeric_limits<float>::max(),
      std::numeric_limits<float>::max(),
    };    
    auto maxpos = glm::vec3{
      std::numeric_limits<float>::min(),
      std::numeric_limits<float>::min(),
      std::numeric_limits<float>::min(),
    };

    auto resTransform = gTransform * transform;
    for (auto& corner : corners) {
      auto proj = resTransform * glm::vec4(bounds.origin + (corner * bounds.extents), 1.0f);
      auto current = glm::vec3(proj);
      current /= proj.w;

      minpos = glm::min(current, minpos);
      maxpos = glm::max(current, maxpos);
    }

    return 
      minpos.z <=  1.0f &&
      maxpos.z >= -1.0f &&
      minpos.x <=  1.0f &&
      maxpos.x >= -1.0f &&
      minpos.y <=  1.0f &&
      maxpos.y >= -1.0f;
  }

private:
  void renderScene(
    vk::CommandBuffer cmd_buf, 
    const glm::mat4x4& glob_tm, 
    vk::PipelineLayout pipeline_layout,
    etna::Buffer& instances
  );

  void parseInstanceInfo(etna::Buffer& current_buffer, const glm::mat4x4& glob_tm);

private:
  std::unique_ptr<SceneManager> sceneMgr;

  etna::Image mainViewDepth;
  etna::Buffer constants;

  struct PushConstants
  {
    glm::mat4x4 projView;
    glm::mat4x4 model;
  } pushConst2M;

  glm::mat4x4 worldViewProj;
  glm::mat4x4 lightMatrix;

  etna::GraphicsPipeline staticMeshPipeline{};

  glm::uvec2 resolution;

  std::optional<etna::GpuSharedResource<etna::Buffer>> instanceTransformBuffer;
  std::vector<uint32_t> instancesBatch;
};
