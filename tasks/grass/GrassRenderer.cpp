#include "GrassRenderer.hpp"

#include <array>
#include <glm/gtc/matrix_transform.hpp>

#include <etna/GlobalContext.hpp>
#include <etna/PipelineManager.hpp>
#include <etna/Profiling.hpp>
#include <etna/Etna.hpp>

GrassRenderer::GrassRenderer () {}

void GrassRenderer::AllocateResources () {
    const vk::DeviceSize instanceSize = maxBlades * 8 * sizeof(float);

    grassInstanceBuffer = etna::get_context().createBuffer(etna::Buffer::CreateInfo{
        .size        = instanceSize,
        .bufferUsage = vk::BufferUsageFlagBits::eStorageBuffer,
        .memoryUsage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE,
        .name        = "grass_instance_buffer",
    });

    grassIndirectBuffer = etna::get_context().createBuffer(etna::Buffer::CreateInfo{
        .size        = sizeof(uint32_t) * 4,
        .bufferUsage = vk::BufferUsageFlagBits::eStorageBuffer
                     | vk::BufferUsageFlagBits::eIndirectBuffer
                     | vk::BufferUsageFlagBits::eTransferDst,
        .memoryUsage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE,
        .name        = "grass_indirect_buffer",
    });
}

void GrassRenderer::LoadShaders () {
    placementShader.Create(
        "grass_placement",
        {GRASS_SHADERS_ROOT "grass_placement.comp.spv"}
    );

    grassShader.Create(
        "grass_draw",
        {
            GRASS_SHADERS_ROOT "grass.vert.spv",
            GRASS_SHADERS_ROOT "grass.frag.spv",
        }
    );
}

void GrassRenderer::SetupPipelines (vk::Format colorFormat, vk::Format depthFormat) {
    auto& pipelineMgr = etna::get_context().getPipelineManager();

    placementPipeline = pipelineMgr.createComputePipeline("grass_placement", {});

    grassPipeline = {};
    grassPipeline = pipelineMgr.createGraphicsPipeline(
        "grass_draw",
        etna::GraphicsPipeline::CreateInfo{
            .vertexShaderInput    = {},
            .rasterizationConfig  = vk::PipelineRasterizationStateCreateInfo{
                .polygonMode = vk::PolygonMode::eFill,
                .cullMode    = vk::CullModeFlagBits::eNone,
                .frontFace   = vk::FrontFace::eCounterClockwise,
                .lineWidth   = 1.0f,
            },
            .fragmentShaderOutput = {
                .colorAttachmentFormats = {colorFormat},
                .depthAttachmentFormat  = depthFormat,
            },
        });
}

std::array<glm::vec4, 6> GrassRenderer::ExtractFrustumPlanes (const glm::mat4& pv) {
    auto row = [&](int i) -> glm::vec4 {
        return {pv[0][i], pv[1][i], pv[2][i], pv[3][i]};
    };
    const glm::vec4 r0 = row(0), r1 = row(1), r2 = row(2), r3 = row(3);

    std::array<glm::vec4, 6> planes = {
        r3 + r0,
        r3 - r0,
        r3 + r1,
        r3 - r1,
        r2,
        r3 - r2,
    };
    for (auto& p : planes) {
        const float len = glm::length(glm::vec3(p));
        if (len > 1e-6f) p /= len;
    }
    return planes;
}

void GrassRenderer::Update (glm::mat4 projView, glm::vec3 camPos, float time) {
    worldViewProj    = projView;
    cameraPosition   = camPos;
    currentTime      = time;
    frustumPlanes    = ExtractFrustumPlanes(projView);
}

