#pragma once

#include <vector>

#include <glm/glm.hpp>

#include <etna/Image.hpp>

class TerrainGenerator {
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
