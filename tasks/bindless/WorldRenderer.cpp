#include "WorldRenderer.hpp"

#include <etna/GlobalContext.hpp>
#include <etna/PipelineManager.hpp>
#include <etna/RenderTargetStates.hpp>
#include <etna/Profiling.hpp>
#include <etna/Etna.hpp>
#include <glm/ext.hpp>

WorldRenderer::WorldRenderer ()
    : sceneMgr{std::make_unique<BindlessSceneManager>()}
    , oneShotCommands{etna::get_context().createOneShotCmdMgr()}
    , transferHelper{etna::BlockingTransferHelper::CreateInfo{.stagingSize = 64 * 1024 * 1024}} {}

void WorldRenderer::AllocateResources (glm::uvec2 swapchainResolution) {
    resolution = swapchainResolution;

    auto& ctx = etna::get_context();

    mainViewDepth = ctx.createImage(etna::Image::CreateInfo{
        .extent = vk::Extent3D{resolution.x, resolution.y, 1},
        .name = "main_view_depth",
        .format = vk::Format::eD32Sfloat,
        .imageUsage = vk::ImageUsageFlagBits::eDepthStencilAttachment,
    });
}

void WorldRenderer::LoadShaders () {
    staticMeshShader.Create(
        "static_mesh_bindless",
        {
            BINDLESS_SHADERS_ROOT "static_mesh.frag.spv",
            BINDLESS_SHADERS_ROOT "static_mesh.vert.spv"
        }
    );
}

void WorldRenderer::SetupPipelines (vk::Format swapchainFormat) {
    etna::VertexShaderInputDescription sceneVertexInputDesc{
        .bindings = {etna::VertexShaderInputDescription::Binding{
            .byteStreamDescription = sceneMgr->getVertexFormatDescription(),
        }},
    };

    auto& pipelineManager = etna::get_context().getPipelineManager();

    staticMeshPipeline = {};
    staticMeshPipeline = pipelineManager.createGraphicsPipeline(
        staticMeshShader.Name().c_str(),
        etna::GraphicsPipeline::CreateInfo{
            .vertexShaderInput = sceneVertexInputDesc,
            .rasterizationConfig = vk::PipelineRasterizationStateCreateInfo{
                .polygonMode = vk::PolygonMode::eFill,
                .cullMode = vk::CullModeFlagBits::eBack,
                .frontFace = vk::FrontFace::eCounterClockwise,
                .lineWidth = 1.f,
            },
            .fragmentShaderOutput = {
                .colorAttachmentFormats = {swapchainFormat},
                .depthAttachmentFormat = vk::Format::eD32Sfloat,
            },
        });
}

void WorldRenderer::CreateBindlessDescriptorSet () {
    auto sceneTextures = sceneMgr->GetSceneTextures();
    if (sceneTextures.empty()) return;

    auto shaderInfo = etna::get_shader_program(staticMeshShader.Name().c_str());

    std::vector<etna::Binding> bindings;
    for (std::uint32_t i = 0; i < sceneTextures.size(); ++i) {
        bindings.emplace_back(
            0, 
            sceneTextures[i].genBinding(sceneMgr->GetTextureSampler().get(), vk::ImageLayout::eShaderReadOnlyOptimal),
            i
        );
    }

    bindlessSet = etna::create_persistent_descriptor_set(
        shaderInfo.getDescriptorLayoutId(1), 
        std::move(bindings),
        true
    );
}

