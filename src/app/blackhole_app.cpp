#include "blackhole_app.h"

#include "core/engine.h"
#include "runtime/game_runtime.h"
#include "render/passes/blackhole.h"
#include "scene/vk_scene.h"
#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <stdexcept>

void vk_engine_draw_debug_ui(VulkanEngine *engine);

namespace
{
    constexpr float SHOT_DURATION = 55.0f;

    float ease_range(float time, float begin, float end)
    {
        float t = std::clamp((time - begin) / (end - begin), 0.0f, 1.0f);
        return t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f);
    }

    struct ShotPose
    {
        glm::dvec3 position;
        glm::dvec3 direction;
    };

    ShotPose shot_pose(float time, float near_radius)
    {
        float approach = ease_range(time, 6.0f, 27.0f);
        float depart = ease_range(time, 36.0f, 51.0f);
        // Interpolate radius separately so the path cannot cut through the horizon.
        float radius = glm::mix(22.0f, near_radius, approach);
        radius = glm::mix(radius, 22.0f, depart);
        float azimuth = glm::radians(glm::mix(-20.0f, 95.0f, ease_range(time, 0.0f, 51.0f)));
        float elevation = glm::radians(glm::mix(18.0f, 28.0f, approach * (1.0f - depart)));
        glm::dvec3 radial(std::sin(azimuth) * std::cos(elevation), std::sin(elevation),
                          std::cos(azimuth) * std::cos(elevation));
        glm::dvec3 tangent(std::cos(azimuth), 0.0, -std::sin(azimuth));
        glm::dvec3 position = radial * double(radius);
        // Turn through the rim toward the outward sky before closest approach.
        // Hold that view during initial retreat to make the stellar shift readable.
        float look = ease_range(time, 17.0f, 27.0f) *
                     (1.0f - ease_range(time, 39.0f, 49.0f));
        double angle = glm::radians(172.0 * double(look));
        glm::dvec3 direction = -radial * std::cos(angle) + tangent * std::sin(angle);
        return {position, direction};
    }
}

void BlackholeApp::run()
{
    VulkanEngine engine;
    engine.init();
    try
    {
        GameRuntime::Runtime runtime(&engine);
        runtime.run(this);
    }
    catch (...)
    {
        engine.cleanup();
        throw;
    }
    engine.cleanup();
}

void BlackholeApp::on_init(GameRuntime::Runtime &runtime)
{
    _runtime = &runtime;
    auto &api = runtime.api();
    auto *blackhole = _runtime->renderer()->_renderPassManager->getPass<BlackholePass>();
    blackhole->enabled = true;
    blackhole->stars = true;
    blackhole->disk = true;
    runtime.renderer()->ui()->clearDrawCallbacks();
    runtime.renderer()->ui()->addDrawCallback([this]() { draw_ui(); });

    GameAPI::IBLPaths ibl;
    ibl.specularCube = "assets/ibl/canary_wharf_4k.ktx2";
    ibl.background = ibl.specularCube;
    ibl.brdfLut = "assets/ibl/brdf_lut.ktx2";
    if (!api.load_global_ibl(ibl))
    {
        throw std::runtime_error("Failed to queue the Canary Wharf environment");
    }
    api.set_plain_background(false, glm::vec3(0.0f));
    api.set_sunlight_color(glm::vec3(1.0f), 0.0f);

    if (!load_model()) throw std::runtime_error(_model_error);

    reset_orbit(glm::dvec3(0.0, 0.7, 0.0));
}

void BlackholeApp::reset_orbit(const glm::dvec3 &target)
{
    stop_cinematic();
    auto &api = _runtime->api();
    api.set_camera_mode(GameAPI::CameraMode::Orbit);
    auto orbit = api.get_orbit_camera_settings();
    orbit.target.type = GameAPI::CameraTargetType::WorldPoint;
    orbit.target.worldPoint = target;
    orbit.distance = 10.0;
    orbit.yaw = 0.0f;
    orbit.pitch = glm::radians(12.0f);
    api.set_orbit_camera_settings(orbit);
    api.set_camera_fov(50.0f);
}

