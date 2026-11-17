#include "RockModel.hpp"

#define TINYGLTF_IMPLEMENTATION
#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <tiny_gltf.h>

#include <algorithm>
#include <cstring>
#include <limits>
#include <stdexcept>

#include <etna/Etna.hpp>
#include <etna/GlobalContext.hpp>
#include <etna/OneShotCmdMgr.hpp>
#include <etna/PipelineManager.hpp>


void RockModel::UploadBuffer(
    vk::CommandBuffer cmd, const etna::Buffer& buffer, const void* data, std::size_t size,
    vk::PipelineStageFlags2 stages, vk::AccessFlags2 access
) {
    etna::set_state(cmd, buffer.get(), vk::PipelineStageFlagBits2::eAllTransfer, vk::AccessFlagBits2::eTransferWrite);
    etna::flush_barriers(cmd);
    const auto* bytes = static_cast<const std::byte*>(data);
    for (std::size_t offset = 0; offset < size; offset += 65536) {
        const auto count = std::min<std::size_t>(65536, size - offset);
        cmd.updateBuffer(buffer.get(), offset, count, bytes + offset);
    }
    etna::set_state(cmd, buffer.get(), stages, access);
    etna::flush_barriers(cmd);
}

void RockModel::Load(const std::filesystem::path& path) {
    tinygltf::TinyGLTF loader;
    tinygltf::Model model;
    std::string error, warning;
    if (!loader.LoadASCIIFromFile(&model, &error, &warning, path.string())) {
        throw std::runtime_error("Could not load " + path.string() + ": " + error);
    }
    if (!warning.empty()) spdlog::warn("{}: {}", path.filename().string(), warning);

    if (model.meshes.size() != 1 || model.meshes[0].primitives.size() != 1 || model.nodes.size() != 1 ||
        !model.nodes[0].matrix.empty() || !model.nodes[0].translation.empty() ||
        !model.nodes[0].rotation.empty() || !model.nodes[0].scale.empty()) {
        throw std::runtime_error("Expected a single untransformed Poly Haven rock mesh");
    }
    const auto& primitive = model.meshes[0].primitives[0];
    if (primitive.mode != TINYGLTF_MODE_TRIANGLES || primitive.indices < 0) {
        throw std::runtime_error("Expected an indexed triangle mesh");
    }

    auto attribute = [&](const char* name, int type) -> const tinygltf::Accessor& {
        const auto& accessor = model.accessors.at(primitive.attributes.at(name));
        if (accessor.componentType != TINYGLTF_COMPONENT_TYPE_FLOAT || accessor.type != type || accessor.sparse.isSparse) {
            throw std::runtime_error("Unexpected rock vertex format");
        }
        return accessor;
    };
    const auto& positions = attribute("POSITION", TINYGLTF_TYPE_VEC3);
    const auto& normals = attribute("NORMAL", TINYGLTF_TYPE_VEC3);
    const auto& uvs = attribute("TEXCOORD_0", TINYGLTF_TYPE_VEC2);
    if (positions.count != normals.count || positions.count != uvs.count || positions.count == 0) {
        throw std::runtime_error("Rock vertex attribute counts differ");
    }
    auto read = [&](const tinygltf::Accessor& accessor, std::size_t index, void* destination, std::size_t count) {
        const auto& view = model.bufferViews.at(accessor.bufferView);
        const auto& data = model.buffers.at(view.buffer).data;
        const auto stride = accessor.ByteStride(view);
        const auto offset = view.byteOffset + accessor.byteOffset + index * stride;
        if (stride < static_cast<int>(count) || offset + count > data.size()) throw std::runtime_error("Invalid rock accessor");
        std::memcpy(destination, data.data() + offset, count);
    };

    std::vector<Vertex> vertexData(positions.count);
    glm::vec3 minimum{std::numeric_limits<float>::max()};
    glm::vec3 maximum{std::numeric_limits<float>::lowest()};
    for (std::size_t i = 0; i < vertexData.size(); ++i) {
        read(positions, i, &vertexData[i].position, sizeof(glm::vec3));
        read(normals, i, &vertexData[i].normal, sizeof(glm::vec3));
        read(uvs, i, &vertexData[i].uv, sizeof(glm::vec2));
        minimum = glm::min(minimum, vertexData[i].position);
        maximum = glm::max(maximum, vertexData[i].position);
    }
    const glm::vec3 extent = maximum - minimum;
    const float normalization = 1.0f / std::max({extent.x, extent.y, extent.z, 0.0001f});
    const glm::vec3 origin{(minimum.x + maximum.x) * 0.5f, minimum.y, (minimum.z + maximum.z) * 0.5f};
    bounds.center = {0, extent.y * normalization * 0.5f, 0};
    for (auto& vertex : vertexData) {
        vertex.position = (vertex.position - origin) * normalization;
        bounds.radius = std::max(bounds.radius, glm::distance(vertex.position, bounds.center));
    }

    const auto& indexAccessor = model.accessors.at(primitive.indices);
    if (indexAccessor.type != TINYGLTF_TYPE_SCALAR || indexAccessor.sparse.isSparse || indexAccessor.count % 3 != 0) {
        throw std::runtime_error("Invalid rock indices");
    }
    std::vector<uint32_t> indexData(indexAccessor.count);
    for (std::size_t i = 0; i < indexData.size(); ++i) {
        switch (indexAccessor.componentType) {
        case TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT: read(indexAccessor, i, &indexData[i], 4); break;
        case TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT: {
            uint16_t index;
            read(indexAccessor, i, &index, 2);
            indexData[i] = index;
            break;
        }
        default: throw std::runtime_error("Unsupported rock index format");
        }
        if (indexData[i] >= vertexData.size()) throw std::runtime_error("Rock index out of range");
    }
    indexCount = static_cast<uint32_t>(indexData.size());

    auto& context = etna::get_context();
    vertices = context.createBuffer({
        .size = vertexData.size() * sizeof(Vertex),
        .bufferUsage = vk::BufferUsageFlagBits::eVertexBuffer | vk::BufferUsageFlagBits::eTransferDst,
        .name = "rock vertices",
    });
    indices = context.createBuffer({
        .size = indexData.size() * sizeof(uint32_t),
        .bufferUsage = vk::BufferUsageFlagBits::eIndexBuffer | vk::BufferUsageFlagBits::eTransferDst,
        .name = "rock indices",
    });
    instanceBuffer = context.createBuffer({
        .size = 32768 * sizeof(RockInstance),
        .bufferUsage = vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferDst,
        .name = "visible rock transforms",
    });
    auto oneShot = context.createOneShotCmdMgr();
    auto cmd = oneShot->start();
    ETNA_CHECK_VK_RESULT(cmd.begin(vk::CommandBufferBeginInfo{}));
    UploadBuffer(
        cmd, 
        vertices, vertexData.data(), vertexData.size() * sizeof(Vertex),
                 vk::PipelineStageFlagBits2::eVertexAttributeInput, vk::AccessFlagBits2::eVertexAttributeRead);
    UploadBuffer(cmd, indices, indexData.data(), indexData.size() * sizeof(uint32_t),
                 vk::PipelineStageFlagBits2::eIndexInput, vk::AccessFlagBits2::eIndexRead);
    ETNA_CHECK_VK_RESULT(cmd.end());
    oneShot->submitAndWait(std::move(cmd));

    const auto& sourceMaterial = model.materials.at(primitive.material);
    const auto& pbr = sourceMaterial.pbrMetallicRoughness;
    material = {pbr.roughnessFactor, pbr.metallicFactor, sourceMaterial.normalTexture.scale, 1};
    auto uploadTexture = [&](int textureIndex, vk::Format format, const char* name) {
        const auto& image = model.images.at(model.textures.at(textureIndex).source);
        if (image.bits != 8 || image.component < 3 || image.component > 4) throw std::runtime_error("Expected 8-bit rock texture");
        std::vector<uint8_t> rgba(static_cast<std::size_t>(image.width) * image.height * 4);
        for (std::size_t i = 0; i < rgba.size() / 4; ++i) {
            for (int channel = 0; channel < 3; ++channel) rgba[4 * i + channel] = image.image[i * image.component + channel];
            rgba[4 * i + 3] = 255;
        }
        auto texture = context.createImage({
            .extent = {static_cast<uint32_t>(image.width), static_cast<uint32_t>(image.height), 1},
            .name = name,
            .format = format,
            .imageUsage = vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eSampled,
        });
        auto upload = context.createBuffer({
            .size = rgba.size(),
            .bufferUsage = vk::BufferUsageFlagBits::eTransferSrc | vk::BufferUsageFlagBits::eTransferDst,
            .name = "rock texture upload",
        });
        auto textureCmd = oneShot->start();
        ETNA_CHECK_VK_RESULT(textureCmd.begin(vk::CommandBufferBeginInfo{}));
        UploadBuffer(textureCmd, upload, rgba.data(), rgba.size(), vk::PipelineStageFlagBits2::eCopy, vk::AccessFlagBits2::eTransferRead);
        etna::set_state(textureCmd, texture.get(), vk::PipelineStageFlagBits2::eCopy, vk::AccessFlagBits2::eTransferWrite,
                        vk::ImageLayout::eTransferDstOptimal, vk::ImageAspectFlagBits::eColor);
        etna::flush_barriers(textureCmd);
        textureCmd.copyBufferToImage(upload.get(), texture.get(), vk::ImageLayout::eTransferDstOptimal, {
            vk::BufferImageCopy{
                .imageSubresource = {.aspectMask = vk::ImageAspectFlagBits::eColor, .layerCount = 1},
                .imageExtent = texture.getExtent(),
            },
        });
        etna::set_state(textureCmd, texture.get(), vk::PipelineStageFlagBits2::eFragmentShader, vk::AccessFlagBits2::eShaderSampledRead,
                        vk::ImageLayout::eShaderReadOnlyOptimal, vk::ImageAspectFlagBits::eColor);
        etna::flush_barriers(textureCmd);
        ETNA_CHECK_VK_RESULT(textureCmd.end());
        oneShot->submitAndWait(std::move(textureCmd));
        return texture;
    };
    albedo = uploadTexture(pbr.baseColorTexture.index, vk::Format::eR8G8B8A8Srgb, "rock albedo");
    normal = uploadTexture(sourceMaterial.normalTexture.index, vk::Format::eR8G8B8A8Unorm, "rock normal");
    arm = uploadTexture(pbr.metallicRoughnessTexture.index, vk::Format::eR8G8B8A8Unorm, "rock ARM");
    sampler = etna::Sampler({
        .filter = vk::Filter::eLinear,
        .addressMode = vk::SamplerAddressMode::eRepeat,
        .name = "rock material sampler",
        .maxLod = 0.0f,
    });
    spdlog::info("Loaded {}: {} vertices, {} triangles", path.filename().string(), vertexData.size(), indexCount / 3);
}

