#include "BindlessSceneManager.hpp"

#include <spdlog/spdlog.h>
#include <fmt/core.h>
#include <stb_image.h>

#include <etna/GlobalContext.hpp>

BindlessSceneManager::BindlessSceneManager()
    : oneShotCommands{etna::get_context().createOneShotCmdMgr()}
    , transferHelper{etna::BlockingTransferHelper::CreateInfo{.stagingSize = 4096 * 4096 * 4}}
{
    textureSampler = etna::Sampler(etna::Sampler::CreateInfo{
        .filter = vk::Filter::eLinear,
        .addressMode = vk::SamplerAddressMode::eRepeat,
        .name = "texture_sampler",
    });

    auto& ctx = etna::get_context();

    fallbackTexture = ctx.createImage(etna::Image::CreateInfo{
        .extent = vk::Extent3D{1, 1, 1},
        .name = "fallback_texture",
        .format = vk::Format::eR8G8B8A8Srgb,
        .imageUsage = vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferDst,
    });

    std::array<std::byte, 4> white = {std::byte{255}, std::byte{255}, std::byte{255}, std::byte{255}};
    transferHelper.uploadImage(
        *oneShotCommands, 
        fallbackTexture, 
        0, 
        0, 
        std::span<const std::byte>(white.data(), white.size())
    );
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
    sceneTextures.clear();

    auto& ctx = etna::get_context();

    for (std::size_t i = 0; i < model.images.size(); i++) {
        const auto& img = model.images[i];

        int w = img.width;
        int h = img.height;
        const unsigned char* pixels = nullptr;
        bool freePixels = false;

        if (!img.image.empty()) {
            pixels = img.image.data();
        } else if (!img.uri.empty()) {
            auto fullPath = sceneDir / img.uri;
            int chans = 0;
            pixels = stbi_load(fullPath.string().c_str(), &w, &h, &chans, STBI_rgb_alpha);
            freePixels = true;
        }

        if (!pixels || w <= 0 || h <= 0) {
            spdlog::warn("Failed to load texture {}, using fallback", i);

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

        vk::DeviceSize imageSize = static_cast<vk::DeviceSize>(w) * h * 4;
        transferHelper.uploadImage(
            *oneShotCommands, 
            tex, 
            0, 
            0,
            std::span<const std::byte>(reinterpret_cast<const std::byte*>(pixels), imageSize)
        );

        if (freePixels) stbi_image_free(const_cast<unsigned char*>(pixels));

        sceneTextures.push_back(std::move(tex));
    }

    for (const auto& mesh : model.meshes) {
        for (const auto& prim : mesh.primitives) {
            std::uint32_t texIdx = 0;
            if (prim.material >= 0 && prim.material < static_cast<int>(model.materials.size())) {
                const auto& mat = model.materials[prim.material];
                int texInfoIdx = mat.pbrMetallicRoughness.baseColorTexture.index;
                if (texInfoIdx >= 0 && texInfoIdx < static_cast<int>(model.textures.size())) {
                    int source = model.textures[texInfoIdx].source;
                    if (source >= 0 && source < static_cast<int>(sceneTextures.size())) {
                        texIdx = static_cast<std::uint32_t>(source);
                    }
                }
            }
            relemTextureIndices.push_back(texIdx);
        }
    }

    spdlog::info("Loaded {} textures, {} relem texture indices", sceneTextures.size(), relemTextureIndices.size());
}
