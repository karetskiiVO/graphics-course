#pragma once

#include <array>

#include <glm/glm.hpp>

#include <etna/Buffer.hpp>
#include <etna/GraphicsPipeline.hpp>
#include <etna/Image.hpp>
#include <etna/Sampler.hpp>


class RockScatter;

class Decals {
public:
    void Load(const RockScatter& scatter);
    void CreatePipeline(const std::array<vk::Format, 2>& formats);
    void DrawGui();
    void Draw(
        vk::CommandBuffer cmd, const glm::mat4& inverseViewProjection,
        const etna::Image& albedoRoughness, const etna::Image& normalMaterial,
        const etna::Image& depth, const etna::Sampler& gbufferSampler
    );

    bool IsEnabled() const { return opacity > 0.0f; }

private:
    etna::Image albedo;
    etna::Image normalRoughness;
    etna::Sampler sampler;
    etna::Buffer projectors;
    etna::GraphicsPipeline pipeline;
    float opacity = 0.85f;
    float strength = 1.0f;
};
