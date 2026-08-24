#ifndef RAYTRACER_AABB_HPP
#define RAYTRACER_AABB_HPP

#include <algorithm>
#include <cmath>

#include "ray.hpp"
#include "vec3.hpp"

namespace rt {

// Axis-aligned bounding box, used exclusively to accelerate ray traversal
// via the BVH in bvh.hpp. hit() implements the standard "slab method"
// (Kay & Kajiya): a box is the intersection of three axis-aligned slabs,
// so we intersect the ray against each pair of parallel planes and shrink
// the running [tMin, tMax] interval; if it ever becomes empty, the ray
// misses the box.
class AABB {
public:
    AABB() = default;
    AABB(const Point3& a, const Point3& b) : min_(a), max_(b) {}

    const Point3& min() const { return min_; }
    const Point3& max() const { return max_; }

    bool hit(const Ray& r, double tMin, double tMax) const {
        for (int axis = 0; axis < 3; ++axis) {
            double origin = component(r.origin(), axis);
            double dir = component(r.direction(), axis);
            double invD = 1.0 / dir;

            double t0 = (component(min_, axis) - origin) * invD;
            double t1 = (component(max_, axis) - origin) * invD;
            if (invD < 0.0) {
                std::swap(t0, t1);
            }

            tMin = t0 > tMin ? t0 : tMin;
            tMax = t1 < tMax ? t1 : tMax;
            if (tMax <= tMin) {
                return false;
            }
        }
        return true;
    }

    static double component(const Vec3& v, int axis) {
        return axis == 0 ? v.x : (axis == 1 ? v.y : v.z);
    }

private:
    Point3 min_;
    Point3 max_;
};

// The smallest box containing both `a` and `b`. Used when building a BVH
// node's box from its two children.
inline AABB surroundingBox(const AABB& a, const AABB& b) {
    Point3 small(std::min(a.min().x, b.min().x), std::min(a.min().y, b.min().y),
                 std::min(a.min().z, b.min().z));
    Point3 big(std::max(a.max().x, b.max().x), std::max(a.max().y, b.max().y),
               std::max(a.max().z, b.max().z));
    return AABB(small, big);
}

}  // namespace rt

#endif  // RAYTRACER_AABB_HPP
