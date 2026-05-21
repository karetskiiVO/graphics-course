module;

#include <vector>
#include <algorithm>
#include <cmath>
#include <random>
#include <cstring>

#include <glm/glm.hpp>
#include <etna/Image.hpp>
#include <etna/BlockingTransferHelper.hpp>
#include <etna/GlobalContext.hpp>
#include <stb_image.h>

export module TerrainGenerator;

export class TerrainGenerator {
public:
    TerrainGenerator ();

    etna::Image GenerateHeightMap (uint32_t width, uint32_t height, int octaves = 6);
    etna::Image GenerateSplatMap (uint32_t width, uint32_t height, int octaves = 6);
    etna::Image GenerateDetailTextures (uint32_t size);
    etna::Image GenerateDetailNormalMaps (uint32_t size);

private:
    float PerlinNoise (float x, float y) const;
    float FbmNoise (float x, float y, int octaves, float lacunarity = 2.0f, float gain = 0.5f) const;

    float Fade (float t) const;
    float Lerp (float t, float a, float b) const;
    float Grad (int hash, float x, float y) const;

    std::vector<int> permutation;
};

TerrainGenerator::TerrainGenerator () {
    permutation.resize(512);

    std::vector<int> p = {
        151, 160, 137, 91, 90, 15, 131, 13, 201, 95, 96, 53, 194, 233, 7, 225, 140, 36, 103, 30, 69, 142, 8, 99, 37,
        240, 21, 10, 23, 190, 6, 148, 247, 120, 234, 75, 0, 26, 197, 62, 94, 252, 219, 203, 117, 35, 11, 32, 57, 177,
        33, 88, 237, 149, 56, 87, 174, 20, 125, 136, 171, 168, 68, 175, 74, 165, 71, 134, 139, 48, 27, 166, 77, 146,
        158, 231, 83, 111, 229, 122, 60, 211, 133, 230, 220, 105, 92, 41, 55, 46, 245, 40, 244, 102, 143, 54, 65, 25,
        63, 161, 1, 216, 80, 73, 209, 76, 132, 187, 208, 89, 18, 169, 200, 196, 135, 130, 116, 188, 159, 86, 164, 100,
        109, 198, 173, 186, 3, 64, 52, 217, 226, 250, 124, 123, 5, 202, 38, 147, 118, 126, 255, 82, 85, 212, 207, 206,
        59, 227, 47, 16, 58, 17, 182, 189, 28, 42, 223, 183, 170, 213, 119, 248, 152, 2, 44, 154, 163, 70, 221, 153,
        101, 155, 167, 43, 172, 9, 129, 22, 39, 253, 19, 98, 108, 110, 79, 113, 224, 232, 178, 185, 112, 104, 218, 246,
        97, 228, 251, 34, 242, 193, 238, 210, 144, 12, 191, 179, 162, 241, 81, 51, 145, 235, 249, 14, 239, 107, 49, 192,
        214, 31, 181, 199, 106, 157, 184, 84, 204, 176, 115, 121, 50, 45, 127, 4, 150, 254, 138, 236, 205, 93, 222, 114,
        67, 29, 24, 72, 243, 141, 128, 195, 78, 66, 215, 61, 156, 180
    };

    for (int i = 0; i < 256; i++) permutation[i] = permutation[i + 256] = p[i];
}

float TerrainGenerator::Fade (float t) const { return t * t * t * (t * (t * 6 - 15) + 10); }

float TerrainGenerator::Lerp (float t, float a, float b) const { return a + t * (b - a); }

float TerrainGenerator::Grad (int hash, float x, float y) const {
    auto h = hash & 15;
    auto u = h < 8 ? x : y;
    auto v = h < 4 ? y : (h == 12 || h == 14 ? x : 0);
    return ((h & 1) == 0 ? u : -u) + ((h & 2) == 0 ? v : -v);
}

float TerrainGenerator::PerlinNoise (float x, float y) const {
    auto X = static_cast<int>(std::floor(x)) & 255;
    auto Y = static_cast<int>(std::floor(y)) & 255;

    x -= std::floor(x);
    y -= std::floor(y);

    auto u = Fade(x);
    auto v = Fade(y);

    auto A  = permutation[X] + Y;
    auto AA = permutation[A];
    auto AB = permutation[A + 1];
    auto B  = permutation[X + 1] + Y;
    auto BA = permutation[B];
    auto BB = permutation[B + 1];

    float res = Lerp(
        v,
        Lerp(u, Grad(permutation[AA], x, y), Grad(permutation[BA], x - 1, y)),
        Lerp(u, Grad(permutation[AB], x, y - 1), Grad(permutation[BB], x - 1, y - 1))
    );

    return res;
}

