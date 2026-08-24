#ifndef RAYTRACER_RAY_HPP
#define RAYTRACER_RAY_HPP

#include "vec3.hpp"

namespace rt {

// A parametric ray: P(t) = origin + t * direction.
class Ray {
public:
    Ray() = default;
    Ray(const Point3& origin, const Vec3& direction) : origin_(origin), direction_(direction) {}

    const Point3& origin() const { return origin_; }
    const Vec3& direction() const { return direction_; }

    Point3 at(double t) const { return origin_ + t * direction_; }

private:
    Point3 origin_;
    Vec3 direction_;
};

}  // namespace rt

#endif  // RAYTRACER_RAY_HPP
