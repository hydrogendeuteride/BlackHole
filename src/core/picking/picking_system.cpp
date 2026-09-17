#include "picking_system.h"
#include "picking_system_internal.h"

#include "core/context.h"
#include "core/device/device.h"
#include "core/device/images.h"
#include "render/graph/graph.h"

#include <limits>
#include <utility>

using namespace PickingInternal;

void PickingSystem::init(EngineContext *context)
{
    _context = context;
    _last_pick = {};
    _hover_pick = {};
    _drag_selection.clear();
    _selection_projection.clear();
    _selection_projection_active = false;
    _line_pick_groups.clear();
    _owned_line_pick_segments.clear();
    _line_pick_batches.clear();
    _gltf_owner_bindings.clear();
    _mesh_owner_bindings.clear();
    _mouse_pos_window = glm::vec2{-1.0f, -1.0f};
    _drag_state = {};
    _pending_pick = {};
    _pick_result_pending = false;
    _last_pick_object_id = 0;
    _hover_miss_cache = {};
    _mouse_motion_generation = 0;
    _last_hover_mouse_motion_generation = std::numeric_limits<uint64_t>::max();
    _hover_frame_counter = 0;
    _last_hover_update_frame = 0;

    _pick_readback_buffer = {};
    if (_context && _context->getResources())
    {
        _pick_readback_buffer = _context->getResources()->create_buffer(
            sizeof(uint32_t),
            VK_BUFFER_USAGE_TRANSFER_DST_BIT,
            VMA_MEMORY_USAGE_CPU_TO_GPU);
    }
}

void PickingSystem::cleanup()
{
    if (_pick_readback_buffer.buffer && _context && _context->getResources())
    {
        _context->getResources()->destroy_buffer(_pick_readback_buffer);
    }
    _pick_readback_buffer = {};

    _context = nullptr;
    _last_pick = {};
    _hover_pick = {};
    _manual_hover_pick = false;
    _drag_selection.clear();
    _selection_projection.clear();
    _selection_projection_active = false;
    _line_pick_groups.clear();
    _owned_line_pick_segments.clear();
    _line_pick_batches.clear();
    _gltf_owner_bindings.clear();
    _mesh_owner_bindings.clear();
    _gltf_owner_pick_policies.clear();
    _mesh_owner_pick_policies.clear();
    _line_owner_pick_policies.clear();
    _pending_pick = {};
    _pick_result_pending = false;
    _last_pick_object_id = 0;
    _hover_miss_cache = {};
    _mouse_motion_generation = 0;
    _last_hover_mouse_motion_generation = std::numeric_limits<uint64_t>::max();
    _hover_frame_counter = 0;
    _last_hover_update_frame = 0;
}

void PickingSystem::begin_frame()
{
    if (!_pick_result_pending || !_pick_readback_buffer.buffer || _context == nullptr || _context->scene == nullptr)
    {
        return;
    }

    DeviceManager *dev = _context->getDevice();
    if (!dev)
    {
        return;
    }

    vmaInvalidateAllocation(dev->allocator(), _pick_readback_buffer.allocation, 0, sizeof(uint32_t));

    uint32_t picked_id = 0;
    if (_pick_readback_buffer.info.pMappedData)
    {
        picked_id = *reinterpret_cast<const uint32_t *>(_pick_readback_buffer.info.pMappedData);
    }

    if (picked_id == 0)
    {
        if (_settings.clear_last_pick_on_miss)
        {
            clear_pick(_last_pick);
            _last_pick_object_id = 0;
        }
    }
    else
    {
        RenderObject picked{};
        if (_context->scene->resolveObjectID(picked_id, picked))
        {
            const PickInfo previous_pick = _last_pick;
            glm::vec3 fallback_local = glm::vec3(picked.transform[3]);
            WorldVec3 fallback_pos = local_to_world(fallback_local, _context->scene->get_world_origin());
            PickInfo next_pick{};
            set_pick_from_hit(picked, fallback_pos, next_pick);
            if (pick_allowed(next_pick, PickUse::Click))
            {
                _last_pick = std::move(next_pick);
                _last_pick_object_id = picked_id;
                apply_click_selection_level(_last_pick, &previous_pick);
            }
            else if (_settings.clear_last_pick_on_miss)
            {
                clear_pick(_last_pick);
                _last_pick_object_id = 0;
            }
        }
        else if (_settings.clear_last_pick_on_miss)
        {
            clear_pick(_last_pick);
            _last_pick_object_id = 0;
        }
    }

    _pick_result_pending = false;
}

