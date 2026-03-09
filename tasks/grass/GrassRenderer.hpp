#pragma once

#include <array>

#include <etna/Buffer.hpp>
#include <etna/ComputePipeline.hpp>
#include <etna/GraphicsPipeline.hpp>
#include <glm/glm.hpp>

#include "scene/Camera.hpp"
#include "Shader.hpp"

class GrassRenderer {
public:
    GrassRenderer ();

    void AllocateResources ();
    void LoadShaders ();
    void SetupPipelines (vk::Format colorFormat, vk::Format depthFormat);

    void Update (glm::mat4 projView, glm::vec3 camPos, float time);
    void DispatchPlacement (vk::CommandBuffer cmdBuf);
    void DrawBlades (vk::CommandBuffer cmdBuf);

    static constexpr uint32_t gridSize = 512u;
    static constexpr float gridRadius = 300.0f;

private:
    static constexpr uint32_t maxBlades = gridSize * gridSize;
    static constexpr float bladeHeight = 1.5f;
    static constexpr float bladeWidth = 0.18f;
    static constexpr float windStrength = 0.8f;
    static constexpr int vertsPerBlade = 18;
    static constexpr float bladeCullRadius = 2.5f;

    etna::Buffer grassInstanceBuffer;
    etna::Buffer grassIndirectBuffer;

    Shader placementShader;
    etna::ComputePipeline placementPipeline;

    Shader grassShader;
    etna::GraphicsPipeline grassPipeline;

    struct PlacePushConstants {
        glm::vec4 camPosAndRadius;
        uint32_t  gridSize;
        float     bladeRadius;
        uint32_t  pad0;
        uint32_t  pad1;
        glm::vec4 frustumPlanes[6];
    };
    static_assert(sizeof(PlacePushConstants) == 128);

    struct DrawPushConstants {
        glm::mat4 projView;
        float     time;
        float     windStrength;
        float     bladeWidth;
        float     bladeHeight;
        glm::vec3 camPos;
        float     _pad;
    };
    static_assert(sizeof(DrawPushConstants) <= 128);

    glm::mat4 worldViewProj{1.0f};
    glm::vec3 cameraPosition{};
    float currentTime = 0.0f;
    std::array<glm::vec4, 6> frustumPlanes{};

    static std::array<glm::vec4, 6> ExtractFrustumPlanes (const glm::mat4& pv);
};

