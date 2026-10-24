#include "modules/render_vulkan/vulkan_headless_readback.hpp"

#include <cstring>
#include <stdexcept>
#include <string>

namespace mosaic::render::vk
{
namespace
{

void vk_check(VkResult r, const char* what)
{
    if (r != VK_SUCCESS)
    {
        throw std::runtime_error(std::string(what) + " failed: VkResult " +
                                 std::to_string(static_cast<int>(r)));
    }
}

} // namespace

VulkanHeadlessReadback::VulkanHeadlessReadback(VulkanHeadlessDevice& dev,
                                               std::uint32_t width,
                                               std::uint32_t height)
    : dev_(dev),
      width_(width),
      height_(height),
      buffer_size_(static_cast<VkDeviceSize>(width) *
                   static_cast<VkDeviceSize>(height) * sizeof(Rgba8))
{
    if (width_ == 0 || height_ == 0)
    {
        throw std::invalid_argument(
            "VulkanHeadlessReadback extent must be non-zero");
    }

    try
    {
        // Command pool MUST be created before any resource that clears or uploads.
        create_command_pool_and_buffer();
        create_fence();
        create_offscreen_image();
        create_readback_buffer();
    }
    catch (...)
    {
        cleanup();
        throw;
    }
}

VulkanHeadlessReadback::~VulkanHeadlessReadback()
{
    cleanup();
}

std::uint32_t VulkanHeadlessReadback::find_memory_type(
    std::uint32_t type_filter,
    VkMemoryPropertyFlags required,
    VkMemoryPropertyFlags* out_flags) const
{
    VkPhysicalDeviceMemoryProperties props{};
    vkGetPhysicalDeviceMemoryProperties(dev_.physical_device(), &props);

    for (std::uint32_t i = 0; i < props.memoryTypeCount; ++i)
    {
        const bool type_ok = (type_filter & (1u << i)) != 0u;
        const VkMemoryPropertyFlags flags = props.memoryTypes[i].propertyFlags;
        if (type_ok && (flags & required) == required)
        {
            if (out_flags != nullptr)
            {
                *out_flags = flags;
            }
            return i;
        }
    }

    throw std::runtime_error("no suitable Vulkan memory type found");
}

void VulkanHeadlessReadback::create_command_pool_and_buffer()
{
    VkCommandPoolCreateInfo pci{};
    pci.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    pci.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    pci.queueFamilyIndex = dev_.graphics_family();

    vk_check(vkCreateCommandPool(dev_.device(), &pci, nullptr, &command_pool_),
             "vkCreateCommandPool");

    VkCommandBufferAllocateInfo cai{};
    cai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    cai.commandPool = command_pool_;
    cai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cai.commandBufferCount = 1;

    vk_check(vkAllocateCommandBuffers(dev_.device(), &cai, &command_buffer_),
             "vkAllocateCommandBuffers");
}

void VulkanHeadlessReadback::create_fence()
{
    VkFenceCreateInfo fci{};
    fci.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fci.flags = 0;

    vk_check(vkCreateFence(dev_.device(), &fci, nullptr, &fence_),
             "vkCreateFence");
}

void VulkanHeadlessReadback::create_offscreen_image()
{
    VkImageCreateInfo ici{};
    ici.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    ici.imageType = VK_IMAGE_TYPE_2D;
    ici.format = VK_FORMAT_R8G8B8A8_UNORM;
    ici.extent.width = width_;
    ici.extent.height = height_;
    ici.extent.depth = 1;
    ici.mipLevels = 1;
    ici.arrayLayers = 1;
    ici.samples = VK_SAMPLE_COUNT_1_BIT;
    ici.tiling = VK_IMAGE_TILING_OPTIMAL;
    ici.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT |
                VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
                VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    ici.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    ici.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    vk_check(vkCreateImage(dev_.device(), &ici, nullptr, &image_),
             "vkCreateImage");

    VkMemoryRequirements req{};
    vkGetImageMemoryRequirements(dev_.device(), image_, &req);

    VkMemoryAllocateInfo mai{};
    mai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    mai.allocationSize = req.size;
    mai.memoryTypeIndex = find_memory_type(
        req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, nullptr);

    vk_check(vkAllocateMemory(dev_.device(), &mai, nullptr, &image_memory_),
             "vkAllocateMemory(image)");
    vk_check(vkBindImageMemory(dev_.device(), image_, image_memory_, 0),
             "vkBindImageMemory");
}

void VulkanHeadlessReadback::create_readback_buffer()
{
    VkBufferCreateInfo bci{};
    bci.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bci.size = buffer_size_;
    bci.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    bci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    vk_check(vkCreateBuffer(dev_.device(), &bci, nullptr, &readback_buffer_),
             "vkCreateBuffer");

    VkMemoryRequirements req{};
    vkGetBufferMemoryRequirements(dev_.device(), readback_buffer_, &req);

    VkMemoryPropertyFlags chosen_flags = 0;
    std::uint32_t mem_type = 0;

    // Prefer HOST_VISIBLE | HOST_COHERENT when available; fall back to
    // HOST_VISIBLE and invalidate before reading.
    VkPhysicalDeviceMemoryProperties props{};
    vkGetPhysicalDeviceMemoryProperties(dev_.physical_device(), &props);
    bool found_coherent = false;
    const VkMemoryPropertyFlags preferred =
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
        VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    for (std::uint32_t i = 0; i < props.memoryTypeCount; ++i)
    {
        if ((req.memoryTypeBits & (1u << i)) != 0u &&
            (props.memoryTypes[i].propertyFlags & preferred) == preferred)
        {
            mem_type = i;
            chosen_flags = props.memoryTypes[i].propertyFlags;
            found_coherent = true;
            break;
        }
    }
    if (!found_coherent)
    {
        mem_type = find_memory_type(req.memoryTypeBits,
                                    VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT,
                                    &chosen_flags);
    }
    host_coherent_ =
        (chosen_flags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) != 0u;

    VkMemoryAllocateInfo mai{};
    mai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    mai.allocationSize = req.size;
    mai.memoryTypeIndex = mem_type;

    vk_check(vkAllocateMemory(dev_.device(), &mai, nullptr, &readback_memory_),
             "vkAllocateMemory(readback_buffer)");
    vk_check(
        vkBindBufferMemory(dev_.device(), readback_buffer_, readback_memory_, 0),
        "vkBindBufferMemory");
}

std::vector<Rgba8> VulkanHeadlessReadback::clear_and_readback(Rgba8 clear_color)
{
    vk_check(vkResetFences(dev_.device(), 1, &fence_), "vkResetFences");
    vk_check(vkResetCommandBuffer(command_buffer_, 0), "vkResetCommandBuffer");

    VkCommandBufferBeginInfo bi{};
    bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vk_check(vkBeginCommandBuffer(command_buffer_, &bi),
             "vkBeginCommandBuffer");

    VkImageSubresourceRange subresource{};
    subresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    subresource.baseMipLevel = 0;
    subresource.levelCount = 1;
    subresource.baseArrayLayer = 0;
    subresource.layerCount = 1;

    // 1. Transition UNDEFINED -> TRANSFER_DST_OPTIMAL before clearing.
    VkImageMemoryBarrier to_dst{};
    to_dst.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    to_dst.srcAccessMask = 0;
    to_dst.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    to_dst.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    to_dst.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    to_dst.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    to_dst.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    to_dst.image = image_;
    to_dst.subresourceRange = subresource;

    vkCmdPipelineBarrier(command_buffer_,
                         VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                         VK_PIPELINE_STAGE_TRANSFER_BIT,
                         0,
                         0, nullptr,
                         0, nullptr,
                         1, &to_dst);

    // 2. Clear to known RGBA colour.
    VkClearColorValue clear_val{};
    clear_val.float32[0] = static_cast<float>(clear_color.r) / 255.0f;
    clear_val.float32[1] = static_cast<float>(clear_color.g) / 255.0f;
    clear_val.float32[2] = static_cast<float>(clear_color.b) / 255.0f;
    clear_val.float32[3] = static_cast<float>(clear_color.a) / 255.0f;

    vkCmdClearColorImage(command_buffer_,
                         image_,
                         VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                         &clear_val,
                         1,
                         &subresource);

    // 3. Transition TRANSFER_DST_OPTIMAL -> TRANSFER_SRC_OPTIMAL before copy.
    VkImageMemoryBarrier to_src{};
    to_src.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    to_src.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    to_src.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    to_src.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    to_src.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    to_src.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    to_src.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    to_src.image = image_;
    to_src.subresourceRange = subresource;

    vkCmdPipelineBarrier(command_buffer_,
                         VK_PIPELINE_STAGE_TRANSFER_BIT,
                         VK_PIPELINE_STAGE_TRANSFER_BIT,
                         0,
                         0, nullptr,
                         0, nullptr,
                         1, &to_src);

    // 4. Copy image to host-visible readback buffer.
    VkBufferImageCopy region{};
    region.bufferOffset = 0;
    region.bufferRowLength = 0;
    region.bufferImageHeight = 0;
    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.mipLevel = 0;
    region.imageSubresource.baseArrayLayer = 0;
    region.imageSubresource.layerCount = 1;
    region.imageOffset = {0, 0, 0};
    region.imageExtent = {width_, height_, 1};

    vkCmdCopyImageToBuffer(command_buffer_,
                           image_,
                           VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                           readback_buffer_,
                           1,
                           &region);

    // 5. Make transfer writes to the readback buffer visible to host reads.
    VkBufferMemoryBarrier to_host{};
    to_host.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
    to_host.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    to_host.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
    to_host.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    to_host.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    to_host.buffer = readback_buffer_;
    to_host.offset = 0;
    to_host.size = VK_WHOLE_SIZE;

    vkCmdPipelineBarrier(command_buffer_,
                         VK_PIPELINE_STAGE_TRANSFER_BIT,
                         VK_PIPELINE_STAGE_HOST_BIT,
                         0,
                         0, nullptr,
                         1, &to_host,
                         0, nullptr);

    vk_check(vkEndCommandBuffer(command_buffer_), "vkEndCommandBuffer");

    // 6. Submit and wait on the fence before mapping.
    VkSubmitInfo submit{};
    submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit.commandBufferCount = 1;
    submit.pCommandBuffers = &command_buffer_;

    vk_check(vkQueueSubmit(dev_.graphics_queue(), 1, &submit, fence_),
             "vkQueueSubmit");
    vk_check(vkWaitForFences(dev_.device(), 1, &fence_, VK_TRUE, UINT64_MAX),
             "vkWaitForFences");

    // 7. Map (and invalidate if non-coherent) and copy out all pixels.
    void* mapped = nullptr;
    vk_check(
        vkMapMemory(dev_.device(), readback_memory_, 0, buffer_size_, 0, &mapped),
        "vkMapMemory");

    if (!host_coherent_)
    {
        VkMappedMemoryRange mapped_range{};
        mapped_range.sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
        mapped_range.memory = readback_memory_;
        mapped_range.offset = 0;
        mapped_range.size = VK_WHOLE_SIZE;
        vk_check(vkInvalidateMappedMemoryRanges(dev_.device(), 1, &mapped_range),
                 "vkInvalidateMappedMemoryRanges");
    }

    std::vector<Rgba8> pixels(static_cast<std::size_t>(width_) *
                              static_cast<std::size_t>(height_));
    std::memcpy(pixels.data(), mapped, static_cast<std::size_t>(buffer_size_));

    vkUnmapMemory(dev_.device(), readback_memory_);
    return pixels;
}

void VulkanHeadlessReadback::cleanup() noexcept
{
    VkDevice device = dev_.device();
    if (device == VK_NULL_HANDLE)
    {
        return;
    }

    vkDeviceWaitIdle(device);

    // Destroy in reverse order of creation.
    if (readback_buffer_ != VK_NULL_HANDLE)
    {
        vkDestroyBuffer(device, readback_buffer_, nullptr);
        readback_buffer_ = VK_NULL_HANDLE;
    }
    if (readback_memory_ != VK_NULL_HANDLE)
    {
        vkFreeMemory(device, readback_memory_, nullptr);
        readback_memory_ = VK_NULL_HANDLE;
    }
    if (image_ != VK_NULL_HANDLE)
    {
        vkDestroyImage(device, image_, nullptr);
        image_ = VK_NULL_HANDLE;
    }
    if (image_memory_ != VK_NULL_HANDLE)
    {
        vkFreeMemory(device, image_memory_, nullptr);
        image_memory_ = VK_NULL_HANDLE;
    }
    if (fence_ != VK_NULL_HANDLE)
    {
        vkDestroyFence(device, fence_, nullptr);
        fence_ = VK_NULL_HANDLE;
    }
    if (command_buffer_ != VK_NULL_HANDLE && command_pool_ != VK_NULL_HANDLE)
    {
        vkFreeCommandBuffers(device, command_pool_, 1, &command_buffer_);
        command_buffer_ = VK_NULL_HANDLE;
    }
    if (command_pool_ != VK_NULL_HANDLE)
    {
        vkDestroyCommandPool(device, command_pool_, nullptr);
        command_pool_ = VK_NULL_HANDLE;
    }
}

} // namespace mosaic::render::vk
