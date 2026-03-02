#pragma once

#include <glm/glm.hpp>
#include <vulkan/vulkan.hpp>

class RenderObject {
public:
    RenderObject () = default;

    virtual ~RenderObject () = default;

    virtual void Prepare () = 0;

    virtual void Render (vk::CommandBuffer cmd_buf, vk::PipelineLayout layout) = 0;

    virtual void Update (float /*delta_time*/) {}

    glm::vec3 position{0.0f, 0.0f, 0.0f};
    glm::mat4 modelMatrix{1.0f};
};
