#pragma once

#include <filesystem>
#include <span>

#include <etna/Buffer.hpp>
#include <etna/GraphicsPipeline.hpp>
#include <etna/Image.hpp>
#include <etna/Sampler.hpp>

#include "RockScatter.hpp"


class RockModel {
public:
    void Load(const std::filesystem::path& path);
    void CreatePipelines(const std::array<vk::Format, 2>& colorFormats, vk::Format depthFormat);
    void UploadInstances(vk::CommandBuffer cmd, std::span<const RockInstance> instances);
    void Draw(vk::CommandBuffer cmd, const glm::mat4& viewProjection, bool depthOnly);

    RockBounds GetBounds() const { return bounds; }
    uint32_t GetInstanceCount() const { return instanceCount; }

private:
    struct Vertex {
        glm::vec3 position;
        glm::vec3 normal;
        glm::vec2 uv;
    };

    static void UploadBuffer(
        vk::CommandBuffer cmd, const etna::Buffer& buffer, const void* data, std::size_t size,
        vk::PipelineStageFlags2 stages, vk::AccessFlags2 access
    );

    etna::Buffer vertices;
    etna::Buffer indices;
    etna::Buffer instanceBuffer;
    etna::Image albedo;
    etna::Image normal;
    etna::Image arm;
    etna::Sampler sampler;
    etna::GraphicsPipeline depthPipeline;
    etna::GraphicsPipeline pipeline;
    RockBounds bounds{};
    uint32_t indexCount = 0;
    uint32_t instanceCount = 0;
    glm::vec4 material{1, 0, 1, 1};
};
