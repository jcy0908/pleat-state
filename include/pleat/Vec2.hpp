#pragma once

#include <cmath>

namespace pleat {

struct Vec2 {
    float x{0.0F};
    float y{0.0F};

    [[nodiscard]] constexpr Vec2 operator+(const Vec2& other) const noexcept {
        return {x + other.x, y + other.y};
    }

    [[nodiscard]] constexpr Vec2 operator-(const Vec2& other) const noexcept {
        return {x - other.x, y - other.y};
    }

    [[nodiscard]] constexpr Vec2 operator*(float scalar) const noexcept {
        return {x * scalar, y * scalar};
    }

    [[nodiscard]] constexpr Vec2 operator/(float scalar) const noexcept {
        return {x / scalar, y / scalar};
    }

    constexpr Vec2& operator+=(const Vec2& other) noexcept {
        x += other.x;
        y += other.y;
        return *this;
    }

    constexpr Vec2& operator-=(const Vec2& other) noexcept {
        x -= other.x;
        y -= other.y;
        return *this;
    }

    constexpr Vec2& operator*=(float scalar) noexcept {
        x *= scalar;
        y *= scalar;
        return *this;
    }
};

[[nodiscard]] constexpr Vec2 operator*(float scalar, const Vec2& value) noexcept {
    return value * scalar;
}

[[nodiscard]] constexpr float dot(const Vec2& left, const Vec2& right) noexcept {
    return left.x * right.x + left.y * right.y;
}

[[nodiscard]] inline float lengthSquared(const Vec2& value) noexcept {
    return dot(value, value);
}

[[nodiscard]] inline float length(const Vec2& value) noexcept {
    return std::sqrt(lengthSquared(value));
}

[[nodiscard]] inline bool isFinite(const Vec2& value) noexcept {
    return std::isfinite(value.x) && std::isfinite(value.y);
}

}  // namespace pleat
