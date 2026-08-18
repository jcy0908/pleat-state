#include "pleat/Cloth.hpp"
#include "pleat/wasm_api.h"

#include <algorithm>

// CMake Release builds define NDEBUG by default. These are executable
// regression checks, so keep the standard assert macro active in every CI
// configuration rather than letting a green Release job skip its contracts.
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <limits>
#include <vector>

namespace {

constexpr float kTolerance = 1.0e-4F;

[[nodiscard]] bool nearlyEqual(float left, float right, float tolerance = kTolerance) {
    return std::abs(left - right) <= tolerance;
}

[[nodiscard]] bool finite(const pleat::Vec2& point) {
    return std::isfinite(point.x) && std::isfinite(point.y);
}

void pinnedTopRowNeverMoves() {
    pleat::Cloth cloth(18U, 12U, 720.0F, 420.0F);
    cloth.setParameters({0.86F, 2.5F, 900.0F, 240.0F, 28.0F, 6.0F});
    std::vector<pleat::Vec2> pinned;
    for (std::size_t index = 0; index < cloth.columns(); ++index) {
        assert(cloth.isPinned(index));
        pinned.push_back(cloth.particlePosition(index));
    }

    for (int frame = 0; frame < 480; ++frame) {
        cloth.step(1.0F / 60.0F);
    }

    for (std::size_t index = 0; index < pinned.size(); ++index) {
        const auto current = cloth.particlePosition(index);
        assert(nearlyEqual(current.x, pinned[index].x));
        assert(nearlyEqual(current.y, pinned[index].y));
    }
}

void longRunStaysFiniteAndBounded() {
    pleat::Cloth cloth(26U, 18U, 960.0F, 540.0F);
    cloth.setParameters({0.72F, 1.8F, 1600.0F, 900.0F, 48.0F, 9.0F});
    for (int frame = 0; frame < 3600; ++frame) {
        cloth.step(frame % 41 == 0 ? 0.2F : 1.0F / 60.0F);
    }

    for (std::size_t index = 0; index < cloth.particleCount(); ++index) {
        const auto point = cloth.particlePosition(index);
        assert(finite(point));
        assert(point.x >= -cloth.width());
        assert(point.x <= cloth.width() * 2.0F);
        assert(point.y >= -cloth.height());
        assert(point.y <= cloth.height() * 2.0F);
    }
    assert(std::isfinite(cloth.energy()));
}

void resetIsDeterministic() {
    pleat::Cloth cloth(14U, 9U, 560.0F, 300.0F);
    cloth.setParameters({0.9F, 4.0F, 500.0F, -180.0F, 22.0F, 4.0F});
    cloth.reset();
    std::vector<pleat::Vec2> expected;
    expected.reserve(cloth.particleCount());
    for (std::size_t index = 0; index < cloth.particleCount(); ++index) {
        expected.push_back(cloth.particlePosition(index));
    }

    for (int frame = 0; frame < 240; ++frame) {
        cloth.step(1.0F / 60.0F);
    }
    cloth.reset();

    assert(nearlyEqual(cloth.energy(), 0.0F));
    for (std::size_t index = 0; index < expected.size(); ++index) {
        const auto current = cloth.particlePosition(index);
        assert(nearlyEqual(current.x, expected[index].x));
        assert(nearlyEqual(current.y, expected[index].y));
    }
}

void dampingKeepsReleasedEnergyBounded() {
    pleat::Cloth cloth(16U, 11U, 640.0F, 360.0F);
    cloth.setParameters({0.82F, 8.0F, 0.0F, 0.0F, 18.0F, 5.0F});
    const std::size_t target = cloth.particleCount() - cloth.columns() / 2U - 1U;
    const auto original = cloth.particlePosition(target);
    assert(cloth.pointerDown(original.x, original.y, 12.0F));
    cloth.pointerMove(original.x + 105.0F, original.y - 80.0F);
    cloth.pointerUp();

    float peak = 0.0F;
    float lateMaximum = 0.0F;
    for (int frame = 0; frame < 720; ++frame) {
        cloth.step(1.0F / 60.0F);
        assert(std::isfinite(cloth.energy()));
        peak = std::max(peak, cloth.energy());
        if (frame > 600) {
            lateMaximum = std::max(lateMaximum, cloth.energy());
        }
    }
    assert(peak > 0.01F);
    assert(lateMaximum < peak * 0.15F + 0.01F);
}

void pointerAndCapiStayContinuous() {
    ps_destroy();
    assert(ps_particle_count() == 0U);
    assert(ps_positions_ptr() == nullptr);
    assert(ps_create(12, 8, 480.0F, 280.0F) == 1);
    assert(ps_particle_count() == 96U);
    assert(ps_index_count() == 11U * 7U * 6U);
    assert(ps_positions_ptr() != nullptr);
    assert(ps_indices_ptr() != nullptr);

    const float* positions = ps_positions_ptr();
    const std::size_t target = 12U * 6U + 5U;
    const float startX = positions[target * 2U];
    const float startY = positions[target * 2U + 1U];
    assert(ps_pointer_down(startX, startY, 20.0F) == 1);
    ps_pointer_move(startX + 48.0F, startY - 24.0F);
    positions = ps_positions_ptr();
    const float heldX = positions[target * 2U];
    const float heldY = positions[target * 2U + 1U];
    assert(std::isfinite(heldX) && std::isfinite(heldY));

    ps_pointer_up();
    ps_set_params(0.8F, 4.0F, 300.0F, 60.0F, 24.0F, 6.0F);
    ps_step(1.0F / 60.0F);
    positions = ps_positions_ptr();
    const float releasedX = positions[target * 2U];
    const float releasedY = positions[target * 2U + 1U];
    assert(std::isfinite(releasedX) && std::isfinite(releasedY));
    assert(std::hypot(releasedX - heldX, releasedY - heldY) < 120.0F);
    assert(std::isfinite(ps_energy()));

    ps_reset();
    assert(std::isfinite(ps_energy()));
    ps_destroy();
    assert(ps_particle_count() == 0U);
    assert(ps_index_count() == 0U);
}

void invalidInputsFailSafely() {
    assert(ps_create(1, 8, 100.0F, 100.0F) == 0);
    assert(ps_create(513, 8, 100.0F, 100.0F) == 0);
    assert(ps_create(std::numeric_limits<int>::max(),
                     std::numeric_limits<int>::max(),
                     100.0F,
                     100.0F) == 0);
    assert(ps_create(8, 8, std::numeric_limits<float>::quiet_NaN(), 100.0F) == 0);
    pleat::Cloth cloth(8U, 8U, 320.0F, 240.0F);
    const auto before = cloth.particlePosition(10U);
    cloth.step(std::numeric_limits<float>::quiet_NaN());
    cloth.step(-1.0F);
    const auto after = cloth.particlePosition(10U);
    assert(nearlyEqual(before.x, after.x));
    assert(nearlyEqual(before.y, after.y));
    assert(!cloth.pointerDown(0.0F, 0.0F, -5.0F));
}

}  // namespace

int main() {
    pinnedTopRowNeverMoves();
    longRunStaysFiniteAndBounded();
    resetIsDeterministic();
    dampingKeepsReleasedEnergyBounded();
    pointerAndCapiStayContinuous();
    invalidInputsFailSafely();
    std::cout << "pleat-state native tests passed\n";
    return 0;
}