float TerrainGenerator::FbmNoise (float x, float y, int octaves, float lacunarity, float gain) const {
    auto amplitude = 1.0f;
    auto frequency = 1.0f;
    auto value = 0.0f;

    for (int i = 0; i < octaves; ++i) {
        value += PerlinNoise(x * frequency, y * frequency) * amplitude;
        amplitude *= gain;
        frequency *= lacunarity;
    }

    return value;
}

etna::Image TerrainGenerator::GenerateHeightMap (uint32_t width, uint32_t height, int octaves) {
    std::vector<float> heights(width * height);

    auto maxHeight = 0.0f;
    auto minHeight = 0.0f;

    for (uint32_t y = 0; y < height; ++y) {
        for (uint32_t x = 0; x < width; ++x) {
            auto amplitude = 1.0f;
            auto frequency = 1.0f;
            auto noiseValue = 0.0f;

            for (int octave = 0; octave < octaves; ++octave) {
                auto sampleX = x / static_cast<float>(width) * frequency * 8.0f;
                auto sampleY = y / static_cast<float>(height) * frequency * 8.0f;

                auto perlin = PerlinNoise(sampleX, sampleY);
                noiseValue += perlin * amplitude;

                amplitude *= 0.5f;
                frequency *= 2.0f;
            }

            heights[y * width + x] = noiseValue;
            maxHeight = std::max(maxHeight, noiseValue);
            minHeight = std::min(minHeight, noiseValue);
        }
    }

    for (auto& h: heights) { h = (h - minHeight) / (maxHeight - minHeight); }

    auto& ctx = etna::get_context();

    auto heightMap = ctx.createImage(etna::Image::CreateInfo{
        .extent = vk::Extent3D{width, height, 1},
        .name = "height_map",
        .format = vk::Format::eR32Sfloat,
        .imageUsage = vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferDst,
    });

    std::unique_ptr<etna::OneShotCmdMgr> oneShotCmdMgr = ctx.createOneShotCmdMgr();
    auto transferHelper = etna::BlockingTransferHelper{
        etna::BlockingTransferHelper::CreateInfo{
            .stagingSize = static_cast<std::uint64_t>(width * height * sizeof(float)),
        }
    };

    transferHelper.uploadImage(
        *oneShotCmdMgr,
        heightMap,
        0,
        0,
        std::span<const std::byte>(reinterpret_cast<const std::byte*>(heights.data()),
        width * height * sizeof(float))
    );

    return heightMap;
}

