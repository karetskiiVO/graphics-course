#include "Decals.hpp"
#include "RockScatter.hpp"
#include "StartupCommands.hpp"

#include <cmath>

#include <glm/ext.hpp>
#include <imgui.h>

#include <etna/Etna.hpp>
#include <etna/GlobalContext.hpp>
#include <etna/PipelineManager.hpp>


namespace {

constexpr uint32_t textureSize = 64;
using Pixel = std::array<uint8_t, 4>;
using Texture = std::array<Pixel, textureSize * textureSize>;

Pixel Pack(glm::vec4 value) {
    const glm::vec4 bytes = glm::round(glm::clamp(value, 0.0f, 1.0f) * 255.0f);
    return {
        static_cast<uint8_t>(bytes.r), static_cast<uint8_t>(bytes.g),
        static_cast<uint8_t>(bytes.b), static_cast<uint8_t>(bytes.a),
    };
}

float SoilHeight(glm::vec2 uv) {
    const glm::vec2 warped = uv + 0.035f * glm::sin(glm::vec2{23.0f * uv.y, 19.0f * uv.x});
    const float relief = std::sin(22.0f * warped.x - 17.0f * warped.y) * std::cos(18.0f * warped.x + 19.0f * warped.y);
    const float grain = std::sin(71.0f * warped.x + 47.0f * warped.y) * std::sin(59.0f * warped.x - 77.0f * warped.y);
    return 0.5f + 0.35f * relief + 0.15f * grain;
}

std::array<Texture, 2> GenerateTextures() {
    std::array<Texture, 2> textures;
    const float step = 1.0f / textureSize;
    const glm::vec2 dx{step, 0.0f};
    const glm::vec2 dy{0.0f, step};
    for (uint32_t y = 0; y < textureSize; ++y) {
        for (uint32_t x = 0; x < textureSize; ++x) {
            const glm::vec2 uv = (glm::vec2{x, y} + 0.5f) * step;
            const float height = SoilHeight(uv);
            const float radius = glm::length(uv * 2.0f - 1.0f) + (height - 0.5f) * 0.28f;
            const float alpha = 1.0f - glm::smoothstep(0.67f, 0.93f, radius);
            const glm::vec3 color = glm::mix(glm::vec3{0.12f, 0.045f, 0.018f}, glm::vec3{0.43f, 0.24f, 0.105f}, height);
            const glm::vec3 normal = glm::normalize(glm::vec3{
                SoilHeight(uv - dx) - SoilHeight(uv + dx),
                SoilHeight(uv - dy) - SoilHeight(uv + dy),
                0.7f,
            });
            const float roughness = glm::mix(0.48f, 0.94f, height);
            const auto pixel = y * textureSize + x;
            textures[0][pixel] = Pack({color, alpha});
            textures[1][pixel] = Pack({glm::vec2{normal} * 0.5f + 0.5f, roughness, 1.0f});
        }
    }
    return textures;
}

}