void RockModel::CreatePipeline(vk::Format colorFormat) {
    pipeline = etna::get_context().getPipelineManager().createGraphicsPipeline("instanced rocks", {
        .vertexShaderInput = {.bindings = {etna::VertexShaderInputDescription::Binding{
            .byteStreamDescription = {
                .stride = sizeof(Vertex),
                .attributes = {
                    {vk::Format::eR32G32B32Sfloat, offsetof(Vertex, position)},
                    {vk::Format::eR32G32B32Sfloat, offsetof(Vertex, normal)},
                    {vk::Format::eR32G32Sfloat, offsetof(Vertex, uv)},
                },
            },
        }}},
        .fragmentShaderOutput = {
            .colorAttachmentFormats = {colorFormat},
            .depthAttachmentFormat = vk::Format::eD32Sfloat,
        },
    });
}

void RockModel::UploadInstances(vk::CommandBuffer cmd, std::span<const RockInstance> instances) {
    if (instances.size() > 32768) throw std::runtime_error("Visible rock instance buffer capacity exceeded");
    instanceCount = static_cast<uint32_t>(instances.size());
    if (instances.empty()) return;
    UploadBuffer(cmd, instanceBuffer, instances.data(), instances.size_bytes(),
                 vk::PipelineStageFlagBits2::eVertexShader, vk::AccessFlagBits2::eShaderStorageRead);
}

