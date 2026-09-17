#include "picking_system.h"

#include <algorithm>
#include <unordered_map>

void PickingSystem::clear_owner_picks(RenderObject::OwnerType owner_type, const std::string &owner_name)
{
    if (_last_pick.valid && _last_pick.ownerType == owner_type && _last_pick.ownerName == owner_name)
    {
        clear_pick(_last_pick);
        _last_pick_object_id = 0;
    }
    if (_hover_pick.valid && _hover_pick.ownerType == owner_type && _hover_pick.ownerName == owner_name)
    {
        clear_pick(_hover_pick);
    }

    if (!_drag_selection.empty())
    {
        _drag_selection.erase(std::remove_if(_drag_selection.begin(),
                                             _drag_selection.end(),
                                             [&](const PickInfo &p) {
                                                 return p.valid && p.ownerType == owner_type && p.ownerName == owner_name;
                                             }),
                              _drag_selection.end());
    }

    if (!_selection_projection.empty())
    {
        _selection_projection.erase(
                std::remove_if(_selection_projection.begin(),
                               _selection_projection.end(),
                               [&](const PickInfo &pick) {
                                   return pick.valid && pick.ownerType == owner_type &&
                                          pick.ownerName == owner_name;
                               }),
                _selection_projection.end());
    }
}

void PickingSystem::clear_all_owner_bindings()
{
    _gltf_owner_bindings.clear();
    _mesh_owner_bindings.clear();
}

void PickingSystem::clear_owner_pick_policies()
{
    _gltf_owner_pick_policies.clear();
    _mesh_owner_pick_policies.clear();
    _line_owner_pick_policies.clear();
}

void PickingSystem::set_owner_pick_policy(RenderObject::OwnerType owner_type,
                                          const std::string &owner_name,
                                          OwnerPickPolicy policy)
{
    if (owner_name.empty())
    {
        return;
    }

    std::unordered_map<std::string, OwnerPickPolicy> *policies = nullptr;
    switch (owner_type)
    {
        case RenderObject::OwnerType::GLTFInstance:
            policies = &_gltf_owner_pick_policies;
            break;
        case RenderObject::OwnerType::MeshInstance:
            policies = &_mesh_owner_pick_policies;
            break;
        case RenderObject::OwnerType::None:
            policies = &_line_owner_pick_policies;
            break;
    }

    if (!policies)
    {
        return;
    }

    (*policies)[owner_name] = policy;

    if (!policy.click &&
        _last_pick.valid &&
        _last_pick.ownerType == owner_type &&
        _last_pick.ownerName == owner_name)
    {
        clear_pick(_last_pick);
        _last_pick_object_id = 0;
    }
    if (!policy.hover &&
        _hover_pick.valid &&
        _hover_pick.ownerType == owner_type &&
        _hover_pick.ownerName == owner_name)
    {
        clear_pick(_hover_pick);
    }
}

void PickingSystem::clear_owner_pick_policy(RenderObject::OwnerType owner_type,
                                            const std::string &owner_name)
{
    if (owner_name.empty())
    {
        return;
    }

    switch (owner_type)
    {
        case RenderObject::OwnerType::GLTFInstance:
            _gltf_owner_pick_policies.erase(owner_name);
            break;
        case RenderObject::OwnerType::MeshInstance:
            _mesh_owner_pick_policies.erase(owner_name);
            break;
        case RenderObject::OwnerType::None:
            _line_owner_pick_policies.erase(owner_name);
            break;
    }
}

void PickingSystem::set_owner_binding(RenderObject::OwnerType owner_type,
                                      const std::string &owner_name,
                                      const std::string &object_name,
                                      const std::string &member_name)
{
    if (owner_name.empty())
    {
        return;
    }

    std::unordered_map<std::string, OwnerBinding> *bindings = nullptr;
    switch (owner_type)
    {
        case RenderObject::OwnerType::GLTFInstance:
            bindings = &_gltf_owner_bindings;
            break;
        case RenderObject::OwnerType::MeshInstance:
            bindings = &_mesh_owner_bindings;
            break;
        default:
            return;
    }

    if (!bindings)
    {
        return;
    }

    const std::string resolved_object = object_name.empty() ? owner_name : object_name;
    const std::string resolved_member = member_name.empty() ? owner_name : member_name;
    (*bindings)[owner_name] = OwnerBinding{resolved_object, resolved_member};
}

void PickingSystem::clear_owner_binding(RenderObject::OwnerType owner_type, const std::string &owner_name)
{
    if (owner_name.empty())
    {
        return;
    }

    switch (owner_type)
    {
        case RenderObject::OwnerType::GLTFInstance:
            _gltf_owner_bindings.erase(owner_name);
            break;
        case RenderObject::OwnerType::MeshInstance:
            _mesh_owner_bindings.erase(owner_name);
            break;
        default:
            break;
    }
}

bool PickingSystem::get_owner_binding(RenderObject::OwnerType owner_type,
                                      const std::string &owner_name,
                                      std::string &out_object_name,
                                      std::string &out_member_name) const
{
    const OwnerBindingView binding = resolve_owner_binding(owner_type, owner_name);
    if (binding.object_name.empty())
    {
        out_object_name.clear();
        out_member_name.clear();
        return false;
    }

    out_object_name.assign(binding.object_name.begin(), binding.object_name.end());
    out_member_name.assign(binding.member_name.begin(), binding.member_name.end());
    return true;
}

PickingSystem::OwnerBindingView PickingSystem::resolve_owner_binding(RenderObject::OwnerType owner_type,
                                                                     const std::string &owner_name) const
{
    if (owner_name.empty())
    {
        return {};
    }

    const std::unordered_map<std::string, OwnerBinding> *bindings = nullptr;
    switch (owner_type)
    {
        case RenderObject::OwnerType::GLTFInstance:
            bindings = &_gltf_owner_bindings;
            break;
        case RenderObject::OwnerType::MeshInstance:
            bindings = &_mesh_owner_bindings;
            break;
        default:
            break;
    }

    if (bindings)
    {
        auto it = bindings->find(owner_name);
        if (it != bindings->end())
        {
            return {
                it->second.object_name.empty() ? std::string_view(owner_name) : std::string_view(it->second.object_name),
                it->second.member_name.empty() ? std::string_view(owner_name) : std::string_view(it->second.member_name),
            };
        }
    }

    return {owner_name, owner_name};
}

bool PickingSystem::pick_allowed(const PickInfo &pick, PickUse use) const
{
    if (!pick.valid || pick.ownerName.empty())
    {
        return pick.valid;
    }

    const std::unordered_map<std::string, OwnerPickPolicy> *policies = nullptr;
    switch (pick.ownerType)
    {
        case RenderObject::OwnerType::GLTFInstance:
            policies = &_gltf_owner_pick_policies;
            break;
        case RenderObject::OwnerType::MeshInstance:
            policies = &_mesh_owner_pick_policies;
            break;
        case RenderObject::OwnerType::None:
            if (pick.kind == PickInfo::Kind::Line)
            {
                policies = &_line_owner_pick_policies;
            }
            break;
    }

    if (!policies)
    {
        return true;
    }

    const auto it = policies->find(pick.ownerName);
    if (it == policies->end())
    {
        return true;
    }

    switch (use)
    {
        case PickUse::Hover:
            return it->second.hover;
        case PickUse::Click:
            return it->second.click;
    }
    return true;
}
