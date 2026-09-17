#include "orbit_plot.h"

#include <algorithm>
#include <cmath>

namespace
{
    constexpr double kClipPlaneEpsilon = 1.0e-9;
    constexpr double kClipDirectionEpsilon = 1.0e-18;

    double clip_plane_distance(const glm::dvec4 &clip, const int plane_index)
    {
        switch (plane_index)
        {
            case 0: return clip.x + clip.w;
            case 1: return -clip.x + clip.w;
            case 2: return clip.y + clip.w;
            case 3: return -clip.y + clip.w;
            case 4: return clip.z;
            case 5: return clip.w - clip.z;
            default: return 0.0;
        }
    }

    bool finite_clip(const glm::dvec4 &clip)
    {
        return std::isfinite(clip.x) && std::isfinite(clip.y) &&
               std::isfinite(clip.z) && std::isfinite(clip.w);
    }

    bool clip_segment(glm::dvec4 &clip_a, glm::dvec4 &clip_b)
    {
        if (!finite_clip(clip_a) || !finite_clip(clip_b))
        {
            return false;
        }

        double t0 = 0.0;
        double t1 = 1.0;
        for (int plane_index = 0; plane_index < 6; ++plane_index)
        {
            const double d0 = clip_plane_distance(clip_a, plane_index);
            const double d1 = clip_plane_distance(clip_b, plane_index);
            const bool a_inside = d0 >= kClipPlaneEpsilon;
            const bool b_inside = d1 >= kClipPlaneEpsilon;
            if (a_inside && b_inside)
            {
                continue;
            }
            if (!a_inside && !b_inside)
            {
                return false;
            }

            const double denominator = d1 - d0;
            if (!(std::abs(denominator) > kClipDirectionEpsilon) ||
                !std::isfinite(denominator))
            {
                return false;
            }
            const double t = std::clamp((kClipPlaneEpsilon - d0) / denominator,
                                        0.0,
                                        1.0);
            if (!a_inside)
            {
                t0 = std::max(t0, t);
            }
            else
            {
                t1 = std::min(t1, t);
            }
            if (t0 > t1)
            {
                return false;
            }
        }

        const glm::dvec4 original_a = clip_a;
        const glm::dvec4 original_b = clip_b;
        clip_a = glm::mix(original_a, original_b, t0);
        clip_b = glm::mix(original_a, original_b, t1);
        return clip_a.w > 0.0 && clip_b.w > 0.0;
    }

    glm::vec4 float_clip_position(const glm::dvec4 &clip)
    {
        const glm::vec4 out(clip);
        if (!std::isfinite(out.x) || !std::isfinite(out.y) ||
            !std::isfinite(out.z) || !std::isfinite(out.w))
        {
            return glm::vec4(2.0f, 2.0f, 2.0f, 1.0f);
        }
        return out;
    }

    static void push_line(OrbitPlotVertex *dst,
                          uint32_t &vertex_index,
                          const glm::vec4 &clip_a,
                          const glm::vec4 &clip_b,
                          const glm::vec4 &color,
                          const OrbitPlotLineStyle style,
                          const float dash_coord_a_px,
                          const float dash_coord_b_px)
    {
        const bool dashed =
                style == OrbitPlotLineStyle::Dashed &&
                std::isfinite(dash_coord_a_px) &&
                std::isfinite(dash_coord_b_px) &&
                dash_coord_a_px >= 0.0f &&
                dash_coord_b_px >= 0.0f;

        OrbitPlotVertex v0{};
        v0.clip_position = clip_a;
        v0.dash_coord_px = dashed ? dash_coord_a_px : -1.0f;
        v0.color = color;

        OrbitPlotVertex v1{};
        v1.clip_position = clip_b;
        v1.dash_coord_px = dashed ? dash_coord_b_px : -1.0f;
        v1.color = color;

        dst[vertex_index++] = v0;
        dst[vertex_index++] = v1;
    }
} // namespace

void OrbitPlotSystem::clear_pending()
{
    _pending_lines.clear();
    _stats.pending_line_count = 0;
}

