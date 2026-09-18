#include "blackhole_app.h"

#include "core/engine.h"
#include "runtime/game_runtime.h"

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
    auto &api = runtime.api();

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
    if (!api.add_gltf_instance("bmw", "assets/models/bmw/scene.gltf", car, true))
    {
        throw std::runtime_error("Failed to load the BMW model");
    }

    GameAPI::OrbitCameraSettings orbit;
    orbit.target.type = GameAPI::CameraTargetType::WorldPoint;
    orbit.target.worldPoint = glm::dvec3(0.0, 0.7, 0.0);
    orbit.distance = 6.5;
    orbit.yaw = glm::radians(40.0f);
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
}