void GrassRenderer::DispatchPlacement (vk::CommandBuffer cmdBuf) {
    ETNA_PROFILE_GPU(cmdBuf, grassPlacement);

    cmdBuf.fillBuffer(grassIndirectBuffer.get(), 0, vk::WholeSize, 0u);

    const vk::BufferMemoryBarrier2 fillBarrier{
        .srcStageMask        = vk::PipelineStageFlagBits2::eTransfer,
        .srcAccessMask       = vk::AccessFlagBits2::eTransferWrite,
        .dstStageMask        = vk::PipelineStageFlagBits2::eComputeShader,
        .dstAccessMask       = vk::AccessFlagBits2::eShaderStorageRead
                             | vk::AccessFlagBits2::eShaderStorageWrite,
        .srcQueueFamilyIndex = vk::QueueFamilyIgnored,
        .dstQueueFamilyIndex = vk::QueueFamilyIgnored,
        .buffer              = grassIndirectBuffer.get(),
        .offset              = 0,
        .size                = vk::WholeSize,
    };
    const vk::DependencyInfo fillDep{
        .bufferMemoryBarrierCount = 1,
        .pBufferMemoryBarriers    = &fillBarrier,
    };
    cmdBuf.pipelineBarrier2(fillDep);

    etna::set_state(
        cmdBuf,
        grassInstanceBuffer.get(),
        vk::PipelineStageFlagBits2::eComputeShader,
        vk::AccessFlagBits2::eShaderStorageWrite
    );
    etna::flush_barriers(cmdBuf);

    auto shaderInfo = etna::get_shader_program("grass_placement");

    auto set = etna::create_descriptor_set(
        shaderInfo.getDescriptorLayoutId(0),
        cmdBuf,
        {
            etna::Binding{0, grassInstanceBuffer.genBinding()},
            etna::Binding{1, grassIndirectBuffer.genBinding()},
        }
    );

    cmdBuf.bindPipeline(vk::PipelineBindPoint::eCompute, placementPipeline.getVkPipeline());
    cmdBuf.bindDescriptorSets(
        vk::PipelineBindPoint::eCompute,
        placementPipeline.getVkPipelineLayout(),
        0, 
        {set.getVkSet()}, 
        {}
    );

    PlacePushConstants pc{};
    pc.camPosAndRadius = glm::vec4(cameraPosition, gridRadius);
    pc.gridSize        = gridSize;
    pc.bladeRadius     = bladeCullRadius;
    pc.pad0            = 0u;
    pc.pad1            = 0u;
    for (int i = 0; i < 6; ++i) pc.frustumPlanes[i] = frustumPlanes[i];

    cmdBuf.pushConstants(
        placementPipeline.getVkPipelineLayout(),
        vk::ShaderStageFlagBits::eCompute,
        0, 
        sizeof(pc), 
        &pc
    );

    etna::flush_barriers(cmdBuf);

    const uint32_t groupCount = (gridSize + 15u) / 16u;
    cmdBuf.dispatch(groupCount, groupCount, 1u);

    etna::set_state(
        cmdBuf,
        grassInstanceBuffer.get(),
        vk::PipelineStageFlagBits2::eVertexShader,
        vk::AccessFlagBits2::eShaderStorageRead
    );
    etna::set_state(
        cmdBuf,
        grassIndirectBuffer.get(),
        vk::PipelineStageFlagBits2::eDrawIndirect,
        vk::AccessFlagBits2::eIndirectCommandRead
    );
    etna::flush_barriers(cmdBuf);
}

void GrassRenderer::DrawBlades (vk::CommandBuffer cmdBuf) {
    ETNA_PROFILE_GPU(cmdBuf, grassDraw);

    auto shaderInfo = etna::get_shader_program("grass_draw");

    auto set = etna::create_descriptor_set(
        shaderInfo.getDescriptorLayoutId(0),
        cmdBuf,
        {etna::Binding{0, grassInstanceBuffer.genBinding()}}
    );

    cmdBuf.bindPipeline(vk::PipelineBindPoint::eGraphics, grassPipeline.getVkPipeline());

    cmdBuf.bindDescriptorSets(
        vk::PipelineBindPoint::eGraphics,
        grassPipeline.getVkPipelineLayout(),
        0,
        {set.getVkSet()},
        {}
    );

    const DrawPushConstants pc{
        .projView     = worldViewProj,
        .time         = currentTime,
        .windStrength = windStrength,
        .bladeWidth   = bladeWidth,
        .bladeHeight  = bladeHeight,
        .camPos       = cameraPosition,
        ._pad         = 0.0f,
    };
    cmdBuf.pushConstants(
        grassPipeline.getVkPipelineLayout(),
        vk::ShaderStageFlagBits::eVertex,
        0,
        sizeof(pc),
        &pc
    );

    etna::flush_barriers(cmdBuf);

    cmdBuf.drawIndirect(grassIndirectBuffer.get(), 0u, 1u, 16u);
}