void OrbitPlotSystem::commit_pending()
{
    _active_lines.swap(_pending_lines);
    _pending_lines.clear();
    _stats.active_line_count = static_cast<uint32_t>(_active_lines.size());
    _stats.pending_line_count = 0;
}

void OrbitPlotSystem::clear_all()
{
    _pending_lines.clear();
    _active_lines.clear();
    _stats.active_line_count = 0;
    _stats.pending_line_count = 0;
    _stats.depth_segment_count = 0;
    _stats.overlay_segment_count = 0;
    _stats.upload_bytes_last_frame = 0;
    _stats.upload_bytes_peak = 0;
    _stats.upload_ms_last_frame = 0.0;
    _stats.upload_ms_peak = 0.0;
    _stats.upload_cap_hit_last_frame = false;
    _stats.upload_cap_hits_total = 0;
    _stats.upload_budget_bytes = _settings.upload_budget_bytes;
}

void OrbitPlotSystem::begin_frame()
{
    if (!_pending_lines.empty())
    {
        commit_pending();
    }
    _stats.depth_segment_count = 0;
    _stats.overlay_segment_count = 0;
    _stats.upload_bytes_last_frame = 0;
    _stats.upload_ms_last_frame = 0.0;
    _stats.upload_cap_hit_last_frame = false;
    _stats.upload_budget_bytes = _settings.upload_budget_bytes;
}

void OrbitPlotSystem::add_line(const WorldVec3 &a_world,
                               const WorldVec3 &b_world,
                               const glm::vec4 &color,
                               OrbitPlotDepth depth,
                               OrbitPlotLineStyle style)
{
    LineCommand cmd{};
    cmd.a_world = a_world;
    cmd.b_world = b_world;
    cmd.color = color;
    cmd.depth = depth;
    cmd.style = style;
    add_line_command(cmd);
}

void OrbitPlotSystem::reserve_pending_lines(const std::size_t additional_line_count)
{
    if (additional_line_count == 0u)
    {
        return;
    }
    _pending_lines.reserve(_pending_lines.size() + additional_line_count);
}

void OrbitPlotSystem::add_line_command(const LineCommand &cmd)
{
    _pending_lines.push_back(cmd);
    _stats.pending_line_count = static_cast<uint32_t>(_pending_lines.size());
}

void OrbitPlotSystem::add_lines(std::span<const LineCommand> lines)
{
    if (lines.empty())
    {
        return;
    }

    reserve_pending_lines(lines.size());
    _pending_lines.insert(_pending_lines.end(), lines.begin(), lines.end());
    _stats.pending_line_count = static_cast<uint32_t>(_pending_lines.size());
}

void OrbitPlotSystem::add_lines_translated(std::span<const LineCommand> lines, const WorldVec3 &delta_world)
{
    if (lines.empty())
    {
        return;
    }

    _pending_lines.reserve(_pending_lines.size() + lines.size());
    for (LineCommand cmd : lines)
    {
        cmd.a_world += delta_world;
        cmd.b_world += delta_world;
        _pending_lines.push_back(cmd);
    }
    _stats.pending_line_count = static_cast<uint32_t>(_pending_lines.size());
}

bool OrbitPlotSystem::has_active_lines() const
{
    return !_active_lines.empty();
}

std::span<const OrbitPlotSystem::LineCommand> OrbitPlotSystem::active_lines() const
{
    return std::span<const LineCommand>(_active_lines.data(), _active_lines.size());
}

OrbitPlotSystem::LineVertexCounts OrbitPlotSystem::count_line_vertices() const
{
    LineVertexCounts out{};
    if (!_settings.enabled || _active_lines.empty())
    {
        return out;
    }

    for (const LineCommand &cmd : _active_lines)
    {
        uint32_t &vertex_count =
                (cmd.depth == OrbitPlotDepth::DepthTested) ? out.depth_vertex_count : out.overlay_vertex_count;
        vertex_count += 2u;
    }

    return out;
}

