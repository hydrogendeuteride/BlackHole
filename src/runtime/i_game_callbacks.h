#pragma once

#include <cstdint>

// IGameCallbacks: Interface for game logic callbacks
namespace GameRuntime
{

class Runtime;

class IGameCallbacks
{
public:
    virtual ~IGameCallbacks() = default;

    // called once after runtime initialization before the first update.
    // load initial assets, spawn entities, set up the camera, etc.
    virtual void on_init(Runtime& runtime) = 0;

    // called every frame with variable delta time.
    // rendering dependent logic, input handling, camera control
    virtual void on_update(float dt) = 0;

    // called after native UI event dispatch and before engine picking/camera input.
    // returns a mouse button mask consumed by game-owned screen overlays.
    virtual uint32_t on_pre_input() { return 0; }

    // called at fixed intervals for physics, simulation.
    // physics update, AI tick, game state simulation
    virtual void on_fixed_update(float fixed_dt) = 0;

    // called once before shutdown
    virtual void on_shutdown() = 0;
};

} // namespace GameRuntime
