#include "blackhole_app.h"

#include "core/engine.h"
#include "runtime/game_runtime.h"
#include "render/passes/blackhole.h"
#include <imgui.h>

#include <algorithm>
#include <stdexcept>

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
    auto *blackhole = runtime.renderer()->_renderPassManager->getPass<BlackholePass>();
    blackhole->enabled = true;
    blackhole->stars = true;
    blackhole->disk = true;
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

    GameAPI::Transform car;
    // The exported model is about 0.046 units long; display it at car scale.
    car.scale = glm::vec3(100.0f);
    car.position.y = 0.0184f;
    car.position.x = 2.5f;
    car.position.z = -2.0f;
    if (!api.add_gltf_instance("bmw", "assets/models/bmw/scene.gltf", car, true))
    {
        throw std::runtime_error("Failed to load the BMW model");
    }

    GameAPI::OrbitCameraSettings orbit;
    orbit.target.type = GameAPI::CameraTargetType::WorldPoint;
    orbit.target.worldPoint = glm::dvec3(0.0, 0.7, 0.0);
    orbit.distance = 10.0;
    orbit.yaw = 0.0f;
    orbit.pitch = glm::radians(12.0f);
    api.set_camera_mode(GameAPI::CameraMode::Orbit);
    api.set_orbit_camera_settings(orbit);
    api.set_camera_fov(50.0f);
}

void BlackholeApp::on_update(float dt)
{
    auto *bh = _runtime->renderer()->_renderPassManager->getPass<BlackholePass>();
    bh->disk_time += dt * bh->disk_speed;
}

void BlackholeApp::on_fixed_update(float /*fixed_dt*/)
{
}

void BlackholeApp::on_shutdown()
{
    _runtime->renderer()->ui()->clearDrawCallbacks();
    _runtime = nullptr;
}

void BlackholeApp::set_free_camera(bool enabled)
{
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

void BlackholeApp::draw_ui()
{
    if (!_runtime) return;
    auto &api = _runtime->api();
    if (!ImGui::GetIO().WantTextInput && !ImGui::IsAnyItemActive() && ImGui::IsKeyPressed(ImGuiKey_C, false))
        set_free_camera(api.get_camera_mode() != GameAPI::CameraMode::Free);
    if (ImGui::IsKeyPressed(ImGuiKey_F2)) _show_ui = !_show_ui;
    if (!_show_ui) return;
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
        bool show_car = _show_car;
        if (ImGui::Checkbox("Render car", &show_car) &&
            _runtime->api().set_gltf_instance_visible("bmw", show_car))
            _show_car = show_car;

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
            int mode = api.get_camera_mode() == GameAPI::CameraMode::Free ? 1 : 0;
            if (ImGui::Combo("##camera_mode", &mode, "Orbit\0Free\0")) set_free_camera(mode == 1);
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
        if (ImGui::Button("Reset orbit"))
        {
            set_free_camera(false);
            auto orbit = api.get_orbit_camera_settings();
            orbit.distance = 10.0;
            orbit.yaw = 0.0f;
            orbit.pitch = glm::radians(12.0f);
            api.set_orbit_camera_settings(orbit);
            api.set_camera_fov(50.0f);
        }

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
                ImGui::SliderFloat("##disk_speed", &bh->disk_speed, -2.0f, 2.0f, "%.2f");
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
}
