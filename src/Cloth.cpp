#include "pleat/Cloth.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace pleat {
namespace {

constexpr float kPi = 3.14159265358979323846F;
constexpr float kMinimumDimension = 1.0F;
constexpr float kMaximumFrameStep = 1.0F / 20.0F;
constexpr float kFixedStep = 1.0F / 120.0F;
constexpr int kMaximumCatchupSteps = 6;
constexpr int kConstraintIterations = 8;
constexpr float kEpsilon = 1.0e-6F;
constexpr std::uint32_t kMaximumGridAxis = 512U;

[[nodiscard]] float finiteOr(float value, float fallback) noexcept {
    return std::isfinite(value) ? value : fallback;
}

[[nodiscard]] float clampFinite(float value, float fallback, float minimum, float maximum) noexcept {
    return std::clamp(finiteOr(value, fallback), minimum, maximum);
}

[[nodiscard]] Vec2 clampLength(Vec2 value, float maximumLength) noexcept {
    const float squared = lengthSquared(value);
    const float maximumSquared = maximumLength * maximumLength;
    if (squared <= maximumSquared || squared <= kEpsilon) {
        return value;
    }
    return value * (maximumLength / std::sqrt(squared));
}

}  // namespace

Cloth::Cloth(std::uint32_t columns, std::uint32_t rows, float width, float height)
    : columns_(columns),
      rows_(rows),
      width_(width),
      height_(height) {
    if (columns_ < 2U || rows_ < 2U) {
        throw std::invalid_argument("Cloth requires at least two columns and two rows");
    }
    // The browser ABI accepts signed integers supplied by JavaScript. A firm
    // grid limit bounds allocation cost and makes all particle/index products
    // safe in uint32_t as well as size_t on supported native and WASM targets.
    if (columns_ > kMaximumGridAxis || rows_ > kMaximumGridAxis) {
        throw std::invalid_argument("Cloth grid axes may not exceed 512 particles");
    }
    if (!std::isfinite(width_) || !std::isfinite(height_) ||
        width_ < kMinimumDimension || height_ < kMinimumDimension) {
        throw std::invalid_argument("Cloth dimensions must be finite and positive");
    }

    const auto count = static_cast<std::size_t>(columns_) * static_cast<std::size_t>(rows_);
    particles_.resize(count);
    renderPositions_.resize(count * 2U);
    rebuildRestShape();
    rebuildConstraints();
    rebuildTriangleIndices();
    reset();
}

std::size_t Cloth::particleIndex(std::uint32_t column, std::uint32_t row) const noexcept {
    return static_cast<std::size_t>(row) * static_cast<std::size_t>(columns_) + column;
}

void Cloth::setParameters(const Parameters& candidate) {
    Parameters next{};
    next.stiffness = clampFinite(candidate.stiffness, parameters_.stiffness, 0.0F, 1.0F);
    next.damping = clampFinite(candidate.damping, parameters_.damping, 0.0F, 24.0F);
    next.gravity = clampFinite(candidate.gravity, parameters_.gravity, -5000.0F, 5000.0F);
    next.wind = clampFinite(candidate.wind, parameters_.wind, -5000.0F, 5000.0F);
    const float maximumAmplitude = std::min(width_, height_) * 0.35F;
    next.pleatAmplitude = clampFinite(candidate.pleatAmplitude,
                                      parameters_.pleatAmplitude,
                                      0.0F,
                                      maximumAmplitude);
    next.pleatFrequency = clampFinite(candidate.pleatFrequency,
                                      parameters_.pleatFrequency,
                                      0.25F,
                                      24.0F);

    const bool restShapeChanged =
        std::abs(next.pleatAmplitude - parameters_.pleatAmplitude) > kEpsilon ||
        std::abs(next.pleatFrequency - parameters_.pleatFrequency) > kEpsilon;
    parameters_ = next;

    if (restShapeChanged) {
        rebuildRestShape();
        rebuildConstraints();
        restoreAnchors();
        syncRenderData(kFixedStep);
    }
}

