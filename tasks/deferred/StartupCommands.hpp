#pragma once

#include <utility>

#include <etna/GlobalContext.hpp>


class StartupCommands {
public:
    explicit StartupCommands(const etna::GlobalContext& context)
        : device{context.getDevice()}
        , queue{context.getQueue()}
        , pool{etna::unwrap_vk_result(device.createCommandPoolUnique({
            .flags = vk::CommandPoolCreateFlagBits::eResetCommandBuffer,
            .queueFamilyIndex = context.getQueueFamilyIdx(),
        }))}
        , commandBuffer{std::move(etna::unwrap_vk_result(device.allocateCommandBuffersUnique({
            .commandPool = pool.get(),
            .level = vk::CommandBufferLevel::ePrimary,
            .commandBufferCount = 1,
        })).front())}
    {
    }

    vk::CommandBuffer Begin() {
        ETNA_CHECK_VK_RESULT(commandBuffer->begin({.flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit}));
        return commandBuffer.get();
    }

    void SubmitAndWait() {
        ETNA_CHECK_VK_RESULT(commandBuffer->end());
        const vk::CommandBufferSubmitInfo command{.commandBuffer = commandBuffer.get(), .deviceMask = 1};
        const vk::SubmitInfo2 submission{.commandBufferInfoCount = 1, .pCommandBufferInfos = &command};
        ETNA_CHECK_VK_RESULT(queue.submit2({submission}, {}));
        // Startup runs before frame rendering. Keep staging resources alive until the GPU finishes.
        ETNA_CHECK_VK_RESULT(queue.waitIdle());
        ETNA_CHECK_VK_RESULT(commandBuffer->reset());
    }

private:
    vk::Device device;
    vk::Queue queue;
    vk::UniqueCommandPool pool;
    vk::UniqueCommandBuffer commandBuffer;
};