void WorldRenderer::BuildIndirectCommands () {
    auto meshes           = sceneMgr->getMeshes();
    auto relems           = sceneMgr->getRenderElements();
    auto instanceMeshes   = sceneMgr->getInstanceMeshes();
    auto relemTexIndices  = sceneMgr->GetRelemTextureIndices();
    auto instanceMatrices = sceneMgr->getInstanceMatrices();

    std::vector<vk::DrawIndexedIndirectCommand> commands;
    std::vector<DrawElementInfo> drawInfos;

    for (std::size_t instIdx = 0; instIdx < instanceMeshes.size(); ++instIdx) {
        auto meshIdx = instanceMeshes[instIdx];
        auto& mesh = meshes[meshIdx];

        for (std::uint32_t j = 0; j < mesh.relemCount; ++j) {
            auto relemIdx = mesh.firstRelem + j;
            auto& relem = relems[relemIdx];

            commands.push_back(vk::DrawIndexedIndirectCommand{
                .indexCount = relem.indexCount,
                .instanceCount = 1,
                .firstIndex = relem.indexOffset,
                .vertexOffset = static_cast<int32_t>(relem.vertexOffset),
                .firstInstance = 0,
            });

            std::uint32_t texIdx = 0;
            if (relemIdx < relemTexIndices.size())
                texIdx = relemTexIndices[relemIdx];

            drawInfos.push_back(DrawElementInfo{
                .model = instanceMatrices[instIdx],
                .textureIndex = texIdx,
                .pad0 = 0,
                .pad1 = 0,
                .pad2 = 0,
            });
        }
    }

    totalDrawCount = static_cast<std::uint32_t>(commands.size());
    if (totalDrawCount == 0) return;

    auto& ctx = etna::get_context();

    indirectBuffer = ctx.createBuffer(etna::Buffer::CreateInfo{
        .size = commands.size() * sizeof(vk::DrawIndexedIndirectCommand),
        .bufferUsage = vk::BufferUsageFlagBits::eIndirectBuffer | vk::BufferUsageFlagBits::eTransferDst,
        .memoryUsage = VMA_MEMORY_USAGE_GPU_ONLY,
        .name = "indirect_draw_buffer",
    });
    transferHelper.uploadBuffer(
        *oneShotCommands,
        indirectBuffer,
        0,
        std::span<const vk::DrawIndexedIndirectCommand>(commands)
    );

    drawInfoBuffer = ctx.createBuffer(etna::Buffer::CreateInfo{
        .size = drawInfos.size() * sizeof(DrawElementInfo),
        .bufferUsage = vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferDst,
        .memoryUsage = VMA_MEMORY_USAGE_GPU_ONLY,
        .name = "draw_info_buffer",
    });
    transferHelper.uploadBuffer(
        *oneShotCommands,
        drawInfoBuffer,
        0,
        std::span<const DrawElementInfo>(drawInfos)
    );
}

void WorldRenderer::LoadScene (std::filesystem::path path) {
    sceneMgr->SelectScene(path);
    BuildIndirectCommands();
    CreateBindlessDescriptorSet();
}

void WorldRenderer::DebugInput (const Keyboard&) {}

void WorldRenderer::Update (const FramePacket& packet) {
    ZoneScoped;

    float aspect = float(resolution.x) / float(resolution.y);
    worldViewProj = packet.mainCam.projTm(aspect) * packet.mainCam.viewTm();
}

void WorldRenderer::RenderScene (vk::CommandBuffer cmdBuf, vk::PipelineLayout pipelineLayout) {
    if (!sceneMgr->getVertexBuffer() || totalDrawCount == 0) return;

    cmdBuf.bindVertexBuffers(0, {sceneMgr->getVertexBuffer()}, {0});
    cmdBuf.bindIndexBuffer(sceneMgr->getIndexBuffer(), 0, vk::IndexType::eUint32);

    auto shaderInfo = etna::get_shader_program(staticMeshShader.Name().c_str());

    auto descriptorSet = etna::create_descriptor_set(
        shaderInfo.getDescriptorLayoutId(0), cmdBuf, {etna::Binding{0, drawInfoBuffer.genBinding()}});

    cmdBuf.bindDescriptorSets(
        vk::PipelineBindPoint::eGraphics, pipelineLayout, 0, {descriptorSet.getVkSet()}, {});

    if (bindlessSet.isValid()) {
        bindlessSet.processBarriers(cmdBuf);
        etna::flush_barriers(cmdBuf);
        cmdBuf.bindDescriptorSets(
            vk::PipelineBindPoint::eGraphics,
            pipelineLayout,
            1,
            {bindlessSet.getVkSet()},
            {}
        );
    }

    PushConstants pc{.projView = worldViewProj};
    cmdBuf.pushConstants<PushConstants>(pipelineLayout, vk::ShaderStageFlagBits::eVertex, 0, {pc});

    cmdBuf.drawIndexedIndirect(indirectBuffer.get(), 0, totalDrawCount, sizeof(vk::DrawIndexedIndirectCommand));
}

void WorldRenderer::RenderWorld (vk::CommandBuffer cmdBuf, vk::Image targetImage, vk::ImageView targetImageView) {
    ETNA_PROFILE_GPU(cmdBuf, renderWorld);

    {
        ETNA_PROFILE_GPU(cmdBuf, renderForward);

        etna::RenderTargetState renderTargets(
            cmdBuf,
            {{0, 0}, {resolution.x, resolution.y}},
            {{.image = targetImage, .view = targetImageView}},
            {.image = mainViewDepth.get(), .view = mainViewDepth.getView({})}
        );

        cmdBuf.bindPipeline(vk::PipelineBindPoint::eGraphics, staticMeshPipeline.getVkPipeline());
        RenderScene(cmdBuf, staticMeshPipeline.getVkPipelineLayout());
    }
}