bool BlackholeApp::load_model()
{
    auto &api = _runtime->api();
    const auto &input = _model_input;
    _model_error.clear();
    GameAPI::Transform transform;
    if (!input.path[0] || !input.name[0])
        _model_error = "Enter a model path and instance name.";
    else if (api.get_gltf_instance_transform(input.name, transform))
        _model_error = "Instance name already exists. Choose another name.";
    else if (input.scale.x == 0.0f || input.scale.y == 0.0f || input.scale.z == 0.0f)
        _model_error = "Scale must be nonzero on every axis.";
    if (!_model_error.empty()) return false;
    transform.position = input.position;
    transform.rotation = glm::quat(glm::radians(input.rotation));
    transform.scale = input.scale;
    if (!api.add_gltf_instance(input.name, input.path, transform, true))
    {
        _model_error = std::string("Failed to load model: ") + input.path;
        return false;
    }
    _models.push_back({input.name});
    if (_cinematic) api.set_gltf_instance_visible(input.name, false);
    return true;
}

void BlackholeApp::draw_models()
{
    auto &api = _runtime->api();
    ImGui::BeginDisabled(_cinematic);
    for (auto &model : _models)
    {
        bool visible = model.visible && !_cinematic;
        ImGui::PushID(model.name.c_str());
        if (ImGui::Checkbox("Visible", &visible) &&
            api.set_gltf_instance_visible(model.name, visible))
            model.visible = visible;
        ImGui::SameLine();
        ImGui::TextUnformatted(model.name.c_str());
        ImGui::PopID();
    }
    ImGui::EndDisabled();

    if (ImGui::TreeNode("Load model"))
    {
        ImGui::InputText("Path", _model_input.path, sizeof(_model_input.path));
        ImGui::TextWrapped("glTF / GLB: absolute path or relative to assets/.");
        ImGui::InputText("Instance", _model_input.name, sizeof(_model_input.name));
        ImGui::InputFloat3("Position", &_model_input.position.x);
        ImGui::InputFloat3("Rotation (deg)", &_model_input.rotation.x);
        ImGui::InputFloat3("Scale", &_model_input.scale.x);
        if (ImGui::Button("Load")) load_model();
        if (!_model_error.empty()) ImGui::TextWrapped("%s", _model_error.c_str());
        ImGui::TreePop();
    }
}

void BlackholeApp::set_models_visible(bool visible)
{
    for (const auto &model : _models)
        _runtime->api().set_gltf_instance_visible(model.name, visible && model.visible);
}

void BlackholeApp::on_update(float dt)
{
    if (_render_size.x > 0)
    {
        _runtime->api().set_logical_render_extent(_render_size.x, _render_size.y);
        _render_size = {};
    }
    if (_toggle_fullscreen)
    {
        auto *engine = _runtime->renderer();
        const auto mode = engine->_windowMode == VulkanEngine::WindowMode::Windowed
            ? VulkanEngine::WindowMode::FullscreenDesktop : VulkanEngine::WindowMode::Windowed;
        engine->setWindowMode(mode, -1);
        _toggle_fullscreen = false;
    }
    auto *bh = _runtime->renderer()->_renderPassManager->getPass<BlackholePass>();
    if (_cinematic)
    {
        if (_playing) _shot_time = std::min(_shot_time + dt, SHOT_DURATION);
        if (_shot_time >= SHOT_DURATION) _playing = false;
        sample_cinematic();
    }
    else bh->disk_time += dt * bh->disk_speed;
}

void BlackholeApp::start_cinematic()
{
    if (!_cinematic)
    {
        auto &scene = *_runtime->renderer()->_sceneManager;
        auto *bh = _runtime->renderer()->_renderPassManager->getPass<BlackholePass>();
        _saved_camera = scene.getMainCamera();
        _saved_mode = scene.getCameraRig().mode();
        _saved_stars = bh->stars;
        _saved_shift = bh->star_redshift;
        _saved_lensing = bh->enabled;
        _disk_start = bh->disk_time;
        _disk_rate = bh->disk_speed;
        bh->enabled = true;
        bh->stars = true;
        bh->star_redshift = true;
        set_models_visible(false);
        _runtime->api().set_camera_mode(GameAPI::CameraMode::Fixed);
        _cinematic = true;
    }
    _shot_time = 0.0f;
    _playing = true;
    sample_cinematic();
}

