#ifndef RAYTRACER_HITTABLE_HPP
#define RAYTRACER_HITTABLE_HPP

#include "aabb.hpp"
#include "material.hpp"
#include "ray.hpp"
#include "vec3.hpp"

namespace rt {

// Filled in by a Hittable's hit() when a ray intersects it. Kept as a
// plain struct (rather than returning it by value from every call site
// wrapped in std::optional) since hit() is on the hot path and this avoids
// an extra copy/branch per candidate object.
struct HitRecord {
    Point3 point;
    Vec3 normal;  // always faces against the incoming ray (see setFaceNormal)
    double t = 0.0;
    bool frontFace = true;
    const Material* material = nullptr;

    // Given the outward geometric normal, records whether the ray hit the
    // front or back face and flips the stored normal so it always points
    // opposite the ray direction. Shading code can then always assume
    // `normal` points toward the viewer.
    void setFaceNormal(const Ray& r, const Vec3& outwardNormal) {
        frontFace = dot(r.direction(), outwardNormal) < 0.0;
        normal = frontFace ? outwardNormal : -outwardNormal;
    }
};

// Base interface for anything a ray can intersect. Only Sphere implements
// this today, but keeping the interface abstract (rather than hardcoding
// Scene to a vector<Sphere>) means new primitives can be dropped into
// Scene's object list without touching the traversal or shading code.
class Hittable {
public:
    virtual ~Hittable() = default;

    // Returns true if the ray hits this object with parameter t in
    // [tMin, tMax], filling `rec` with the closest such intersection.
    virtual bool hit(const Ray& r, double tMin, double tMax, HitRecord& rec) const = 0;

    // Fills `outBox` with a bounding box tight enough for BVH construction
    // (see bvh.hpp) and returns true. Returning false would mean "this
    // object has no finite bounds"; every primitive we support is bounded,
    // so in practice every override returns true.
    virtual bool boundingBox(AABB& outBox) const = 0;
};

}  // namespace rt

#endif  // RAYTRACER_HITTABLE_HPP
