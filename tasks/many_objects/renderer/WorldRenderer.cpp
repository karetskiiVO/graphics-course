#include "WorldRenderer.hpp"

#include <etna/GlobalContext.hpp>
#include <etna/PipelineManager.hpp>
#include <etna/RenderTargetStates.hpp>
#include <etna/Profiling.hpp>
#include <glm/ext.hpp>

#ifndef MANY_OBJECTS_RENDERER_SHADERS_ROOT
#define MANY_OBJECTS_RENDERER_SHADERS_ROOT ""
#endif


WorldRenderer::WorldRenderer() :
  sceneMgr{std::make_unique<SceneManager>()},
  instanceTransformBuffer(),
  instancesBatch(maxInstanceCount, 0)
{
}

void WorldRenderer::allocateResources(glm::uvec2 swapchain_resolution)
{
  resolution = swapchain_resolution;

  auto& ctx = etna::get_context();

  mainViewDepth = ctx.createImage(etna::Image::CreateInfo{
    .extent = vk::Extent3D{resolution.x, resolution.y, 1},
    .name = "main_view_depth",
    .format = vk::Format::eD32Sfloat,
    .imageUsage = vk::ImageUsageFlagBits::eDepthStencilAttachment,
  });

  instanceTransformBuffer.emplace(
    ctx.getMainWorkCount(), 
    [&] (size_t i) {
      return ctx.createBuffer(etna::Buffer::CreateInfo{
        .size = sizeof(glm::mat4x4) * maxInstanceCount,
        .bufferUsage = vk::BufferUsageFlagBits::eVertexBuffer | vk::BufferUsageFlagBits::eTransferDst | vk::BufferUsageFlagBits::eStorageBuffer,
        .memoryUsage = VMA_MEMORY_USAGE_CPU_TO_GPU,
        .name = fmt::format("{}", i)});
    });
}

void WorldRenderer::loadScene(std::filesystem::path path)
{
  sceneMgr->selectScene(path, true);
}

void WorldRenderer::loadShaders()
{
  etna::create_program(
    "static_mesh_material",
    {MANY_OBJECTS_RENDERER_SHADERS_ROOT "static_mesh.frag.spv",
     MANY_OBJECTS_RENDERER_SHADERS_ROOT "static_mesh.vert.spv"});
  etna::create_program("static_mesh", {MANY_OBJECTS_RENDERER_SHADERS_ROOT "static_mesh.vert.spv"});
}

void WorldRenderer::setupPipelines(vk::Format swapchain_format)
{
  etna::VertexShaderInputDescription sceneVertexInputDesc{
    .bindings = {etna::VertexShaderInputDescription::Binding{
      .byteStreamDescription = sceneMgr->getCompressedVertexFormatDescription(),
    }},
  };

  auto& pipelineManager = etna::get_context().getPipelineManager();

  staticMeshPipeline = {};
  staticMeshPipeline = pipelineManager.createGraphicsPipeline(
    "static_mesh_material",
    etna::GraphicsPipeline::CreateInfo{
      .vertexShaderInput = sceneVertexInputDesc,
      .rasterizationConfig =
        vk::PipelineRasterizationStateCreateInfo{
          .polygonMode = vk::PolygonMode::eFill,
          .cullMode = vk::CullModeFlagBits::eBack,
          .frontFace = vk::FrontFace::eCounterClockwise,
          .lineWidth = 1.f,
        },
      .fragmentShaderOutput =
        {
          .colorAttachmentFormats = {swapchain_format},
          .depthAttachmentFormat = vk::Format::eD32Sfloat,
        },
    });
}

void WorldRenderer::debugInput(const Keyboard&) {}

void WorldRenderer::update(const FramePacket& packet)
{
  ZoneScoped;

  // calc camera matrix
  {
    const float aspect = float(resolution.x) / float(resolution.y);
    worldViewProj = packet.mainCam.projTm(aspect) * packet.mainCam.viewTm();
  }
}

void WorldRenderer::renderScene(
  vk::CommandBuffer cmd_buf, 
  const glm::mat4x4& glob_tm, 
  vk::PipelineLayout pipeline_layout,
  etna::Buffer& instances
) {
  if (!sceneMgr->getVertexBuffer())
    return;

  cmd_buf.bindVertexBuffers(0, {sceneMgr->getVertexBuffer()}, {0});
  cmd_buf.bindIndexBuffer(sceneMgr->getIndexBuffer(), 0, vk::IndexType::eUint32);

  pushConst2M.projView = glob_tm;

  cmd_buf.pushConstants<PushConstants>(pipeline_layout, vk::ShaderStageFlagBits::eVertex, 0, {pushConst2M});

  auto shaderInfo = etna::get_shader_program("static_mesh_material");
  auto set = etna::create_descriptor_set(
    shaderInfo.getDescriptorLayoutId(0),
    cmd_buf,
    {etna::Binding{0, instances.genBinding()}}
  );
  auto vkSet = set.getVkSet();

  cmd_buf.bindDescriptorSets(
    vk::PipelineBindPoint::eGraphics, 
    pipeline_layout, 
    0, 
    1, 
    &vkSet, 
    0, 
    nullptr
  );

  uint32_t offset = 0;
  auto elems = sceneMgr->getRenderElements();
  for (uint32_t i = 0; i < elems.size(); i++) {
    if (instancesBatch[i] > 0) {
      cmd_buf.drawIndexed(
        elems[i].indexCount, 
        instancesBatch[i], 
        elems[i].indexOffset,
        elems[i].vertexOffset, 
        offset
      );

      offset += instancesBatch[i];
    }
  }

  instancesBatch.assign(instancesBatch.size(), 0);
}

void WorldRenderer::renderWorld(
  vk::CommandBuffer cmd_buf, 
  vk::Image target_image, 
  vk::ImageView target_image_view
) {
  ETNA_PROFILE_GPU(cmd_buf, renderWorld);

  // draw final scene to screen
  {
    ETNA_PROFILE_GPU(cmd_buf, renderForward);
    auto& buffer = instanceTransformBuffer->get();
    parseInstanceInfo(buffer, worldViewProj);

    etna::RenderTargetState renderTargets(
      cmd_buf,
      {{0, 0}, {resolution.x, resolution.y}},
      {{target_image, target_image_view}},
      {mainViewDepth.get(), mainViewDepth.getView({})}
    );

    cmd_buf.bindPipeline(vk::PipelineBindPoint::eGraphics, staticMeshPipeline.getVkPipeline());
    renderScene(cmd_buf, worldViewProj, staticMeshPipeline.getVkPipelineLayout(), buffer);
  }
}

void WorldRenderer::parseInstanceInfo(etna::Buffer& buffer, const glm::mat4x4& gTransform) {
  auto instanceMeshes   = sceneMgr->getInstanceMeshes();
  auto instanceMatrices = sceneMgr->getInstanceMatrices();
  auto meshes           = sceneMgr->getMeshes();
  auto bounds           = sceneMgr->getRenderElementsBounds();

  buffer.map();

  auto data = reinterpret_cast<glm::mat4x4*>(buffer.data());

  size_t idx = 0;
  for (size_t i_ = 0; i_ < instanceMatrices.size(); i_++) {
    auto i = instanceMeshes[i_];
    auto& currentMatrix = instanceMatrices[i_];

    for (size_t j_ = 0; j_ < meshes[i].relemCount; j_++) {
      size_t j = meshes[i].firstRelem + j_;
      if (!visible(bounds[j], gTransform, currentMatrix)) continue;
    
      instancesBatch[j]++;
      data[idx] = currentMatrix;
      idx++;
    }
  }

  buffer.unmap();
}

