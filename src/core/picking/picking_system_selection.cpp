#include "picking_system.h"
#include "picking_system_internal.h"

#include "core/context.h"

#include <limits>
#include <memory>
#include <utility>

using namespace PickingInternal;

bool PickingSystem::move_last_pick_to_parent()
{
    if (!_last_pick.valid ||
        _last_pick.ownerType != RenderObject::OwnerType::GLTFInstance ||
        _last_pick.node == nullptr)
    {
        return false;
    }

    const std::shared_ptr<Node> parent = _last_pick.node->parent.lock();
    if (!parent)
    {
        return false;
    }
    if (!set_pick_to_gltf_node(_last_pick, parent.get()))
    {
        return false;
    }

    _last_pick.selectionLevel = SelectionLevel::Node;
    return true;
}

bool PickingSystem::move_last_pick_to_child(size_t child_index)
{
    if (!_last_pick.valid ||
        _last_pick.ownerType != RenderObject::OwnerType::GLTFInstance ||
        _last_pick.node == nullptr)
    {
        return false;
    }

    Node *child = get_child_by_compact_index(_last_pick.node, child_index);
    if (!child)
    {
        return false;
    }
    if (!set_pick_to_gltf_node(_last_pick, child))
    {
        return false;
    }

    _last_pick.selectionLevel = SelectionLevel::Node;
    return true;
}

bool PickingSystem::move_last_pick_to_child(const std::string &child_name)
{
    if (!_last_pick.valid ||
        _last_pick.ownerType != RenderObject::OwnerType::GLTFInstance ||
        _last_pick.node == nullptr ||
        _last_pick.scene == nullptr ||
        child_name.empty())
    {
        return false;
    }

    auto child_it = _last_pick.scene->nodes.find(child_name);
    if (child_it == _last_pick.scene->nodes.end() || !child_it->second)
    {
        return false;
    }

    Node *child = child_it->second.get();
    if (!is_direct_child(_last_pick.node, child))
    {
        return false;
    }
    if (!set_pick_to_gltf_node(_last_pick, child))
    {
        return false;
    }

    _last_pick.selectionLevel = SelectionLevel::Node;
    return true;
}

bool PickingSystem::set_last_pick_selection_level(SelectionLevel level)
{
    if (!_last_pick.valid || level == SelectionLevel::None)
    {
        return false;
    }

    switch (level)
    {
        case SelectionLevel::Object:
            if (_last_pick.objectName.empty())
            {
                return false;
            }
            break;
        case SelectionLevel::Member:
            if (_last_pick.memberName.empty())
            {
                return false;
            }
            break;
        case SelectionLevel::Node:
            if (_last_pick.ownerType != RenderObject::OwnerType::GLTFInstance ||
                _last_pick.node == nullptr ||
                _last_pick.nodeName.empty())
            {
                return false;
            }
            break;
        case SelectionLevel::Primitive:
            if (_last_pick.kind != PickInfo::Kind::SceneObject)
            {
                return false;
            }
            break;
        case SelectionLevel::None:
            return false;
    }

    _last_pick.selectionLevel = level;
    return true;
}

bool PickingSystem::select_last_pick_object()
{
    return set_last_pick_selection_level(SelectionLevel::Object);
}

bool PickingSystem::select_last_pick_member()
{
    return set_last_pick_selection_level(SelectionLevel::Member);
}

void PickingSystem::set_pick_from_hit(const RenderObject &hit_object, const WorldVec3 &hit_pos, PickInfo &out_pick)
{
    out_pick.mesh = hit_object.sourceMesh;
    out_pick.scene = hit_object.sourceScene;
    out_pick.node = hit_object.sourceNode;
    out_pick.ownerType = hit_object.ownerType;
    out_pick.ownerName = hit_object.ownerName;
    {
        const OwnerBindingView binding = resolve_owner_binding(hit_object.ownerType, hit_object.ownerName);
        out_pick.objectName.assign(binding.object_name.begin(), binding.object_name.end());
        out_pick.memberName.assign(binding.member_name.begin(), binding.member_name.end());
    }
    if (out_pick.ownerType == RenderObject::OwnerType::GLTFInstance)
    {
        populate_pick_node_hierarchy(out_pick.scene, out_pick.node, out_pick);
    }
    else
    {
        out_pick.nodeName.clear();
        out_pick.nodeParentName.clear();
        out_pick.nodeChildren.clear();
        out_pick.nodePath.clear();
    }
    out_pick.worldPos = hit_pos;
    out_pick.worldTransform = hit_object.transform;
    out_pick.firstIndex = hit_object.firstIndex;
    out_pick.indexCount = hit_object.indexCount;
    out_pick.surfaceIndex = hit_object.surfaceIndex;
    out_pick.time_s = std::numeric_limits<double>::quiet_NaN();
    out_pick.line_payload = {};
    out_pick.line_segment_primary_id = 0;
    out_pick.line_segment_secondary_id = 0;
    out_pick.kind = PickInfo::Kind::SceneObject;
    out_pick.selectionLevel = SelectionLevel::Primitive;
    out_pick.valid = true;
}

bool PickingSystem::set_pick_to_gltf_node(PickInfo &pick, Node *target_node)
{
    if (_context == nullptr ||
        _context->scene == nullptr ||
        !pick.valid ||
        pick.ownerType != RenderObject::OwnerType::GLTFInstance ||
        pick.ownerName.empty() ||
        pick.scene == nullptr ||
        target_node == nullptr)
    {
        return false;
    }

    PickInfo updated = pick;
    updated.mesh = nullptr;
    updated.node = target_node;
    updated.firstIndex = 0;
    updated.indexCount = 0;
    updated.surfaceIndex = 0;

    populate_pick_node_hierarchy(updated.scene, updated.node, updated);
    if (updated.nodeName.empty())
    {
        return false;
    }

    glm::mat4 node_world{1.0f};
    if (_context->scene->getGLTFInstanceNodeWorldTransform(updated.ownerName, updated.nodeName, node_world))
    {
        updated.worldTransform = node_world;
        updated.worldPos = WorldVec3(glm::dvec3(node_world[3]));
    }
    else
    {
        // Fallback to node-local cached world transform if the instance lookup fails.
        updated.worldTransform = target_node->worldTransform;
        updated.worldPos = WorldVec3(glm::dvec3(updated.worldTransform[3]));
    }
    updated.valid = true;
    updated.selectionLevel = SelectionLevel::Node;

    pick = std::move(updated);
    _last_pick_object_id = 0;
    return true;
}
