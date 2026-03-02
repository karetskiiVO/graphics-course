#pragma once

#include <filesystem>
#include <vector>
#include <span>

#include <etna/Image.hpp>
#include <etna/Sampler.hpp>
#include <etna/BlockingTransferHelper.hpp>
#include <etna/OneShotCmdMgr.hpp>
#include <tiny_gltf.h>

#include "scene/SceneManager.hpp"

class BindlessSceneManager : public SceneManager {
public:
    BindlessSceneManager();

    void SelectScene(std::filesystem::path path);

    std::span<const etna::Image> GetSceneTextures() const { return sceneTextures; }
    const etna::Sampler& GetTextureSampler() const { return textureSampler; }
    std::span<const std::uint32_t> GetRelemTextureIndices() const { return relemTextureIndices; }

private:
    void LoadTextures(const tinygltf::Model& model, const std::filesystem::path& sceneDir);

    std::unique_ptr<etna::OneShotCmdMgr> oneShotCommands;
    etna::BlockingTransferHelper transferHelper;

    std::vector<etna::Image> sceneTextures;
    etna::Image fallbackTexture;
    etna::Sampler textureSampler;
    std::vector<std::uint32_t> relemTextureIndices;
};
