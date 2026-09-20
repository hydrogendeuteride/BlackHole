#include "blackhole_app.h"

#include "core/engine.h"
#include "runtime/game_runtime.h"
#include "render/passes/blackhole.h"
#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace
{
    // Same RK4 orbit equation as shaders/blackhole/ray.glsl, with rs = 1.
    void step_ray(float &u, float &du, float dt)
    {
        auto acceleration = [](float x) { return -x * (1.0f - 1.5f * x); };
        float a = acceleration(u);
        float b = acceleration(u + 0.5f * dt * du);
        float c = acceleration(u + 0.5f * dt * (du + 0.5f * dt * a));
        float d = acceleration(u + dt * (du + 0.5f * dt * b));
        u += dt / 6.0f * (6.0f * du + dt * (a + b + c));
        du += dt / 6.0f * (a + 2.0f * b + 2.0f * c + d);
    }

    void draw_rays(float step, float reveal)
    {
        ImGui::TextUnformatted("Side view / camera rays traced backwards / rs = 1");
        ImVec2 size(std::max(300.0f, ImGui::GetContentRegionAvail().x), 300.0f);
        ImVec2 start = ImGui::GetCursorScreenPos();
        ImGui::InvisibleButton("paths", size);
        auto *draw = ImGui::GetWindowDrawList();
        draw->PushClipRect(start, ImVec2(start.x + size.x, start.y + size.y), true);
        draw->AddRectFilled(start, ImVec2(start.x + size.x, start.y + size.y), IM_COL32(12, 16, 24, 255));
        float scale = std::min(size.x / 30.0f, size.y / 20.0f);
        auto screen = [&](glm::vec2 p) { return ImVec2(start.x + size.x * 0.5f + p.x * scale,
                                                      start.y + size.y * 0.5f - p.y * scale); };
        draw->AddCircleFilled(screen({0, 0}), scale, IM_COL32(0, 0, 0, 255), 64);
        draw->AddCircle(screen({0, 0}), scale, IM_COL32(150, 160, 180, 255), 64);
        const float impacts[] = {1.4f, 2.7f, 4.5f};
        const ImU32 colors[] = {IM_COL32(255, 150, 85, 255), IM_COL32(100, 220, 255, 255), IM_COL32(180, 240, 150, 255)};
        for (int ray = 0; ray < 3; ++ray)
        {
            glm::vec2 pos(-12.0f, 0.0f);
            float u = 1.0f / 12.0f;
            float du = 1.0f / impacts[ray];
            float phi = 0.0f;
            for (int i = 0; i < int(768 * reveal); ++i)
            {
                float dt = std::min(step, 0.08f * u / std::max(std::sqrt(u * u + du * du), 1e-6f));
                float old_u = u;
                step_ray(u, du, dt);
                phi += dt;
                if (u <= 0.0f) break;
                if (u >= 1.0f)
                    phi -= dt * (1.0f - std::clamp((1.0f - old_u) / (u - old_u), 0.0f, 1.0f));
                glm::vec2 next(-std::cos(phi), std::sin(phi));
                next /= std::min(u, 1.0f);
                draw->AddLine(screen(pos), screen(next), colors[ray], 2.0f);
                pos = next;
                if (u >= 1.0f || (u < 0.01f && du < 0.0f)) break;
            }
        }
        draw->AddCircleFilled(screen({-12, 0}), 4.0f, IM_COL32_WHITE);
        draw->AddText(screen({-12, -1}), IM_COL32_WHITE, "Camera");
        draw->PopClipRect();
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
    auto *blackhole = runtime.renderer()->_renderPassManager->getPass<BlackholePass>();
    blackhole->enabled = true;
    blackhole->stars = true;
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

void BlackholeApp::on_update(float /*dt*/)
{
}

void BlackholeApp::on_fixed_update(float /*fixed_dt*/)
{
}

void BlackholeApp::on_shutdown()
{
    _runtime->renderer()->ui()->clearDrawCallbacks();
    _runtime = nullptr;
}

void BlackholeApp::draw_ui()
{
    if (!_runtime) return;
    if (ImGui::IsKeyPressed(ImGuiKey_F2)) _show_ui = !_show_ui;
    if (!_show_ui) return;
    auto *blackhole = _runtime->renderer()->_renderPassManager->getPass<BlackholePass>();
    ImGui::SetNextWindowSize(ImVec2(430, 0), ImGuiCond_FirstUseEver);
    const auto *viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(ImVec2(viewport->WorkPos.x + viewport->WorkSize.x - 450,
                                 viewport->WorkPos.y + 30), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Blackhole"))
    {
        ImGui::Checkbox("Lensing", &blackhole->enabled);
        ImGui::Checkbox("Catalog stars", &blackhole->stars);
        if (blackhole->stars)
        {
            ImGui::Text("HYG v4.1 / %u stars / J2000", blackhole->star_count());
            ImGui::SliderFloat("Star brightness", &blackhole->star_brightness, 0.01f, 5.0f, "%.2f", ImGuiSliderFlags_Logarithmic);
            ImGui::SliderFloat("Star size (degrees)", &blackhole->star_size, 0.005f, 0.1f, "%.3f");
            ImGui::SliderFloat("Magnitude limit", &blackhole->star_magnitude, 2.0f, 7.5f, "%.1f");
            ImGui::SliderFloat("Sky rotation", &blackhole->star_rotation, -180.0f, 180.0f, "%.1f");
        }
        ImGui::Checkbox("Mesh lensing (screen space)", &blackhole->meshes);
        ImGui::SliderFloat("Horizon radius", &blackhole->radius, 0.1f, 1.0f);
        glm::vec3 center(blackhole->center);
        if (ImGui::DragFloat3("Position", &center.x, 0.05f)) blackhole->center = center;
        ImGui::SliderFloat("Angular step", &blackhole->step, 0.008f, 0.04f, "%.3f");
        ImGui::SliderFloat("Mesh thickness", &blackhole->thickness, 0.01f, 0.3f, "%.2f");
        ImGui::Checkbox("Light paths", &_show_rays);
        ImGui::TextUnformatted("F2: hide/show this panel");
        if (_show_rays)
        {
            ImGui::SliderFloat("Path reveal", &_ray_progress, 0.0f, 1.0f);
            draw_rays(blackhole->step, _ray_progress);
        }
    }
    ImGui::End();
}