void OrbitPlotSystem::write_line_vertices(const WorldVec3 &origin_world,
                                          const glm::mat4 &viewproj,
                                          OrbitPlotVertex *dst,
                                          const uint32_t depth_vertex_count,
                                          const uint32_t overlay_vertex_count) const
{
    if (!_settings.enabled || _active_lines.empty() || dst == nullptr)
    {
        return;
    }

    OrbitPlotVertex *const depth_dst = dst;
    OrbitPlotVertex *const overlay_dst = dst + depth_vertex_count;
    uint32_t depth_write_index = 0;
    uint32_t overlay_write_index = 0;
    const glm::dmat4 viewproj_d(viewproj);

    for (const LineCommand &cmd : _active_lines)
    {
        OrbitPlotVertex *bucket_dst = depth_dst;
        uint32_t *bucket_write_index = &depth_write_index;
        uint32_t bucket_limit = depth_vertex_count;
        if (cmd.depth == OrbitPlotDepth::AlwaysOnTop)
        {
            bucket_dst = overlay_dst;
            bucket_write_index = &overlay_write_index;
            bucket_limit = overlay_vertex_count;
        }

        if ((*bucket_write_index + 2u) > bucket_limit)
        {
            continue;
        }

        glm::dvec4 clip_a = viewproj_d * glm::dvec4(world_to_local_d(cmd.a_world, origin_world), 1.0);
        glm::dvec4 clip_b = viewproj_d * glm::dvec4(world_to_local_d(cmd.b_world, origin_world), 1.0);
        const bool visible = clip_segment(clip_a, clip_b);
        const glm::vec4 a = visible ? float_clip_position(clip_a)
                                    : glm::vec4(2.0f, 2.0f, 2.0f, 1.0f);
        const glm::vec4 b = visible ? float_clip_position(clip_b)
                                    : glm::vec4(2.0f, 2.0f, 2.0f, 1.0f);
        const glm::vec4 color = visible ? cmd.color : glm::vec4(0.0f);
        push_line(bucket_dst,
                  *bucket_write_index,
                  a,
                  b,
                  color,
                  cmd.style,
                  cmd.dash_coord_a_px,
                  cmd.dash_coord_b_px);
    }
}

OrbitPlotSystem::LineVertexLists OrbitPlotSystem::build_line_vertices(
        const WorldVec3 &origin_world,
        const glm::mat4 &viewproj) const
{
    LineVertexLists out{};
    if (!_settings.enabled || _active_lines.empty())
    {
        return out;
    }

    const LineVertexCounts counts = count_line_vertices();
    out.depth_vertex_count = counts.depth_vertex_count;
    out.overlay_vertex_count = counts.overlay_vertex_count;
    out.vertices.resize(static_cast<std::size_t>(counts.depth_vertex_count) +
                        static_cast<std::size_t>(counts.overlay_vertex_count));
    if (!out.vertices.empty())
    {
        write_line_vertices(origin_world,
                            viewproj,
                            out.vertices.data(),
                            out.depth_vertex_count,
                            out.overlay_vertex_count);
    }
    return out;
}

void OrbitPlotSystem::record_upload_stats(const std::size_t upload_bytes,
                                          const std::size_t upload_budget_bytes,
                                          const double upload_ms,
                                          const bool upload_cap_hit,
                                          const uint32_t depth_segment_count,
                                          const uint32_t overlay_segment_count)
{
    _stats.upload_budget_bytes = upload_budget_bytes;
    _stats.upload_bytes_last_frame = upload_bytes;
    _stats.upload_bytes_peak = std::max(_stats.upload_bytes_peak, upload_bytes);
    _stats.upload_ms_last_frame = std::max(0.0, upload_ms);
    _stats.upload_ms_peak = std::max(_stats.upload_ms_peak, _stats.upload_ms_last_frame);
    _stats.upload_cap_hit_last_frame = upload_cap_hit;
    if (upload_cap_hit)
    {
        ++_stats.upload_cap_hits_total;
    }
    _stats.depth_segment_count = depth_segment_count;
    _stats.overlay_segment_count = overlay_segment_count;
}
