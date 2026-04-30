module;

#include <vector>
#include <algorithm>
#include <cmath>
#include <random>

#include <glm/glm.hpp>
#include <etna/Image.hpp>
#include <etna/BlockingTransferHelper.hpp>
#include <etna/GlobalContext.hpp>

export module TerrainGenerator;

export class TerrainGenerator {
public:
    TerrainGenerator ();

    etna::Image GenerateHeightMap (uint32_t width, uint32_t height, int octaves = 6);

private:
    float PerlinNoise (float x, float y) const;

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
    int h = hash & 15;
    float u = h < 8 ? x : y;
    float v = h < 4 ? y : (h == 12 || h == 14 ? x : 0);
    return ((h & 1) == 0 ? u : -u) + ((h & 2) == 0 ? v : -v);
}

float TerrainGenerator::PerlinNoise (float x, float y) const {
    int X = static_cast<int>(std::floor(x)) & 255;
    int Y = static_cast<int>(std::floor(y)) & 255;

    x -= std::floor(x);
    y -= std::floor(y);

    float u = Fade(x);
    float v = Fade(y);

    int A  = permutation[X] + Y;
    int AA = permutation[A];
    int AB = permutation[A + 1];
    int B  = permutation[X + 1] + Y;
    int BA = permutation[B];
    int BB = permutation[B + 1];

    float res = Lerp(v, Lerp(u, Grad(permutation[AA], x, y), Grad(permutation[BA], x - 1, y)),
                     Lerp(u, Grad(permutation[AB], x, y - 1), Grad(permutation[BB], x - 1, y - 1)));

    return res;
}

etna::Image TerrainGenerator::GenerateHeightMap (uint32_t width, uint32_t height, int octaves) {
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