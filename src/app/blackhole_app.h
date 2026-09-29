#pragma once

#include "runtime/i_game_callbacks.h"
#include "scene/camera.h"
#include "scene/camera/camera_rig.h"
#include <string>
#include <vector>

class BlackholeApp final : public GameRuntime::IGameCallbacks
{
public:
    void run();

    void on_init(GameRuntime::Runtime &runtime) override;
    void on_update(float dt) override;
    void on_fixed_update(float) override {}
    void on_shutdown() override;

private:
    struct SceneModel
    {
        std::string name;
        bool visible = true;
    };

    struct ModelInput
    {
        char path[1024] = "models/porsche_911/scene.gltf";
        char name[128] = "porsche_911";
        glm::vec3 position{2.5f, 0.0240f, -2.0f};
        glm::vec3 rotation{0.0f};
        // Scale the 6.233-unit export to about 4.63 units long.
        glm::vec3 scale{0.743f};
    };

    bool load_model();
    void draw_models();
    void set_models_visible(bool visible);
    void draw_ui();
    void draw_minimap();
    void reset_orbit(const glm::dvec3 &target);
    void set_free_camera(bool enabled);
    void start_cinematic();
    void stop_cinematic();
    void sample_cinematic();
    GameRuntime::Runtime *_runtime = nullptr;
    bool _show_ui = true;
    std::vector<SceneModel> _models;
    ModelInput _model_input;
    std::string _model_error;
    bool _cinematic = false;
    bool _playing = false;
    float _shot_time = 0.0f;
    float _near_radius = 1.25f;
    float _disk_start = 0.0f;
    float _disk_rate = 1.0f;
    Camera _saved_camera;
    CameraMode _saved_mode = CameraMode::Orbit;
    bool _saved_stars = true;
    bool _saved_shift = true;
    bool _saved_lensing = true;
    bool _show_minimap = true;
    bool _keep_minimap = false;
    glm::uvec2 _render_size{};
    bool _toggle_fullscreen = false;
};
