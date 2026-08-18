#pragma once

#include "pleat/Vec2.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace pleat {

struct Parameters {
    float stiffness{0.82F};
    float damping{3.2F};
    float gravity{620.0F};
    float wind{55.0F};
    float pleatAmplitude{20.0F};
    float pleatFrequency{5.0F};
};

class Cloth {
public:
    Cloth(std::uint32_t columns, std::uint32_t rows, float width, float height);

    void reset();
    void step(float deltaSeconds);
    void setParameters(const Parameters& parameters);

    [[nodiscard]] bool pointerDown(float x, float y, float radius);
    void pointerMove(float x, float y);
    void pointerUp() noexcept;

    [[nodiscard]] std::uint32_t columns() const noexcept { return columns_; }
    [[nodiscard]] std::uint32_t rows() const noexcept { return rows_; }
    [[nodiscard]] float width() const noexcept { return width_; }
    [[nodiscard]] float height() const noexcept { return height_; }
    [[nodiscard]] const Parameters& parameters() const noexcept { return parameters_; }

    [[nodiscard]] std::size_t particleCount() const noexcept { return particles_.size(); }
    [[nodiscard]] const float* positionsData() const noexcept { return renderPositions_.data(); }
    [[nodiscard]] std::size_t indexCount() const noexcept { return triangleIndices_.size(); }
    [[nodiscard]] const std::uint32_t* indicesData() const noexcept { return triangleIndices_.data(); }
    [[nodiscard]] float energy() const noexcept { return energy_; }

    // Lightweight inspection methods keep the native tests and alternate renderers
    // independent of the internal particle representation.
    [[nodiscard]] Vec2 particlePosition(std::size_t index) const;
    [[nodiscard]] Vec2 restPosition(std::size_t index) const;
    [[nodiscard]] bool isPinned(std::size_t index) const;
    [[nodiscard]] bool hasGrabbedParticle() const noexcept { return grabbed_.has_value(); }

private:
    struct Particle {
        Vec2 position{};
        Vec2 previous{};
        Vec2 rest{};
        bool pinned{false};
    };

    struct Constraint {
        std::uint32_t first{0};
        std::uint32_t second{0};
        float restLength{0.0F};
    };

    [[nodiscard]] std::size_t particleIndex(std::uint32_t column, std::uint32_t row) const noexcept;
    void rebuildRestShape();
    void rebuildConstraints();
    void rebuildTriangleIndices();
    void integrate(float stepSeconds);
    void solveConstraints();
    void restoreAnchors();
    void recoverParticle(Particle& particle) const noexcept;
    void syncRenderData(float stepSeconds);

    std::uint32_t columns_{0};
    std::uint32_t rows_{0};
    float width_{0.0F};
    float height_{0.0F};
    Parameters parameters_{};
    std::vector<Particle> particles_{};
    std::vector<Constraint> constraints_{};
    std::vector<std::uint32_t> triangleIndices_{};
    std::vector<float> renderPositions_{};
    std::optional<std::size_t> grabbed_{};
    Vec2 grabOffset_{};
    Vec2 pointerPosition_{};
    float elapsedSeconds_{0.0F};
    double stepAccumulator_{0.0};
    float energy_{0.0F};
};

}  // namespace pleat
