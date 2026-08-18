#include "pleat/Cloth.hpp"
#include "pleat/wasm_api.h"

#include <cstdint>
#include <exception>
#include <limits>
#include <memory>

#ifdef __EMSCRIPTEN__
#include <emscripten/emscripten.h>
#define PS_KEEPALIVE EMSCRIPTEN_KEEPALIVE
#else
#define PS_KEEPALIVE
#endif

namespace {

std::unique_ptr<pleat::Cloth> g_cloth{};

[[nodiscard]] std::uint32_t narrowSize(std::size_t value) noexcept {
    return value > static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max())
        ? std::numeric_limits<std::uint32_t>::max()
        : static_cast<std::uint32_t>(value);
}

}  // namespace

extern "C" {

PS_KEEPALIVE int ps_create(int columns, int rows, float width, float height) {
    if (columns < 2 || rows < 2) {
        return 0;
    }
    try {
        g_cloth = std::make_unique<pleat::Cloth>(static_cast<std::uint32_t>(columns),
                                                static_cast<std::uint32_t>(rows),
                                                width,
                                                height);
        return 1;
    } catch (const std::exception&) {
        g_cloth.reset();
        return 0;
    }
}

PS_KEEPALIVE void ps_destroy(void) {
    g_cloth.reset();
}

PS_KEEPALIVE void ps_reset(void) {
    if (g_cloth) {
        g_cloth->reset();
    }
}

PS_KEEPALIVE void ps_step(float delta_seconds) {
    if (g_cloth) {
        g_cloth->step(delta_seconds);
    }
}

PS_KEEPALIVE void ps_set_params(float stiffness,
                                float damping,
                                float gravity,
                                float wind,
                                float pleat_amplitude,
                                float pleat_frequency) {
    if (g_cloth) {
        g_cloth->setParameters({stiffness,
                                damping,
                                gravity,
                                wind,
                                pleat_amplitude,
                                pleat_frequency});
    }
}

PS_KEEPALIVE int ps_pointer_down(float x, float y, float radius) {
    return g_cloth && g_cloth->pointerDown(x, y, radius) ? 1 : 0;
}

PS_KEEPALIVE void ps_pointer_move(float x, float y) {
    if (g_cloth) {
        g_cloth->pointerMove(x, y);
    }
}

PS_KEEPALIVE void ps_pointer_up(void) {
    if (g_cloth) {
        g_cloth->pointerUp();
    }
}

PS_KEEPALIVE std::uint32_t ps_particle_count(void) {
    return g_cloth ? narrowSize(g_cloth->particleCount()) : 0U;
}

PS_KEEPALIVE const float* ps_positions_ptr(void) {
    return g_cloth ? g_cloth->positionsData() : nullptr;
}

PS_KEEPALIVE std::uint32_t ps_index_count(void) {
    return g_cloth ? narrowSize(g_cloth->indexCount()) : 0U;
}

PS_KEEPALIVE const std::uint32_t* ps_indices_ptr(void) {
    return g_cloth ? g_cloth->indicesData() : nullptr;
}

PS_KEEPALIVE float ps_energy(void) {
    return g_cloth ? g_cloth->energy() : 0.0F;
}

}  // extern "C"
