#pragma once

#include "runtime/i_game_callbacks.h"

class BlackholeApp final : public GameRuntime::IGameCallbacks
{
public:
    void run();

    void on_init(GameRuntime::Runtime &runtime) override;
    void on_update(float dt) override;
    void on_fixed_update(float fixed_dt) override;
    void on_shutdown() override;

private:
    void draw_ui();
    GameRuntime::Runtime *_runtime = nullptr;
    bool _show_ui = true;
    bool _show_car = true;
};
