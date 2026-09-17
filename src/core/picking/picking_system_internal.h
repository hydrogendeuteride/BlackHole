#pragma once

#include "picking_system.h"

#include <algorithm>
#include <cmath>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace PickingInternal
{
    using NodeNameLookup = std::unordered_map<const Node *, const std::string *>;

    inline NodeNameLookup build_node_name_lookup(const LoadedGLTF *scene)
    {
        NodeNameLookup lookup{};
        if (!scene)
        {
            return lookup;
        }

        lookup.reserve(scene->nodes.size());
        for (const auto &entry : scene->nodes)
        {
            if (!entry.second)
            {
                continue;
            }
            lookup[entry.second.get()] = &entry.first;
        }
        return lookup;
    }

    inline const std::string *find_node_name(const NodeNameLookup &lookup, const Node *node)
    {
        if (!node)
        {
            return nullptr;
        }
        auto it = lookup.find(node);
        return (it != lookup.end()) ? it->second : nullptr;
    }

    inline void populate_pick_node_hierarchy(const LoadedGLTF *scene, const Node *node, PickingSystem::PickInfo &out_pick)
    {
        out_pick.nodeName.clear();
        out_pick.nodeParentName.clear();
        out_pick.nodeChildren.clear();
        out_pick.nodePath.clear();

        if (!scene || !node)
        {
            return;
        }

        const NodeNameLookup lookup = build_node_name_lookup(scene);
        const std::string *node_name = find_node_name(lookup, node);
        if (!node_name)
        {
            return;
        }
        out_pick.nodeName = *node_name;

        if (const std::shared_ptr<Node> parent = node->parent.lock())
        {
            if (const std::string *parent_name = find_node_name(lookup, parent.get()))
            {
                out_pick.nodeParentName = *parent_name;
            }
        }

        out_pick.nodeChildren.reserve(node->children.size());
        for (const std::shared_ptr<Node> &child : node->children)
        {
            if (!child)
            {
                continue;
            }
            if (const std::string *child_name = find_node_name(lookup, child.get()))
            {
                out_pick.nodeChildren.push_back(*child_name);
            }
        }

        std::vector<std::string> reverse_path{};
        reverse_path.reserve(8);

        const Node *cursor = node;
        size_t guard = 0;
        constexpr size_t kMaxHierarchyDepth = 1024;
        while (cursor && guard < kMaxHierarchyDepth)
        {
            ++guard;
            const std::string *cursor_name = find_node_name(lookup, cursor);
            if (!cursor_name)
            {
                break;
            }
            reverse_path.push_back(*cursor_name);

            const std::shared_ptr<Node> parent = cursor->parent.lock();
            cursor = parent.get();
        }

        out_pick.nodePath.assign(reverse_path.rbegin(), reverse_path.rend());
    }

    inline Node *get_child_by_compact_index(Node *parent, size_t child_index)
    {
        if (!parent)
        {
            return nullptr;
        }

        size_t compact_index = 0;
        for (const std::shared_ptr<Node> &child : parent->children)
        {
            if (!child)
            {
                continue;
            }
            if (compact_index == child_index)
            {
                return child.get();
            }
            ++compact_index;
        }
        return nullptr;
    }

    inline bool is_direct_child(const Node *parent, const Node *candidate_child)
    {
        if (!parent || !candidate_child)
        {
            return false;
        }
        for (const std::shared_ptr<Node> &child : parent->children)
        {
            if (child && child.get() == candidate_child)
            {
                return true;
            }
        }
        return false;
    }

    inline void apply_hover_selection_level(PickingSystem::PickInfo &pick)
    {
        if (!pick.valid)
        {
            pick.selectionLevel = PickingSystem::SelectionLevel::None;
            return;
        }

        if (pick.kind == PickingSystem::PickInfo::Kind::SceneObject)
        {
            pick.selectionLevel = PickingSystem::SelectionLevel::Member;
        }
        else
        {
            pick.selectionLevel = PickingSystem::SelectionLevel::Object;
        }
    }

    inline bool picks_share_logical_object(const PickingSystem::PickInfo &a, const PickingSystem::PickInfo &b)
    {
        return a.valid &&
               b.valid &&
               a.kind == PickingSystem::PickInfo::Kind::SceneObject &&
               b.kind == PickingSystem::PickInfo::Kind::SceneObject &&
               !a.objectName.empty() &&
               a.objectName == b.objectName;
    }

    inline bool has_distinct_logical_member(const PickingSystem::PickInfo &pick)
    {
        return pick.kind == PickingSystem::PickInfo::Kind::SceneObject &&
               !pick.objectName.empty() &&
               !pick.memberName.empty() &&
               pick.memberName != pick.objectName;
    }

    inline void apply_click_selection_level(PickingSystem::PickInfo &pick,
                                     const PickingSystem::PickInfo *previous_pick = nullptr)
    {
        if (!pick.valid)
        {
            pick.selectionLevel = PickingSystem::SelectionLevel::None;
            return;
        }

        if (pick.kind != PickingSystem::PickInfo::Kind::SceneObject)
        {
            pick.selectionLevel = PickingSystem::SelectionLevel::Object;
            return;
        }

        if (!previous_pick || !picks_share_logical_object(*previous_pick, pick))
        {
            pick.selectionLevel = PickingSystem::SelectionLevel::Object;
            return;
        }

        if (!has_distinct_logical_member(pick))
        {
            pick.selectionLevel = PickingSystem::SelectionLevel::Object;
            return;
        }

        if (previous_pick->selectionLevel == PickingSystem::SelectionLevel::Object)
        {
            pick.selectionLevel = PickingSystem::SelectionLevel::Member;
            return;
        }

        if (previous_pick->selectionLevel == PickingSystem::SelectionLevel::Member)
        {
            pick.selectionLevel = previous_pick->memberName == pick.memberName
                                      ? PickingSystem::SelectionLevel::Object
                                      : PickingSystem::SelectionLevel::Member;
            return;
        }

        pick.selectionLevel = PickingSystem::SelectionLevel::Member;
    }

    inline void apply_drag_selection_level(PickingSystem::PickInfo &pick)
    {
        if (!pick.valid)
        {
            pick.selectionLevel = PickingSystem::SelectionLevel::None;
            return;
        }

        pick.selectionLevel = (pick.kind == PickingSystem::PickInfo::Kind::SceneObject)
                                  ? PickingSystem::SelectionLevel::Member
                                  : PickingSystem::SelectionLevel::Object;
    }

    struct RaySegmentClosest
    {
        double ray_s = 0.0;   // distance along ray direction (>= 0)
        double seg_t = 0.0;   // segment parameter in [0, 1]
        double dist2 = 0.0;   // squared distance between closest points
    };

    inline RaySegmentClosest closest_points_ray_segment(const glm::dvec3 &ray_origin,
                                                 const glm::dvec3 &ray_dir_unit,
                                                 const glm::dvec3 &a,
                                                 const glm::dvec3 &b)
    {
        RaySegmentClosest out{};

        const glm::dvec3 v = b - a;
        const double c = glm::dot(v, v);
        if (!(c > 1.0e-18) || !std::isfinite(c))
        {
            // Segment is a point.
            const glm::dvec3 w = a - ray_origin;
            double s = glm::dot(ray_dir_unit, w);
            if (!std::isfinite(s))
            {
                return out;
            }
            s = std::max(0.0, s);
            const glm::dvec3 pr = ray_origin + ray_dir_unit * s;
            out.ray_s = s;
            out.seg_t = 0.0;
            out.dist2 = glm::dot(pr - a, pr - a);
            return out;
        }

        const glm::dvec3 w0 = ray_origin - a;
        const double bdot = glm::dot(ray_dir_unit, v);
        const double dw = glm::dot(ray_dir_unit, w0);
        const double e = glm::dot(v, w0);

        const double denom = c - (bdot * bdot); // since dot(d,d)=1
        double s = 0.0;
        double t = 0.0;

        if (denom > 1.0e-18 && std::isfinite(denom))
        {
            t = (e - bdot * dw) / denom;
            s = bdot * t - dw;

            if (s < 0.0 || !std::isfinite(s))
            {
                s = 0.0;
                t = std::clamp(e / c, 0.0, 1.0);
            }
            else if (t < 0.0 || !std::isfinite(t))
            {
                t = 0.0;
                s = std::max(0.0, -dw);
            }
            else if (t > 1.0)
            {
                t = 1.0;
                const glm::dvec3 w1 = ray_origin - b;
                const double dw1 = glm::dot(ray_dir_unit, w1);
                s = std::max(0.0, -dw1);
            }
        }
        else
        {
            // Ray and segment are nearly parallel: choose the closer endpoint.
            const double s_a = std::max(0.0, -dw);
            const glm::dvec3 pr_a = ray_origin + ray_dir_unit * s_a;
            const double dist2_a = glm::dot(pr_a - a, pr_a - a);

            const glm::dvec3 w1 = ray_origin - b;
            const double dw1 = glm::dot(ray_dir_unit, w1);
            const double s_b = std::max(0.0, -dw1);
            const glm::dvec3 pr_b = ray_origin + ray_dir_unit * s_b;
            const double dist2_b = glm::dot(pr_b - b, pr_b - b);

            if (dist2_a <= dist2_b)
            {
                s = s_a;
                t = 0.0;
            }
            else
            {
                s = s_b;
                t = 1.0;
            }
        }

        const glm::dvec3 pr = ray_origin + ray_dir_unit * s;
        const glm::dvec3 ps = a + v * t;
        out.ray_s = s;
        out.seg_t = t;
        out.dist2 = glm::dot(pr - ps, pr - ps);
        return out;
    }

    inline bool intersect_ray_sphere_depth(const glm::dvec3 &ray_origin,
                                    const glm::dvec3 &ray_dir_unit,
                                    const glm::dvec3 &center,
                                    double radius_m,
                                    double &out_t)
    {
        out_t = 0.0;

        if (!(radius_m > 0.0) || !std::isfinite(radius_m))
        {
            return false;
        }

        const double dir_len2 = glm::dot(ray_dir_unit, ray_dir_unit);
        if (!(dir_len2 > 0.0) || !std::isfinite(dir_len2))
        {
            return false;
        }

        // Assume ray_dir_unit is unit length (from compute_camera_ray), but be defensive.
        const glm::dvec3 rd = ray_dir_unit / std::sqrt(dir_len2);
        const glm::dvec3 oc = ray_origin - center;

        const double b = glm::dot(oc, rd);
        const double c = glm::dot(oc, oc) - radius_m * radius_m;
        const double disc = b * b - c;
        if (!(disc >= 0.0) || !std::isfinite(disc))
        {
            return false;
        }

        const double s = std::sqrt(disc);
        const double t0 = -b - s;
        const double t1 = -b + s;
        const double t = (t0 >= 0.0) ? t0 : t1;

        if (!(t >= 0.0) || !std::isfinite(t))
        {
            return false;
        }

        out_t = t;
        return true;
    }

    inline bool same_extent(const VkExtent2D &a, const VkExtent2D &b)
    {
        return a.width == b.width && a.height == b.height;
    }

    inline bool same_vec2(const glm::vec2 &a, const glm::vec2 &b, const float epsilon)
    {
        return std::abs(a.x - b.x) <= epsilon &&
               std::abs(a.y - b.y) <= epsilon;
    }

    inline bool same_world_vec3(const WorldVec3 &a, const WorldVec3 &b, const double epsilon)
    {
        return std::abs(a.x - b.x) <= epsilon &&
               std::abs(a.y - b.y) <= epsilon &&
               std::abs(a.z - b.z) <= epsilon;
    }

    inline bool same_quat(const glm::quat &a, const glm::quat &b, const float epsilon)
    {
        return std::abs(a.w - b.w) <= epsilon &&
               std::abs(a.x - b.x) <= epsilon &&
               std::abs(a.y - b.y) <= epsilon &&
               std::abs(a.z - b.z) <= epsilon;
    }

    template <typename HoverCache>
    inline bool hover_miss_cache_matches(const HoverCache &cache,
                                  const glm::vec2 &mouse_pos_window,
                                  const WorldVec3 &camera_world,
                                  const glm::quat &camera_orientation,
                                  const float camera_fov,
                                  const WorldVec3 &origin_world,
                                  const VkExtent2D &logical_extent,
                                  const VkExtent2D &swapchain_extent,
                                  const bool line_hover_enabled)
    {
        constexpr float kMouseEpsilonPx = 0.01f;
        constexpr double kWorldEpsilonM = 1.0e-6;
        constexpr float kQuatEpsilon = 1.0e-6f;
        constexpr float kFovEpsilonDeg = 1.0e-5f;

        return cache.valid &&
               cache.line_hover_enabled == line_hover_enabled &&
               same_vec2(cache.mouse_pos_window, mouse_pos_window, kMouseEpsilonPx) &&
               same_world_vec3(cache.camera_world, camera_world, kWorldEpsilonM) &&
               same_quat(cache.camera_orientation, camera_orientation, kQuatEpsilon) &&
               std::abs(cache.camera_fov - camera_fov) <= kFovEpsilonDeg &&
               same_world_vec3(cache.origin_world, origin_world, kWorldEpsilonM) &&
               same_extent(cache.logical_extent, logical_extent) &&
               same_extent(cache.swapchain_extent, swapchain_extent);
    }
} // namespace PickingInternal
