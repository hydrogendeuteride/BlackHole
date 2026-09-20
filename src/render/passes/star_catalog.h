#pragma once

#include <core/types.h>

class EngineContext;

class StarCatalog
{
public:
    void init(EngineContext *context);
    void cleanup(EngineContext *context);

    AllocatedBuffer stars{};
    AllocatedBuffer cells{};
    uint32_t count = 0;
};