void Cloth::rebuildRestShape() {
    const float columnDenominator = static_cast<float>(columns_ - 1U);
    const float rowDenominator = static_cast<float>(rows_ - 1U);

    for (std::uint32_t row = 0; row < rows_; ++row) {
        const float v = static_cast<float>(row) / rowDenominator;
        for (std::uint32_t column = 0; column < columns_; ++column) {
            const float u = static_cast<float>(column) / columnDenominator;
            const float phase = u * parameters_.pleatFrequency * 2.0F * kPi;
            const float verticalFade = 0.12F + 0.88F * v;
            const float fold = parameters_.pleatAmplitude * std::sin(phase) * verticalFade;
            auto& particle = particles_[particleIndex(column, row)];
            particle.rest = {u * width_, v * height_ + fold};
            particle.pinned = row == 0U;
        }
    }
}

void Cloth::rebuildConstraints() {
    constraints_.clear();
    const auto horizontal = static_cast<std::size_t>(columns_ - 1U) * rows_;
    const auto vertical = static_cast<std::size_t>(rows_ - 1U) * columns_;
    const auto shear = static_cast<std::size_t>(columns_ - 1U) * (rows_ - 1U) * 2U;
    constraints_.reserve(horizontal + vertical + shear);

    const auto addConstraint = [this](std::uint32_t first, std::uint32_t second) {
        const Vec2 delta = particles_[second].rest - particles_[first].rest;
        constraints_.push_back({first, second, std::max(length(delta), kEpsilon)});
    };

    for (std::uint32_t row = 0; row < rows_; ++row) {
        for (std::uint32_t column = 0; column < columns_; ++column) {
            const auto current = static_cast<std::uint32_t>(particleIndex(column, row));
            if (column + 1U < columns_) {
                addConstraint(current,
                              static_cast<std::uint32_t>(particleIndex(column + 1U, row)));
            }
            if (row + 1U < rows_) {
                addConstraint(current,
                              static_cast<std::uint32_t>(particleIndex(column, row + 1U)));
            }
            if (column + 1U < columns_ && row + 1U < rows_) {
                addConstraint(current,
                              static_cast<std::uint32_t>(particleIndex(column + 1U, row + 1U)));
                addConstraint(static_cast<std::uint32_t>(particleIndex(column + 1U, row)),
                              static_cast<std::uint32_t>(particleIndex(column, row + 1U)));
            }
        }
    }
}

void Cloth::rebuildTriangleIndices() {
    triangleIndices_.clear();
    triangleIndices_.reserve(static_cast<std::size_t>(columns_ - 1U) *
                             static_cast<std::size_t>(rows_ - 1U) * 6U);
    for (std::uint32_t row = 0; row + 1U < rows_; ++row) {
        for (std::uint32_t column = 0; column + 1U < columns_; ++column) {
            const auto upperLeft = static_cast<std::uint32_t>(particleIndex(column, row));
            const auto upperRight = static_cast<std::uint32_t>(particleIndex(column + 1U, row));
            const auto lowerLeft = static_cast<std::uint32_t>(particleIndex(column, row + 1U));
            const auto lowerRight = static_cast<std::uint32_t>(particleIndex(column + 1U, row + 1U));
            triangleIndices_.insert(triangleIndices_.end(),
                                    {upperLeft, lowerLeft, upperRight,
                                     upperRight, lowerLeft, lowerRight});
        }
    }
}

void Cloth::reset() {
    grabbed_.reset();
    grabOffset_ = {};
    pointerPosition_ = {};
    elapsedSeconds_ = 0.0F;
    stepAccumulator_ = 0.0;
    energy_ = 0.0F;
    for (auto& particle : particles_) {
        particle.position = particle.rest;
        particle.previous = particle.rest;
    }
    syncRenderData(kFixedStep);
}