etna::Image TerrainGenerator::GenerateSplatMap (uint32_t width, uint32_t height, int octaves) {
    std::vector<float> heights(width * height);
    float maxHeight = 0.0f;
    float minHeight = 0.0f;

    for (uint32_t y = 0; y < height; ++y) {
        for (uint32_t x = 0; x < width; ++x) {
            float amplitude = 1.0f;
            float frequency = 1.0f;
            float noiseValue = 0.0f;

            for (int octave = 0; octave < octaves; ++octave) {
                float sampleX = x / static_cast<float>(width) * frequency * 8.0f;
                float sampleY = y / static_cast<float>(height) * frequency * 8.0f;

                float perlin = PerlinNoise(sampleX, sampleY);
                noiseValue += perlin * amplitude;

                amplitude *= 0.5f;
                frequency *= 2.0f;
            }

            heights[y * width + x] = noiseValue;
            maxHeight = std::max(maxHeight, noiseValue);
            minHeight = std::min(minHeight, noiseValue);
        }
    }

    for (auto& h: heights) { h = (h - minHeight) / (maxHeight - minHeight); }

    struct RGBA8 { uint8_t r, g, b, a; };
    std::vector<RGBA8> splatData(width * height);

    for (uint32_t i = 0; i < width * height; ++i) {
        float h = heights[i];

        float px = static_cast<float>(i % width) / static_cast<float>(width);
        float py = static_cast<float>(i / width) / static_cast<float>(height);
        float noise = FbmNoise(px * 16.0f, py * 16.0f, 4) * 0.05f;

        float hNoisy = h + noise;

        float gravel = 0.0f, grass = 0.0f, rock = 0.0f, snow = 0.0f;
        gravel = 1.0f - std::clamp((hNoisy - 0.20f) / 0.15f, 0.0f, 1.0f);

        float grassUp = std::clamp((hNoisy - 0.15f) / 0.15f, 0.0f, 1.0f);
        float grassDown = 1.0f - std::clamp((hNoisy - 0.50f) / 0.15f, 0.0f, 1.0f);
        grass = grassUp * grassDown;

        float rockUp = std::clamp((hNoisy - 0.45f) / 0.15f, 0.0f, 1.0f);
        float rockDown = 1.0f - std::clamp((hNoisy - 0.75f) / 0.15f, 0.0f, 1.0f);
        rock = rockUp * rockDown;

        snow = std::clamp((hNoisy - 0.70f) / 0.15f, 0.0f, 1.0f);

        uint32_t xi = i % width;
        uint32_t yi = i / width;
        float slope = 0.0f;
        if (xi > 0 && xi < width - 1 && yi > 0 && yi < height - 1) {
            float dhdx = heights[yi * width + xi + 1] - heights[yi * width + xi - 1];
            float dhdy = heights[(yi + 1) * width + xi] - heights[(yi - 1) * width + xi];
            slope = std::sqrt(dhdx * dhdx + dhdy * dhdy) * 50.0f; // Scale factor
            slope = std::clamp(slope, 0.0f, 1.0f);
        }

        rock = std::max(rock, slope * 0.8f);

        float total = gravel + grass + rock + snow;
        if (total > 0.001f) {
            gravel /= total;
            grass /= total;
            rock /= total;
            snow /= total;
        }

        splatData[i] = {
            static_cast<uint8_t>(std::clamp(gravel * 255.0f, 0.0f, 255.0f)),
            static_cast<uint8_t>(std::clamp(grass * 255.0f, 0.0f, 255.0f)),
            static_cast<uint8_t>(std::clamp(rock * 255.0f, 0.0f, 255.0f)),
            static_cast<uint8_t>(std::clamp(snow * 255.0f, 0.0f, 255.0f)),
        };
    }

    auto& ctx = etna::get_context();

    auto splatMap = ctx.createImage(etna::Image::CreateInfo{
        .extent = vk::Extent3D{width, height, 1},
        .name = "splat_map",
        .format = vk::Format::eR8G8B8A8Unorm,
        .imageUsage = vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferDst,
    });

    std::unique_ptr<etna::OneShotCmdMgr> oneShotCmdMgr = ctx.createOneShotCmdMgr();
    auto transferHelper = etna::BlockingTransferHelper{
        etna::BlockingTransferHelper::CreateInfo{
            .stagingSize = static_cast<std::uint64_t>(width * height * sizeof(RGBA8)),
        }
    };

    transferHelper.uploadImage(
        *oneShotCmdMgr,
        splatMap,
        0,
        0,
        std::span<const std::byte>(reinterpret_cast<const std::byte*>(splatData.data()),
        width * height * sizeof(RGBA8))
    );

    return splatMap;
}

etna::Image TerrainGenerator::GenerateDetailTextures (uint32_t size) {
    struct RGBA8 { uint8_t r, g, b, a; };

    struct LayerFiles {
        const char* albedo;
        const char* height;
    };

    const LayerFiles layerFiles[4] = {
        {GRAPHICS_COURSE_ROOT "/tasks/supertask/textures/gravel/pebble-3.jpg",
         GRAPHICS_COURSE_ROOT "/tasks/supertask/textures/gravel/pebble-3-2.jpg"},
        {GRAPHICS_COURSE_ROOT "/tasks/supertask/textures/grass/grass-1.jpg",
         GRAPHICS_COURSE_ROOT "/tasks/supertask/textures/grass/grass-1-2.jpg"},
        {GRAPHICS_COURSE_ROOT "/tasks/supertask/textures/rock/slate-1.jpg",
         GRAPHICS_COURSE_ROOT "/tasks/supertask/textures/rock/slate-1-2.jpg"},
        {GRAPHICS_COURSE_ROOT "/tasks/supertask/textures/snow/snow-1-1.jpg",
         GRAPHICS_COURSE_ROOT "/tasks/supertask/textures/snow/snow-1-2.jpg"},
    };

    int w = 0, channels;
    std::vector<stbi_uc*> albedos(4, nullptr);
    std::vector<stbi_uc*> heights(4, nullptr);

    for (uint32_t layer = 0; layer < 4; ++layer) {
        int lw, lh;
        albedos[layer] = stbi_load(layerFiles[layer].albedo, &lw, &lh, &channels, 4);
        heights[layer] = stbi_load(layerFiles[layer].height, &lw, &lh, &channels, 4);

        if (!albedos[layer] || !heights[layer]) {
            for (uint32_t i = 0; i <= layer; ++i) {
                if (albedos[i]) stbi_image_free(albedos[i]);
                if (heights[i]) stbi_image_free(heights[i]);
            }
            throw std::runtime_error(
                std::string("Failed to load detail textures for layer ") + std::to_string(layer)
            );
        }

        if (layer == 0) w = lw;
    }

    size = static_cast<uint32_t>(w);
    std::vector<RGBA8> allLayers(size * size * 4);

    for (uint32_t layer = 0; layer < 4; ++layer) {
        for (uint32_t i = 0; i < size * size; ++i) {
            uint32_t idx = layer * size * size + i;
            allLayers[idx] = {
                albedos[layer][i * 4 + 0],
                albedos[layer][i * 4 + 1],
                albedos[layer][i * 4 + 2],
                heights[layer][i * 4 + 0],
            };
        }
    }

    for (auto* p : albedos) stbi_image_free(p);
    for (auto* p : heights) stbi_image_free(p);

    auto& ctx = etna::get_context();

    auto detailTex = ctx.createImage(etna::Image::CreateInfo{
        .extent = vk::Extent3D{size, size, 1},
        .name = "detail_textures",
        .format = vk::Format::eR8G8B8A8Unorm,
        .imageUsage = vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eTransferSrc,
        .layers = 4,
        .mipLevels = 1,
    });

    std::unique_ptr<etna::OneShotCmdMgr> oneShotCmdMgr = ctx.createOneShotCmdMgr();
    auto transferHelper = etna::BlockingTransferHelper{
        etna::BlockingTransferHelper::CreateInfo{
            .stagingSize = static_cast<std::uint64_t>(size * size * sizeof(RGBA8) * 4),
        }
    };

    for (uint32_t layer = 0; layer < 4; ++layer) {
        const RGBA8* layerData = allLayers.data() + layer * size * size;
        transferHelper.uploadImage(
            *oneShotCmdMgr,
            detailTex,
            0,
            layer,
            std::span<const std::byte>(
                reinterpret_cast<const std::byte*>(layerData),
                size * size * sizeof(RGBA8)
            )
        );
    }

    return detailTex;
}

