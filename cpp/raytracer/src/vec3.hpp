#ifndef RAYTRACER_VEC3_HPP
#define RAYTRACER_VEC3_HPP

#include <cmath>
#include <cstdlib>
#include <ostream>

namespace rt {

// A minimal 3-component vector used for both points and colors. Keeping a
// single type for both (rather than separate Point3/Color classes) mirrors
// what most small ray tracers do and keeps the arithmetic below reusable.
class Vec3 {
public:
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;

    Vec3() = default;
    Vec3(double x_, double y_, double z_) : x(x_), y(y_), z(z_) {}

    Vec3 operator-() const { return Vec3(-x, -y, -z); }

    Vec3& operator+=(const Vec3& v) {
        x += v.x;
        y += v.y;
        z += v.z;
        return *this;
    }

    Vec3& operator*=(double t) {
        x *= t;
        y *= t;
        z *= t;
        return *this;
    }

    Vec3& operator/=(double t) { return *this *= (1.0 / t); }

    double lengthSquared() const { return x * x + y * y + z * z; }
    double length() const { return std::sqrt(lengthSquared()); }

    // True if every component is close to zero; used to avoid degenerate
    // scatter directions when a diffuse bounce direction nearly cancels
    // the surface normal.
    bool nearZero() const {
        constexpr double kEps = 1e-8;
        return std::fabs(x) < kEps && std::fabs(y) < kEps && std::fabs(z) < kEps;
    }
};

inline Vec3 operator+(const Vec3& a, const Vec3& b) { return Vec3(a.x + b.x, a.y + b.y, a.z + b.z); }
inline Vec3 operator-(const Vec3& a, const Vec3& b) { return Vec3(a.x - b.x, a.y - b.y, a.z - b.z); }
inline Vec3 operator*(const Vec3& a, const Vec3& b) { return Vec3(a.x * b.x, a.y * b.y, a.z * b.z); }
inline Vec3 operator*(double t, const Vec3& v) { return Vec3(t * v.x, t * v.y, t * v.z); }
inline Vec3 operator*(const Vec3& v, double t) { return t * v; }
inline Vec3 operator/(const Vec3& v, double t) { return v * (1.0 / t); }

inline double dot(const Vec3& a, const Vec3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }

inline Vec3 cross(const Vec3& a, const Vec3& b) {
    return Vec3(a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x);
}

inline Vec3 unitVector(const Vec3& v) { return v / v.length(); }

// Reflects `v` about a surface with unit normal `n` (used for specular /
// mirror-like bounces).
inline Vec3 reflect(const Vec3& v, const Vec3& n) { return v - 2.0 * dot(v, n) * n; }

// Refracts `uv` (a unit vector) through a surface with unit normal `n`
// given the ratio of refractive indices etaiOverEtat, per Snell's law.
// Used for dielectric materials (glass-like spheres).
inline Vec3 refract(const Vec3& uv, const Vec3& n, double etaiOverEtat) {
    double cosTheta = std::fmin(dot(-uv, n), 1.0);
    Vec3 rOutPerp = etaiOverEtat * (uv + cosTheta * n);
    Vec3 rOutParallel = -std::sqrt(std::fabs(1.0 - rOutPerp.lengthSquared())) * n;
    return rOutPerp + rOutParallel;
}

inline std::ostream& operator<<(std::ostream& out, const Vec3& v) {
    return out << v.x << ' ' << v.y << ' ' << v.z;
}

using Point3 = Vec3;
using Color = Vec3;

}  // namespace rt

#endif  // RAYTRACER_VEC3_HPP