void Cloth::step(float deltaSeconds) {
    if (!std::isfinite(deltaSeconds) || deltaSeconds <= 0.0F) {
        return;
    }

    const double frameStep = static_cast<double>(std::min(deltaSeconds, kMaximumFrameStep));
    stepAccumulator_ = std::min(stepAccumulator_ + frameStep,
                                static_cast<double>(kMaximumFrameStep));

    int catchupSteps = 0;
    while (stepAccumulator_ + 1.0e-9 >= static_cast<double>(kFixedStep) &&
           catchupSteps < kMaximumCatchupSteps) {
        elapsedSeconds_ += kFixedStep;
        integrate(kFixedStep);
        solveConstraints();
        syncRenderData(kFixedStep);
        stepAccumulator_ -= static_cast<double>(kFixedStep);
        ++catchupSteps;
    }

    // Floating-point subtraction can leave a tiny negative remainder.
    stepAccumulator_ = std::max(0.0, stepAccumulator_);
}

void Cloth::integrate(float stepSeconds) {
    const float velocityDamping = std::exp(-parameters_.damping * stepSeconds);
    const float maximumDisplacement = std::max(width_, height_) * 4.0F * stepSeconds;

    for (std::size_t index = 0; index < particles_.size(); ++index) {
        auto& particle = particles_[index];
        if (particle.pinned || (grabbed_.has_value() && *grabbed_ == index)) {
            continue;
        }

        if (!isFinite(particle.position) || !isFinite(particle.previous)) {
            recoverParticle(particle);
            continue;
        }

        Vec2 displacement = (particle.position - particle.previous) * velocityDamping;
        displacement = clampLength(displacement, maximumDisplacement);
        const float normalizedX = particle.rest.x / width_;
        const float normalizedY = particle.rest.y / height_;
        const float gust = 0.68F + 0.32F *
            std::sin(elapsedSeconds_ * 1.7F + normalizedX * 4.3F + normalizedY * 2.1F);
        const Vec2 acceleration{parameters_.wind * gust, parameters_.gravity};

        const Vec2 current = particle.position;
        particle.position += displacement + acceleration * (stepSeconds * stepSeconds);
        particle.previous = current;

        if (!isFinite(particle.position)) {
            recoverParticle(particle);
        }
    }
    restoreAnchors();
}

void Cloth::solveConstraints() {
    const float iterationStiffness = parameters_.stiffness <= 0.0F
        ? 0.0F
        : 1.0F - std::pow(1.0F - parameters_.stiffness,
                          1.0F / static_cast<float>(kConstraintIterations));

    for (int iteration = 0; iteration < kConstraintIterations; ++iteration) {
        for (const auto& constraint : constraints_) {
            auto& first = particles_[constraint.first];
            auto& second = particles_[constraint.second];
            const bool firstAnchored = first.pinned ||
                (grabbed_.has_value() && *grabbed_ == constraint.first);
            const bool secondAnchored = second.pinned ||
                (grabbed_.has_value() && *grabbed_ == constraint.second);
            const float firstWeight = firstAnchored ? 0.0F : 1.0F;
            const float secondWeight = secondAnchored ? 0.0F : 1.0F;
            const float weightSum = firstWeight + secondWeight;
            if (weightSum <= 0.0F) {
                continue;
            }

            const Vec2 delta = second.position - first.position;
            const float distanceSquared = lengthSquared(delta);
            if (!std::isfinite(distanceSquared) || distanceSquared <= kEpsilon * kEpsilon) {
                continue;
            }
            const float distance = std::sqrt(distanceSquared);
            const float relativeError = (distance - constraint.restLength) / distance;
            const Vec2 correction = delta * (iterationStiffness * relativeError / weightSum);
            first.position += correction * firstWeight;
            second.position -= correction * secondWeight;
        }
        restoreAnchors();
    }

    const float marginX = width_ * 0.35F;
    const float marginY = height_ * 0.35F;
    for (auto& particle : particles_) {
        if (!isFinite(particle.position)) {
            recoverParticle(particle);
        }
        particle.position.x = std::clamp(particle.position.x, -marginX, width_ + marginX);
        particle.position.y = std::clamp(particle.position.y, -marginY, height_ + marginY);
    }
    restoreAnchors();
}