void BlackholeApp::stop_cinematic()
{
    if (!_cinematic) return;
    _cinematic = false;
    _playing = false;
    auto &scene = *_runtime->renderer()->_sceneManager;
    auto *bh = _runtime->renderer()->_renderPassManager->getPass<BlackholePass>();
    bh->stars = _saved_stars;
    bh->star_redshift = _saved_shift;
    bh->enabled = _saved_lensing;
    bh->disk_time = _disk_start;
    set_models_visible(true);
    scene.getMainCamera() = _saved_camera;
    scene.getCameraRig().set_mode(_saved_mode, scene, scene.getMainCamera());
}

void BlackholeApp::sample_cinematic()
{
    auto &api = _runtime->api();
    auto *bh = _runtime->renderer()->_renderPassManager->getPass<BlackholePass>();
    const auto pose = shot_pose(_shot_time, _near_radius);
    const glm::dvec3 position = bh->center + pose.position * double(bh->radius);
    api.set_camera_position(position);
    api.camera_look_at(glm::dvec3(position + pose.direction));
    api.set_camera_fov(65.0f);
    bh->disk_time = _disk_start + _shot_time * _disk_rate;
}

void BlackholeApp::on_shutdown()
{
    _runtime->renderer()->ui()->clearDrawCallbacks();
    _runtime = nullptr;
}

void BlackholeApp::set_free_camera(bool enabled)
{
    stop_cinematic();
    auto &api = _runtime->api();
    if (!enabled)
    {
        auto orbit = api.get_orbit_camera_settings();
        orbit.target.type = GameAPI::CameraTargetType::WorldPoint;
        orbit.target.worldPoint = _runtime->renderer()->_renderPassManager->getPass<BlackholePass>()->center;
        api.set_orbit_camera_settings(orbit);
    }
    // The rig preserves the pose on entering Free and derives the orbit from
    // the current position on returning. Its mode switch releases mouse capture.
    api.set_camera_mode(enabled ? GameAPI::CameraMode::Free : GameAPI::CameraMode::Orbit);
}

void BlackholeApp::draw_minimap()
{
    if (!_show_minimap) return;
    const auto *viewport = ImGui::GetMainViewport();
    const float font = ImGui::GetFontSize();
    const float size = std::min({font * 20.0f, viewport->WorkSize.x - 4.0f * font,
                                 viewport->WorkSize.y - 8.0f * font});
    if (size < font * 6.0f) return;
    ImGui::SetNextWindowPos(ImVec2(viewport->WorkPos.x + font,
                                 viewport->WorkPos.y + font),
                            ImGuiCond_Always, ImVec2(0, 0));
    ImGui::SetNextWindowBgAlpha(0.82f);
    const auto flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
                       ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoInputs |
                       ImGuiWindowFlags_NoFocusOnAppearing;
    if (ImGui::Begin("Blackhole minimap", nullptr, flags))
    {
        const ImVec2 origin = ImGui::GetCursorScreenPos();
        const ImVec2 end(origin.x + size, origin.y + size);
        const ImVec2 center(origin.x + size * 0.5f, origin.y + size * 0.5f);
        auto *bh = _runtime->renderer()->_renderPassManager->getPass<BlackholePass>();
        const auto &cam = _runtime->renderer()->_sceneManager->getMainCamera();
        const glm::dvec3 position = (cam.position_world - bh->center) / double(bh->radius);
        const float range = _cinematic ? 24.0f :
            std::max({24.0f, float(std::abs(position.x)) * 1.15f,
                      float(std::abs(position.z)) * 1.15f});
        const float scale = size * 0.46f / range;
        auto project = [&](const glm::dvec3 &p) {
            return ImVec2(center.x + float(p.x) * scale, center.y - float(p.z) * scale);
        };
        auto *draw = ImGui::GetWindowDrawList();
        const auto future = IM_COL32(111, 130, 153, 210);
        const auto past = IM_COL32(255, 180, 86, 255);
        const auto camera = IM_COL32(95, 225, 255, 255);
        draw->PushClipRect(origin, end, true);
        draw->AddRectFilled(origin, end, IM_COL32(8, 13, 23, 230), 6.0f);
        if (bh->disk)
        {
            draw->AddCircle(center, bh->disk_inner * scale, IM_COL32(180, 113, 53, 150), 96);
            draw->AddCircle(center, bh->disk_outer * scale, IM_COL32(180, 113, 53, 150), 96);
        }
        draw->AddCircleFilled(center, scale, IM_COL32(0, 0, 0, 255), 64);
        draw->AddCircle(center, scale, IM_COL32(238, 219, 174, 220), 64, 1.5f);
        if (_cinematic)
        {
            constexpr int SEGMENTS = 240;
            auto previous = project(shot_pose(0.0f, _near_radius).position);
            for (int i = 1; i <= SEGMENTS; ++i)
            {
                const float time = SHOT_DURATION * float(i) / SEGMENTS;
                const auto next = project(shot_pose(time, _near_radius).position);
                draw->AddLine(previous, next, time <= _shot_time ? past : future, 2.0f);
                previous = next;
            }
            const auto start = project(shot_pose(0.0f, _near_radius).position);
            const auto finish = project(shot_pose(SHOT_DURATION, _near_radius).position);
            draw->AddCircleFilled(start, 3.0f, past);
            draw->AddCircle(finish, 4.0f, future, 12, 2.0f);
        }
        const auto marker = project(position);
        const glm::vec3 forward = cam.orientation * glm::vec3(0, 0, -1);
        const float length = std::hypot(forward.x, forward.z);
        if (length > 0.001f)
        {
            const ImVec2 direction(forward.x / length, -forward.z / length);
            const ImVec2 tip(marker.x + direction.x * 19.0f, marker.y + direction.y * 19.0f);
            draw->AddLine(marker, tip, camera, 2.0f);
            draw->AddTriangleFilled(tip,
                ImVec2(tip.x - direction.x * 7.0f - direction.y * 4.0f,
                       tip.y - direction.y * 7.0f + direction.x * 4.0f),
                ImVec2(tip.x - direction.x * 7.0f + direction.y * 4.0f,
                       tip.y - direction.y * 7.0f - direction.x * 4.0f), camera);
        }
        draw->AddCircleFilled(marker, 4.0f, camera);
        draw->PopClipRect();
        ImGui::Dummy(ImVec2(size, size));

    }
    ImGui::End();
}

