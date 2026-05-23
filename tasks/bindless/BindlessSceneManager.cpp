#include "BindlessSceneManager.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

#include <spdlog/spdlog.h>
#include <fmt/core.h>
#include <stb_image.h>

#include <etna/GlobalContext.hpp>

namespace {
std::uint32_t InferComponentCount(const tinygltf::Image& image) {
    if (image.component > 0) return static_cast<std::uint32_t>(image.component);

    const auto pixelCount = static_cast<std::size_t>(image.width) * static_cast<std::size_t>(image.height);
    if (pixelCount != 0 && !image.image.empty()) return static_cast<std::uint32_t>(image.image.size() / pixelCount);

    return 4;
}

std::vector<std::byte> ConvertToRgba8(
    const unsigned char* pixels,
    std::size_t pixelCount,
    std::uint32_t componentCount
) {
    std::vector<std::byte> rgba(pixelCount * 4);

    for (std::size_t pixelIdx = 0; pixelIdx < pixelCount; ++pixelIdx) {
        const unsigned char* src = pixels + pixelIdx * componentCount;
        auto* dst = rgba.data() + pixelIdx * 4;

        switch (componentCount) {
        case 1:
            dst[0] = std::byte{src[0]};
            dst[1] = std::byte{src[0]};
            dst[2] = std::byte{src[0]};
            dst[3] = std::byte{255};
            break;
        case 2:
            dst[0] = std::byte{src[0]};
            dst[1] = std::byte{src[0]};
            dst[2] = std::byte{src[0]};
            dst[3] = std::byte{src[1]};
            break;
        case 3:
            dst[0] = std::byte{src[0]};
            dst[1] = std::byte{src[1]};
            dst[2] = std::byte{src[2]};
            dst[3] = std::byte{255};
            break;
        case 4:
            dst[0] = std::byte{src[0]};
            dst[1] = std::byte{src[1]};
            dst[2] = std::byte{src[2]};
            dst[3] = std::byte{src[3]};
            break;
        default:
            break;
        }
    }

    return rgba;
}
}

BindlessSceneManager::BindlessSceneManager()
    : oneShotCommands{etna::get_context().createOneShotCmdMgr()}
    , transferHelper{etna::BlockingTransferHelper::CreateInfo{.stagingSize = 4096 * 4096 * 4}}
{
    textureSampler = etna::Sampler(etna::Sampler::CreateInfo{
        .filter = vk::Filter::eLinear,
        .addressMode = vk::SamplerAddressMode::eRepeat,
        .name = "texture_sampler",
    });

}

void BindlessSceneManager::SelectScene(std::filesystem::path path) {
    auto sceneDir = path.parent_path();

    tinygltf::Model model;
    tinygltf::TinyGLTF gltfLoader;
    std::string error;
    std::string warning;
    bool success = false;

    auto ext = path.extension();
    if (ext == ".gltf") {
        success = gltfLoader.LoadASCIIFromFile(&model, &error, &warning, path.string());
    } else if (ext == ".glb") {
        success = gltfLoader.LoadBinaryFromFile(&model, &error, &warning, path.string());
    }

    if (!success) {
        spdlog::error("BindlessSceneManager: failed to load glTF model: {}", error);
        return;
    }
    if (!warning.empty()) spdlog::warn("glTF: {}", warning);

    LoadTextures(model, sceneDir);

    SceneManager::selectScene(path);
}