etna::Image TerrainGenerator::GenerateDetailNormalMaps (uint32_t size) {
    struct RGBA8 { uint8_t r, g, b, a; };

    const char* normalFiles[4] = {
        GRAPHICS_COURSE_ROOT "/tasks/supertask/textures/gravel/pebble-3-4.jpg",
        GRAPHICS_COURSE_ROOT "/tasks/supertask/textures/grass/grass-1-4.jpg",
        GRAPHICS_COURSE_ROOT "/tasks/supertask/textures/rock/slate-1-4.jpg",
        GRAPHICS_COURSE_ROOT "/tasks/supertask/textures/snow/snow-1-3.jpg",
    };

    int channels;
    int w = 0;
    std::vector<stbi_uc*> normals(4, nullptr);

    for (uint32_t layer = 0; layer < 4; ++layer) {
        int lw, lh;
        normals[layer] = stbi_load(normalFiles[layer], &lw, &lh, &channels, 4);
        if (!normals[layer]) {
            for (uint32_t i = 0; i <= layer; ++i) if (normals[i]) stbi_image_free(normals[i]);
            throw std::runtime_error(
                std::string("Failed to load detail normal map for layer ") + std::to_string(layer)
            );
        }
        if (layer == 0) w = lw;
    }

    size = static_cast<uint32_t>(w);
    std::vector<RGBA8> allLayers(size * size * 4);

    for (uint32_t layer = 0; layer < 4; ++layer) {
        for (uint32_t i = 0; i < size * size; ++i) {
            uint32_t idx = layer * size * size + i;
            allLayers[idx] = {
                normals[layer][i * 4 + 0],
                normals[layer][i * 4 + 1],
                normals[layer][i * 4 + 2],
                255,
            };
        }
    }

    for (auto* p : normals) stbi_image_free(p);

    auto& ctx = etna::get_context();

    auto normalTex = ctx.createImage(etna::Image::CreateInfo{
        .extent = vk::Extent3D{size, size, 1},
        .name = "detail_normals",
        .format = vk::Format::eR8G8B8A8Unorm,
        .imageUsage = vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferDst,
        .layers = 4,
    });

    std::unique_ptr<etna::OneShotCmdMgr> oneShotCmdMgr = ctx.createOneShotCmdMgr();
    auto transferHelper = etna::BlockingTransferHelper{
        etna::BlockingTransferHelper::CreateInfo{
            .stagingSize = static_cast<std::uint64_t>(size * size * sizeof(RGBA8) * 4),
        }
    };

    for (uint32_t layer = 0; layer < 4; ++layer) {
        const RGBA8* layerData = allLayers.data() + layer * size * size;
        transferHelper.uploadImage(
            *oneShotCmdMgr,
            normalTex,
            0,
            layer,
            std::span<const std::byte>(
                reinterpret_cast<const std::byte*>(layerData),
                size * size * sizeof(RGBA8)
            )
        );
    }

    return normalTex;
}