void Cloth::restoreAnchors() {
    for (auto& particle : particles_) {
        if (particle.pinned) {
            particle.position = particle.rest;
            particle.previous = particle.rest;
        }
    }
    if (grabbed_.has_value()) {
        auto& particle = particles_[*grabbed_];
        particle.position = pointerPosition_ + grabOffset_;
        particle.previous = particle.position;
    }
}

void Cloth::recoverParticle(Particle& particle) const noexcept {
    particle.position = particle.rest;
    particle.previous = particle.rest;
}

void Cloth::syncRenderData(float stepSeconds) {
    double kineticEnergy = 0.0;
    const float safeStep = std::max(stepSeconds, kEpsilon);
    for (std::size_t index = 0; index < particles_.size(); ++index) {
        const auto& particle = particles_[index];
        renderPositions_[index * 2U] = particle.position.x;
        renderPositions_[index * 2U + 1U] = particle.position.y;
        if (!particle.pinned && !(grabbed_.has_value() && *grabbed_ == index)) {
            const Vec2 velocity = (particle.position - particle.previous) / safeStep;
            if (isFinite(velocity)) {
                kineticEnergy += 0.5 * static_cast<double>(lengthSquared(velocity));
            }
        }
    }

    double constraintEnergy = 0.0;
    for (const auto& constraint : constraints_) {
        const float currentLength = length(particles_[constraint.second].position -
                                           particles_[constraint.first].position);
        if (std::isfinite(currentLength)) {
            const double extension = static_cast<double>(currentLength - constraint.restLength);
            constraintEnergy += static_cast<double>(parameters_.stiffness) * extension * extension;
        }
    }
    const double total = kineticEnergy + constraintEnergy;
    energy_ = std::isfinite(total)
        ? static_cast<float>(std::min(total, static_cast<double>(std::numeric_limits<float>::max())))
        : std::numeric_limits<float>::max();
}

bool Cloth::pointerDown(float x, float y, float radius) {
    if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(radius) || radius <= 0.0F) {
        return false;
    }

    const Vec2 pointer{x, y};
    const float radiusSquared = radius * radius;
    float nearestDistanceSquared = radiusSquared;
    std::optional<std::size_t> nearest{};
    for (std::size_t index = 0; index < particles_.size(); ++index) {
        if (particles_[index].pinned) {
            continue;
        }
        const float distanceSquared = lengthSquared(particles_[index].position - pointer);
        if (std::isfinite(distanceSquared) && distanceSquared <= nearestDistanceSquared) {
            nearestDistanceSquared = distanceSquared;
            nearest = index;
        }
    }
    if (!nearest.has_value()) {
        return false;
    }

    grabbed_ = nearest;
    pointerPosition_ = pointer;
    grabOffset_ = particles_[*nearest].position - pointer;
    particles_[*nearest].previous = particles_[*nearest].position;
    return true;
}

void Cloth::pointerMove(float x, float y) {
    if (!grabbed_.has_value() || !std::isfinite(x) || !std::isfinite(y)) {
        return;
    }
    pointerPosition_ = {x, y};
    restoreAnchors();
    syncRenderData(kFixedStep);
}

void Cloth::pointerUp() noexcept {
    if (grabbed_.has_value()) {
        auto& particle = particles_[*grabbed_];
        particle.previous = particle.position;
    }
    grabbed_.reset();
}

Vec2 Cloth::particlePosition(std::size_t index) const {
    return particles_.at(index).position;
}

Vec2 Cloth::restPosition(std::size_t index) const {
    return particles_.at(index).rest;
}

bool Cloth::isPinned(std::size_t index) const {
    return particles_.at(index).pinned;
}

}  // namespace pleat