void BindlessSceneManager::LoadTextures(const tinygltf::Model& model, const std::filesystem::path& sceneDir) {
    relemTextureIndices.clear();
    relemBaseColorFactors.clear();
    sceneTextures.clear();

    auto& ctx = etna::get_context();

    for (std::size_t i = 0; i < model.images.size(); i++) {
        const auto& img = model.images[i];

        int w = img.width;
        int h = img.height;
        const unsigned char* pixels = nullptr;
        bool freePixels = false;
        std::uint32_t componentCount = InferComponentCount(img);
        int bitsPerChannel = img.bits > 0 ? img.bits : 8;

        if (!img.image.empty()) {
            pixels = img.image.data();
        } else if (!img.uri.empty()) {
            auto fullPath = sceneDir / img.uri;
            int chans = 0;
            pixels = stbi_load(fullPath.string().c_str(), &w, &h, &chans, STBI_rgb_alpha);
            componentCount = 4;
            bitsPerChannel = 8;
            freePixels = true;
        }

        if (!pixels || w <= 0 || h <= 0 || bitsPerChannel != 8 || componentCount == 0 || componentCount > 4) {
            spdlog::warn(
                "Failed to load texture {} as 8-bit RGBA-compatible image, using fallback",
                i
            );

            auto fallback = ctx.createImage(etna::Image::CreateInfo{
                .extent = vk::Extent3D{1, 1, 1},
                .name = fmt::format("fallback_tex_{}", i),
                .format = vk::Format::eR8G8B8A8Srgb,
                .imageUsage = vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferDst,
            });

            std::array<std::byte, 4> white = {std::byte{255}, std::byte{255}, std::byte{255}, std::byte{255}};
            transferHelper.uploadImage(
                *oneShotCommands,
                fallback,
                0,
                0,
                std::span<const std::byte>(white.data(), white.size())
            );

            if (freePixels && pixels) stbi_image_free(const_cast<unsigned char*>(pixels));

            sceneTextures.push_back(std::move(fallback));
            continue;
        }

        auto tex = ctx.createImage(etna::Image::CreateInfo{
            .extent = vk::Extent3D{static_cast<uint32_t>(w), static_cast<uint32_t>(h), 1},
            .name = fmt::format("scene_tex_{}", i),
            .format = vk::Format::eR8G8B8A8Srgb,
            .imageUsage = vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferDst,
        });

        const auto pixelCount = static_cast<std::size_t>(w) * static_cast<std::size_t>(h);
        auto rgbaPixels = ConvertToRgba8(pixels, pixelCount, componentCount);

        transferHelper.uploadImage(
            *oneShotCommands,
            tex,
            0,
            0,
            std::span<const std::byte>(rgbaPixels.data(), rgbaPixels.size())
        );

        if (freePixels) stbi_image_free(const_cast<unsigned char*>(pixels));

        sceneTextures.push_back(std::move(tex));
    }

    const auto fallbackTextureIndex = static_cast<std::uint32_t>(sceneTextures.size());
    auto fallback = ctx.createImage(etna::Image::CreateInfo{
        .extent = vk::Extent3D{1, 1, 1},
        .name = "bindless_white_fallback_texture",
        .format = vk::Format::eR8G8B8A8Srgb,
        .imageUsage = vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferDst,
    });

    const std::array<std::byte, 4> white = {
        std::byte{255},
        std::byte{255},
        std::byte{255},
        std::byte{255},
    };
    transferHelper.uploadImage(
        *oneShotCommands,
        fallback,
        0,
        0,
        std::span<const std::byte>(white.data(), white.size())
    );
    sceneTextures.push_back(std::move(fallback));

    for (const auto& mesh : model.meshes) {
        for (const auto& prim : mesh.primitives) {
            if (prim.mode != TINYGLTF_MODE_TRIANGLES) {
                continue;
            }

            std::uint32_t texIdx = fallbackTextureIndex;
            glm::vec4 baseColorFactor{1.0f};

            if (prim.material >= 0 && prim.material < static_cast<int>(model.materials.size())) {
                const auto& mat = model.materials[prim.material];

                const auto& gltfBaseColorFactor = mat.pbrMetallicRoughness.baseColorFactor;
                for (int channel = 0; channel < gltfBaseColorFactor.size() && channel < 4; ++channel) {
                    baseColorFactor[channel] = static_cast<float>(gltfBaseColorFactor[channel]);
                }

                const int texInfoIdx = mat.pbrMetallicRoughness.baseColorTexture.index;
                if (texInfoIdx >= 0 && texInfoIdx < static_cast<int>(model.textures.size())) {
                    const int source = model.textures[texInfoIdx].source;
                    if (source >= 0 && source < static_cast<int>(sceneTextures.size())) {
                        texIdx = static_cast<std::uint32_t>(source);
                    }
                }
            }

            relemTextureIndices.push_back(texIdx);
            relemBaseColorFactors.push_back(baseColorFactor);
        }
    }

    spdlog::info("Loaded {} textures, {} relem texture indices", sceneTextures.size(), relemTextureIndices.size());
}