void BlackholeApp::draw_ui()
{
    if (!_runtime) return;
    auto &api = _runtime->api();
    const auto toggle_playback = [this]() {
        if (!_cinematic || _shot_time >= SHOT_DURATION) start_cinematic();
        else _playing = !_playing;
    };
    if (!ImGui::GetIO().WantTextInput && !ImGui::IsAnyItemActive())
    {
        if (ImGui::IsKeyPressed(ImGuiKey_F5, false)) toggle_playback();
        if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) stop_cinematic();
        if (ImGui::IsKeyPressed(ImGuiKey_F6, false)) _show_minimap = !_show_minimap;
        if (ImGui::IsKeyPressed(ImGuiKey_F11, false)) _toggle_fullscreen = true;
        if (ImGui::IsKeyPressed(ImGuiKey_C, false))
            set_free_camera(api.get_camera_mode() != GameAPI::CameraMode::Free);
    }
    if (ImGui::IsKeyPressed(ImGuiKey_F2)) _show_ui = !_show_ui;
    if (!_show_ui)
    {
        if (_keep_minimap) draw_minimap();
        return;
    }
    vk_engine_draw_debug_ui(_runtime->renderer());
    auto *bh = _runtime->renderer()->_renderPassManager->getPass<BlackholePass>();
    const auto *viewport = ImGui::GetMainViewport();
    const auto &style = ImGui::GetStyle();
    const float font = ImGui::GetFontSize();
    const float label_width = ImGui::CalcTextSize("Angular size (deg)").x + style.ItemSpacing.x * 2.0f;
    const float width = label_width + font * 13.0f + style.WindowPadding.x * 2.0f;
    const float margin = font;
    const float available_width = std::max(1.0f, viewport->WorkSize.x - margin * 2.0f);
    const float available_height = std::max(1.0f, viewport->WorkSize.y - margin * 2.0f);
    const float panel_width = std::min(width, available_width);
    // Size from the active font and actual content; ignore stale saved window sizes.
    ImGui::SetNextWindowSizeConstraints(ImVec2(panel_width, 0), ImVec2(panel_width, available_height));
    ImGui::SetNextWindowPos(ImVec2(viewport->WorkPos.x + viewport->WorkSize.x - margin,
                                 viewport->WorkPos.y + margin), ImGuiCond_Always, ImVec2(1, 0));
    const ImGuiWindowFlags flags = ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoCollapse |
                                   ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoMove;
    if (ImGui::Begin("Blackhole", nullptr, flags))
    {
        draw_models();

        if (ImGui::Button(_cinematic ? "Restart cinematic" : "Play cinematic (F5)")) start_cinematic();
        if (_cinematic)
        {
            ImGui::SameLine();
            if (ImGui::Button(_playing ? "Pause" : "Resume")) toggle_playback();
            ImGui::SetNextItemWidth(-1);
            if (ImGui::SliderFloat("##shot_time", &_shot_time, 0.0f, SHOT_DURATION, "%.1f s", ImGuiSliderFlags_AlwaysClamp))
            {
                _playing = false;
                sample_cinematic();
            }
            if (ImGui::Button("Exit cinematic (Esc)")) stop_cinematic();
            ImGui::TextDisabled("F5: play/pause | F2: hide UI");
            if (_cinematic)
            {
                float radius = float(glm::length(api.get_camera_position_d() - bh->center)) / bh->radius;
                ImGui::Text("Distance: %.2f Rs | Star shift: %.2fx", radius,
                            bh->star_redshift ? 1.0f / std::sqrt(1.0f - 1.0f / radius) : 1.0f);
            }
        }
        ImGui::SetNextItemWidth(font * 7.0f);
        if (ImGui::SliderFloat("Closest (Rs)", &_near_radius, 1.08f, 2.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp) && _cinematic)
            sample_cinematic();

        ImGui::Checkbox("Minimap (F6)", &_show_minimap);
        if (_show_minimap) ImGui::Checkbox("Keep map when UI hidden", &_keep_minimap);

        bool fullscreen = api.get_window_settings().mode != GameAPI::WindowMode::Windowed;
        if (ImGui::Checkbox("Fullscreen (F11)", &fullscreen)) _toggle_fullscreen = true;
        const auto extent = _runtime->renderer()->_logicalRenderExtent;
        const glm::uvec2 sizes[] = {{1280, 720}, {1920, 1080}, {2560, 1440}, {3840, 2160}};
        int resolution = -1;
        for (int i = 0; i < 4; ++i)
            if (extent.width == sizes[i].x && extent.height == sizes[i].y) resolution = i;
        ImGui::SetNextItemWidth(font * 9.0f);
        if (ImGui::Combo("Resolution", &resolution, "1280 x 720\0" "1920 x 1080\0" "2560 x 1440\0" "3840 x 2160\0"))
            _render_size = sizes[resolution];

        auto section = [&](const char *label, bool &enabled)
        {
            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();
            ImGui::Checkbox(label, &enabled);
        };
        auto settings = [&](const char *id)
        {
            if (!ImGui::BeginTable(id, 2, ImGuiTableFlags_SizingStretchProp)) return false;
            ImGui::TableSetupColumn("Label", ImGuiTableColumnFlags_WidthFixed, label_width);
            ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);
            return true;
        };
        auto row = [&](const char *label)
        {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted(label);
            ImGui::TableSetColumnIndex(1);
            ImGui::SetNextItemWidth(-1);
        };
        if (settings("camera"))
        {
            row("Camera (C)");
            int mode = _cinematic ? 2 : (api.get_camera_mode() == GameAPI::CameraMode::Free ? 1 : 0);
            if (ImGui::Combo("##camera_mode", &mode, "Orbit\0Free\0Cinematic\0"))
            {
                if (mode == 2) start_cinematic();
                else set_free_camera(mode == 1);
            }
            if (mode == 1)
            {
                auto free = api.get_free_camera_settings();
                row("Move speed");
                if (ImGui::DragFloat("##move_speed", &free.moveSpeed, 0.02f, 0.06f, 100.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp))
                    api.set_free_camera_settings(free);
            }
            ImGui::EndTable();
        }
        if (api.get_camera_mode() == GameAPI::CameraMode::Free)
        {
            ImGui::TextDisabled("WASD: move | Space/Ctrl: up/down");
            ImGui::TextDisabled("RMB: look | Q/E: roll | Wheel: speed");
        }
        if (ImGui::Button("Reset orbit")) reset_orbit(bh->center);

        section("1  Gravitational lensing", bh->enabled);
        if (settings("blackhole"))
        {
            row("Horizon radius");
            ImGui::SliderFloat("##radius", &bh->radius, 0.1f, 1.0f, "%.2f");
            row("Position");
            glm::vec3 center(bh->center);
            if (ImGui::DragFloat3("##center", &center.x, 0.05f, 0, 0, "%.2f")) bh->center = center;
            ImGui::BeginDisabled(!bh->enabled);
            row("Angular step");
            ImGui::SliderFloat("##step", &bh->step, 0.008f, 0.04f, "%.3f");
            ImGui::EndDisabled();
            ImGui::EndTable();
        }
        ImGui::BeginDisabled(!bh->enabled);
        section("2  Mesh lensing", bh->meshes);
        ImGui::BeginDisabled(!bh->meshes);
        if (settings("mesh"))
        {
            row("Mesh thickness");
            ImGui::SliderFloat("##thickness", &bh->thickness, 0.01f, 0.3f, "%.2f");
            ImGui::EndTable();
        }
        ImGui::EndDisabled();
        ImGui::EndDisabled();

        section("3  Stars", bh->stars);
        ImGui::BeginDisabled(!bh->stars);
        if (settings("stars"))
        {
            row("Brightness");
            ImGui::SliderFloat("##star_brightness", &bh->star_brightness, 0.01f, 5.0f, "%.2f", ImGuiSliderFlags_Logarithmic);
            row("Angular size (deg)");
            ImGui::SliderFloat("##star_size", &bh->star_size, 0.005f, 0.1f, "%.3f");
            row("Magnitude limit");
            ImGui::SliderFloat("##magnitude", &bh->star_magnitude, 2.0f, 7.5f, "%.1f");
            row("Sky rotation (deg)");
            ImGui::SliderFloat("##rotation", &bh->star_rotation, -180.0f, 180.0f, "%.1f");
            row("Gravitational shift");
            ImGui::Checkbox("##star_redshift", &bh->star_redshift);
            ImGui::EndTable();
        }
        ImGui::EndDisabled();

        section("4  Disk", bh->disk);
        ImGui::BeginDisabled(!bh->disk);
        if (settings("disk_volume"))
        {
            row("Mode");
            int mode = bh->disk_clouds ? 1 : 0;
            if (ImGui::Combo("##disk_mode", &mode, "Test grid\0Cloud disk\0"))
                bh->disk_clouds = mode == 1;
            if (bh->disk_clouds)
            {
                row("Height (rs)");
                ImGui::SliderFloat("##disk_height", &bh->disk_height, 0.02f, 0.3f, "%.2f");
                row("Cloud contrast");
                ImGui::SliderFloat("##disk_contrast", &bh->disk_contrast, 0.0f, 1.0f, "%.2f");
                row("Animation speed");
                ImGui::BeginDisabled(_cinematic);
                ImGui::SliderFloat("##disk_speed", &bh->disk_speed, -2.0f, 2.0f, "%.2f");
                ImGui::EndDisabled();
                row("Inner temp (K)");
                ImGui::SliderFloat("##disk_temperature", &bh->disk_temperature, 3000.0f, 20000.0f, "%.0f", ImGuiSliderFlags_Logarithmic);
                row("Absorption (1/rs)");
                ImGui::SliderFloat("##disk_absorption", &bh->disk_absorption, 0.1f, 40.0f, "%.2f", ImGuiSliderFlags_Logarithmic);
                row("Emission");
                ImGui::SliderFloat("##disk_emission", &bh->disk_emission, 0.05f, 5.0f, "%.2f", ImGuiSliderFlags_Logarithmic);
            }
            ImGui::EndTable();
        }
        ImGui::EndDisabled();

        ImGui::BeginDisabled(!bh->disk || !bh->disk_clouds);
        section("5  Gravitational redshift", bh->disk_redshift);
        section("6  Doppler shift", bh->disk_doppler);
        ImGui::BeginDisabled(!bh->disk_redshift && !bh->disk_doppler);
        ImGui::Checkbox("Relativistic brightness", &bh->disk_beaming);
        ImGui::EndDisabled();
        ImGui::EndDisabled();

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::TextDisabled("F2: hide UI");
    }
    ImGui::End();
    draw_minimap();
}