void PickingSystem::register_id_buffer_readback(RenderGraph &graph,
                                                RGImageHandle id_buffer,
                                                VkExtent2D draw_extent,
                                                VkExtent2D swapchain_extent)
{
    if (!_use_id_buffer_picking || !_pending_pick.active || !id_buffer.valid() || !_pick_readback_buffer.buffer)
    {
        return;
    }

    if (draw_extent.width == 0 || draw_extent.height == 0 || swapchain_extent.width == 0 || swapchain_extent.height == 0)
    {
        _pending_pick.active = false;
        return;
    }

    glm::vec2 logical_pos{};
    if (!vkutil::map_window_to_letterbox_src(_pending_pick.window_pos_swapchain, draw_extent, swapchain_extent, logical_pos))
    {
        _pending_pick.active = false;
        return;
    }

    const uint32_t id_x = static_cast<uint32_t>(std::clamp(logical_pos.x, 0.0f, float(draw_extent.width - 1)));
    const uint32_t id_y = static_cast<uint32_t>(std::clamp(logical_pos.y, 0.0f, float(draw_extent.height - 1)));
    _pending_pick.id_coords = {id_x, id_y};

    RGImportedBufferDesc bd{};
    bd.name = "pick.readback";
    bd.buffer = _pick_readback_buffer.buffer;
    bd.size = sizeof(uint32_t);
    bd.currentStage = VK_PIPELINE_STAGE_2_NONE;
    bd.currentAccess = 0;
    RGBufferHandle pick_buf = graph.import_buffer(bd);

    const glm::uvec2 coords = _pending_pick.id_coords;
    graph.add_pass(
        "PickReadback",
        RGPassType::Transfer,
        [id_buffer, pick_buf](RGPassBuilder &builder, EngineContext *)
        {
            builder.read(id_buffer, RGImageUsage::TransferSrc);
            builder.write_buffer(pick_buf, RGBufferUsage::TransferDst);
        },
        [coords, id_buffer, pick_buf](VkCommandBuffer cmd, const RGPassResources &res, EngineContext *)
        {
            VkImage id_image = res.image(id_buffer);
            VkBuffer dst = res.buffer(pick_buf);
            if (id_image == VK_NULL_HANDLE || dst == VK_NULL_HANDLE) return;

            VkBufferImageCopy region{};
            region.bufferOffset = 0;
            region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            region.imageSubresource.mipLevel = 0;
            region.imageSubresource.baseArrayLayer = 0;
            region.imageSubresource.layerCount = 1;
            region.imageOffset = {static_cast<int32_t>(coords.x),
                                  static_cast<int32_t>(coords.y),
                                  0};
            region.imageExtent = {1, 1, 1};

            vkCmdCopyImageToBuffer(cmd,
                                   id_image,
                                   VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                                   dst,
                                   1,
                                   &region);
        });

    _pick_result_pending = true;
    _pending_pick.active = false;
}

void PickingSystem::clear_pick(PickInfo &pick) const
{
    pick.mesh = nullptr;
    pick.scene = nullptr;
    pick.node = nullptr;
    pick.ownerType = RenderObject::OwnerType::None;
    pick.ownerName.clear();
    pick.objectName.clear();
    pick.memberName.clear();
    pick.nodeName.clear();
    pick.nodeParentName.clear();
    pick.nodeChildren.clear();
    pick.nodePath.clear();
    pick.worldPos = WorldVec3{0.0, 0.0, 0.0};
    pick.worldTransform = glm::mat4(1.0f);
    pick.indexCount = 0;
    pick.firstIndex = 0;
    pick.surfaceIndex = 0;
    pick.time_s = std::numeric_limits<double>::quiet_NaN();
    pick.line_payload = {};
    pick.line_segment_primary_id = 0;
    pick.line_segment_secondary_id = 0;
    pick.kind = PickInfo::Kind::None;
    pick.selectionLevel = SelectionLevel::None;
    pick.valid = false;
}