void Decals::Load(const RockScatter& scatter) {
    etna::create_program("projective decals", {
        DEFERRED_SHADERS_ROOT "fullscreen.vert.spv",
        DEFERRED_SHADERS_ROOT "decals.frag.spv",
    });

    struct Placement {
        glm::vec2 position;
        glm::vec3 halfSize;
        float rotation;
    };
    const std::array placements{
        Placement{{632, 776}, {6, 4, 10}, 0.36f},
        Placement{{612, 767}, {11, 5, 6}, -0.75f},
        Placement{{606, 720}, {9, 5, 13}, 0.6f},
    };
    std::array<glm::mat4, placements.size()> worldToLocal;
    for (std::size_t i = 0; i < placements.size(); ++i) {
        const auto& [position, halfSize, rotation] = placements[i];
        const glm::vec3 center{position.x, scatter.SampleHeight(position), position.y};
        const auto localToWorld = glm::translate(glm::mat4{1}, center)
            * glm::rotate(glm::mat4{1}, rotation, glm::vec3{0, 1, 0})
            * glm::scale(glm::mat4{1}, halfSize);
        worldToLocal[i] = glm::inverse(localToWorld);
    }

    auto& context = etna::get_context();
    auto createTexture = [&](const char* name) {
        return context.createImage({
            .extent = {textureSize, textureSize, 1},
            .name = name,
            .format = vk::Format::eR8G8B8A8Unorm,
            .imageUsage = vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eSampled,
        });
    };
    albedo = createTexture("decal albedo and opacity");
    normalRoughness = createTexture("decal normal and roughness");
    sampler = etna::Sampler({
        .filter = vk::Filter::eLinear,
        .addressMode = vk::SamplerAddressMode::eClampToEdge,
        .name = "decal material sampler",
        .maxLod = 0.0f,
    });
    projectors = context.createBuffer({
        .size = sizeof(worldToLocal),
        .bufferUsage = vk::BufferUsageFlagBits::eUniformBuffer | vk::BufferUsageFlagBits::eTransferDst,
        .name = "decal projectors",
    });
    const auto textures = GenerateTextures();
    static_assert(sizeof(textures) <= 65536);
    auto upload = context.createBuffer({
        .size = sizeof(textures),
        .bufferUsage = vk::BufferUsageFlagBits::eTransferSrc | vk::BufferUsageFlagBits::eTransferDst,
        .name = "decal texture upload",
    });

    StartupCommands startup{context};
    auto cmd = startup.Begin();
    etna::set_state(cmd, projectors.get(), vk::PipelineStageFlagBits2::eAllTransfer, vk::AccessFlagBits2::eTransferWrite);
    etna::set_state(cmd, upload.get(), vk::PipelineStageFlagBits2::eAllTransfer, vk::AccessFlagBits2::eTransferWrite);
    etna::flush_barriers(cmd);
    cmd.updateBuffer(projectors.get(), 0, sizeof(worldToLocal), worldToLocal.data());
    cmd.updateBuffer(upload.get(), 0, sizeof(textures), textures.data());

    etna::set_state(cmd, projectors.get(), vk::PipelineStageFlagBits2::eFragmentShader, vk::AccessFlagBits2::eUniformRead);
    etna::set_state(cmd, upload.get(), vk::PipelineStageFlagBits2::eCopy, vk::AccessFlagBits2::eTransferRead);
    const std::array images{&albedo, &normalRoughness};
    for (std::size_t i = 0; i < images.size(); ++i) {
        const auto& image = *images[i];
        etna::set_state(
            cmd, image.get(), vk::PipelineStageFlagBits2::eCopy, vk::AccessFlagBits2::eTransferWrite,
            vk::ImageLayout::eTransferDstOptimal, vk::ImageAspectFlagBits::eColor
        );
        etna::flush_barriers(cmd);
        cmd.copyBufferToImage(upload.get(), image.get(), vk::ImageLayout::eTransferDstOptimal, {
            vk::BufferImageCopy{
                .bufferOffset = i * sizeof(Texture),
                .imageSubresource = {.aspectMask = vk::ImageAspectFlagBits::eColor, .layerCount = 1},
                .imageExtent = image.getExtent(),
            },
        });
        etna::set_state(
            cmd, image.get(), vk::PipelineStageFlagBits2::eFragmentShader, vk::AccessFlagBits2::eShaderSampledRead,
            vk::ImageLayout::eShaderReadOnlyOptimal, vk::ImageAspectFlagBits::eColor
        );
    }
    etna::flush_barriers(cmd);
    startup.SubmitAndWait();
}

void Decals::CreatePipeline(const std::array<vk::Format, 2>& formats) {
    constexpr vk::PipelineColorBlendAttachmentState attachment{
        .colorWriteMask = vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG
            | vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA,
    };
    pipeline = etna::get_context().getPipelineManager().createGraphicsPipeline("projective decals", {
        .blendingConfig = {.attachments = {attachment, attachment}},
        .depthConfig = {.depthTestEnable = vk::False, .depthWriteEnable = vk::False},
        .fragmentShaderOutput = {.colorAttachmentFormats = {formats[0], formats[1]}},
    });
}

void Decals::DrawGui() {
    ImGui::SliderFloat("Decal opacity", &opacity, 0.0f, 1.0f);
    ImGui::SliderFloat("Decal normal / roughness strength", &strength, 0.0f, 1.0f);
}

void Decals::Draw(
    vk::CommandBuffer cmd, const glm::mat4& inverseViewProjection,
    const etna::Image& albedoRoughness, const etna::Image& normalMaterial,
    const etna::Image& depth, const etna::Sampler& gbufferSampler
) {
    constexpr auto readOnly = vk::ImageLayout::eShaderReadOnlyOptimal;
    const auto programInfo = etna::get_shader_program("projective decals");
    const auto descriptorSet = etna::create_descriptor_set(
        programInfo.getDescriptorLayoutId(0), cmd, {
            etna::Binding{0, albedoRoughness.genBinding(gbufferSampler.get(), readOnly)},
            etna::Binding{1, normalMaterial.genBinding(gbufferSampler.get(), readOnly)},
            etna::Binding{2, depth.genBinding(gbufferSampler.get(), readOnly)},
            etna::Binding{3, albedo.genBinding(sampler.get(), readOnly)},
            etna::Binding{4, normalRoughness.genBinding(sampler.get(), readOnly)},
            etna::Binding{5, projectors.genBinding()},
        }, BarrierBehavior::eSuppressBarriers
    );
    const struct PushConstants {
        glm::mat4 inverseViewProjection;
        glm::vec4 controls;
    } constants{inverseViewProjection, {opacity, strength, 0, 0}};

    cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, pipeline.getVkPipeline());
    cmd.bindDescriptorSets(
        vk::PipelineBindPoint::eGraphics, pipeline.getVkPipelineLayout(),
        0, {descriptorSet.getVkSet()}, {}
    );
    cmd.pushConstants(pipeline.getVkPipelineLayout(), vk::ShaderStageFlagBits::eFragment, 0, sizeof(constants), &constants);
    cmd.draw(3, 1, 0, 0);
}