void RockModel::Draw(vk::CommandBuffer cmd, const glm::mat4& viewProjection, glm::vec3 camera) {
    if (instanceCount == 0) return;
    auto programInfo = etna::get_shader_program("instanced rocks");
    const auto descriptorSet = etna::create_descriptor_set(
        programInfo.getDescriptorLayoutId(0), cmd, {
            etna::Binding{0, instanceBuffer.genBinding(0, instanceCount * sizeof(RockInstance))},
            etna::Binding{1, albedo.genBinding(sampler.get(), vk::ImageLayout::eShaderReadOnlyOptimal)},
            etna::Binding{2, normal.genBinding(sampler.get(), vk::ImageLayout::eShaderReadOnlyOptimal)},
            etna::Binding{3, arm.genBinding(sampler.get(), vk::ImageLayout::eShaderReadOnlyOptimal)},
        }
    );
    // UploadInstances and startup texture uploads leave these resources in read-only states.
    cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, pipeline.getVkPipeline());
    cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, pipeline.getVkPipelineLayout(), 0, {descriptorSet.getVkSet()}, {});
    cmd.bindVertexBuffers(0, {vertices.get()}, {0});
    cmd.bindIndexBuffer(indices.get(), 0, vk::IndexType::eUint32);
    struct PushConstants {
        glm::mat4 viewProjection;
        glm::vec4 camera;
        glm::vec4 material;
    } constants{viewProjection, glm::vec4{camera, 1}, material};
    cmd.pushConstants(pipeline.getVkPipelineLayout(), vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment,
                      0, sizeof(constants), &constants);
    cmd.drawIndexed(indexCount, instanceCount, 0, 0, 0);
}
