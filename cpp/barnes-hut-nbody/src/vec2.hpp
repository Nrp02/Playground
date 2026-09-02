#ifndef BARNES_HUT_NBODY_VEC2_HPP
#define BARNES_HUT_NBODY_VEC2_HPP

#include <cmath>

namespace bh {

struct Vec2 {
    double x = 0.0;
    double y = 0.0;

    Vec2() = default;
    Vec2(double xValue, double yValue) : x(xValue), y(yValue) {}

    Vec2& operator+=(const Vec2& other) {
        x += other.x;
        y += other.y;
        return *this;
    }

    Vec2& operator-=(const Vec2& other) {
        x -= other.x;
        y -= other.y;
        return *this;
    }

    Vec2& operator*=(double scalar) {
        x *= scalar;
        y *= scalar;
        return *this;
    }
};

inline Vec2 operator+(Vec2 lhs, const Vec2& rhs) {
    lhs += rhs;
    return lhs;
}

inline Vec2 operator-(Vec2 lhs, const Vec2& rhs) {
    lhs -= rhs;
    return lhs;
}

inline Vec2 operator*(Vec2 v, double scalar) {
    v *= scalar;
    return v;
}

inline Vec2 operator*(double scalar, Vec2 v) {
    v *= scalar;
    return v;
}

inline Vec2 operator-(const Vec2& v) {
    return Vec2(-v.x, -v.y);
}

inline double dot(const Vec2& a, const Vec2& b) {
    return a.x * b.x + a.y * b.y;
}

inline double lengthSquared(const Vec2& v) {
    return v.x * v.x + v.y * v.y;
}

inline double length(const Vec2& v) {
    return std::sqrt(lengthSquared(v));
}

}

#endif
