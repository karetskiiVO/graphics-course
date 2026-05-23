#pragma once

#include <etna/Image.hpp>
#include <etna/Sampler.hpp>
#include <etna/Buffer.hpp>
#include <etna/GraphicsPipeline.hpp>
#include <etna/DescriptorSet.hpp>
#include <etna/BlockingTransferHelper.hpp>
#include <etna/OneShotCmdMgr.hpp>
#include <glm/glm.hpp>
#include <memory>

#include "BindlessSceneManager.hpp"
#include "wsi/Keyboard.hpp"

#include "FramePacket.hpp"
#include "Shader.hpp"

class WorldRenderer {
public:
    WorldRenderer ();

    void LoadScene (std::filesystem::path path);

    void LoadShaders ();
    void AllocateResources (glm::uvec2 swapchainResolution);
    void SetupPipelines (vk::Format swapchainFormat);

    void DebugInput (const Keyboard& kb);
    void Update (const FramePacket& packet);
    void RenderWorld (vk::CommandBuffer cmdBuf, vk::Image targetImage, vk::ImageView targetImageView);

private:
    void BuildIndirectCommands ();
    void CreateBindlessDescriptorSet ();
    void RenderScene (vk::CommandBuffer cmdBuf, vk::PipelineLayout pipelineLayout);

private:
    std::unique_ptr<BindlessSceneManager> sceneMgr;

    etna::Image mainViewDepth;

    std::unique_ptr<etna::OneShotCmdMgr> oneShotCommands;
    etna::BlockingTransferHelper transferHelper;

    etna::PersistentDescriptorSet bindlessSet;

    struct DrawElementInfo {
        glm::mat4x4 model;
        glm::vec4 baseColorFactor;
        std::uint32_t textureIndex;
        std::uint32_t pad0;
        std::uint32_t pad1;
        std::uint32_t pad2;
    };

    etna::Buffer drawInfoBuffer;
    etna::Buffer indirectBuffer;
    std::uint32_t totalDrawCount = 0;

    struct PushConstants {
        glm::mat4x4 projView;
    };

    glm::mat4x4 worldViewProj;

    etna::GraphicsPipeline staticMeshPipeline{};
    Shader staticMeshShader;

    glm::uvec2 resolution;
};
