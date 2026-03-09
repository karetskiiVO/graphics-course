#pragma once

#include <etna/Image.hpp>
#include <etna/Sampler.hpp>
#include <etna/Buffer.hpp>
#include <etna/GraphicsPipeline.hpp>
#include <glm/glm.hpp>
#include <memory>

#include "wsi/Keyboard.hpp"

#include "FramePacket.hpp"
#include "Shader.hpp"
#include "TerrainGenerator.hpp"

class WorldRenderer {
public:
    WorldRenderer ();

    void loadShaders ();

    void allocateResources (glm::uvec2 swapchain_resolution);

    void setupPipelines (vk::Format swapchain_format);

    void debugInput (const Keyboard& kb);

    void update (const FramePacket& packet);

    void drawGui ();

    void renderWorld (vk::CommandBuffer cmd_buf, vk::Image target_image, vk::ImageView target_image_view);

private:
    void renderTerrain (vk::CommandBuffer cmd_buf);

private:
    etna::Image mainViewDepth;
    etna::Image heightMap;
    etna::Sampler heightMapSampler;

    struct PushConstants {
        glm::mat4x4 viewProj;
        glm::vec3 cameraPos;
        float heightScale;
        glm::vec2 chunkOffset;
        float chunkSize;
        float tessellationFactor;
    } pushConstants;

    glm::mat4x4 viewProj;
    glm::vec3 cameraPos;

    etna::GraphicsPipeline terrainPipeline{};
    Shader terrainShader;

    glm::uvec2 resolution;

    float heightScale = 50.0f;
    float tessellationFactor = 64.0f;
    int gridSize = 8;
    float chunkSize = 100.0f;
    bool wireframeMode = false;

    std::unique_ptr<TerrainGenerator> terrainGen;
};
