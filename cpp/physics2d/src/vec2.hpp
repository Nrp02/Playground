#pragma once

#include <cmath>

struct Vec2 {
    double x;
    double y;

    Vec2() : x(0.0), y(0.0) {}
    Vec2(double xIn, double yIn) : x(xIn), y(yIn) {}

    Vec2 operator+(const Vec2& o) const { return Vec2(x + o.x, y + o.y); }
    Vec2 operator-(const Vec2& o) const { return Vec2(x - o.x, y - o.y); }
    Vec2 operator-() const { return Vec2(-x, -y); }
    Vec2 operator*(double s) const { return Vec2(x * s, y * s); }
    Vec2 operator/(double s) const { return Vec2(x / s, y / s); }

    Vec2& operator+=(const Vec2& o) {
        x += o.x;
        y += o.y;
        return *this;
    }

    Vec2& operator-=(const Vec2& o) {
        x -= o.x;
        y -= o.y;
        return *this;
    }

    Vec2& operator*=(double s) {
        x *= s;
        y *= s;
        return *this;
    }
};

inline Vec2 operator*(double s, const Vec2& v) {
    return Vec2(v.x * s, v.y * s);
}

inline double dot(const Vec2& a, const Vec2& b) {
    return a.x * b.x + a.y * b.y;
}

inline double cross(const Vec2& a, const Vec2& b) {
    return a.x * b.y - a.y * b.x;
}

inline Vec2 cross(double s, const Vec2& a) {
    return Vec2(-s * a.y, s * a.x);
}

inline double lengthSquared(const Vec2& v) {
    return v.x * v.x + v.y * v.y;
}

inline double length(const Vec2& v) {
    return std::sqrt(lengthSquared(v));
}

inline Vec2 normalized(const Vec2& v) {
    double len = length(v);
    if (len < 1e-12) {
        return Vec2(0.0, 0.0);
    }
    return Vec2(v.x / len, v.y / len);
}

inline Vec2 rotate(const Vec2& v, double angle) {
    double c = std::cos(angle);
    double s = std::sin(angle);
    return Vec2(v.x * c - v.y * s, v.x * s + v.y * c);
}
