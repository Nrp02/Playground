#ifndef RAYTRACER_BVH_HPP
#define RAYTRACER_BVH_HPP

#include <memory>
#include <vector>

#include "hittable.hpp"

namespace rt {

// A bounding volume hierarchy over a set of Hittables. Without this, Scene
// would have to test every ray against every object (O(n) per ray); with
// it, each ray only descends the branches whose bounding box it actually
// enters, which is what makes a scene with hundreds of small spheres (see
// buildDemoScene()) render in a fraction of a second instead of minutes.
//
// BVHNode is itself a Hittable, and its two children are also just
// `unique_ptr<Hittable>` — each one is either another BVHNode (an interior
// node) or a leaf primitive such as a Sphere, with no wrapper type needed
// to tell them apart. hit() and boundingBox() are polymorphic, so the same
// two-line traversal in hit() works uniformly at every level of the tree.
class BVHNode : public Hittable {
public:
    // Consumes `objects` (moving every element out of the vector) and
    // returns the root of a BVH covering all of them. If `objects` has
    // exactly one element, that element is returned directly, unwrapped —
    // a tree of size one would just add an indirection for no benefit.
    // Throws std::invalid_argument if `objects` is empty.
    static std::unique_ptr<Hittable> build(std::vector<std::unique_ptr<Hittable>> objects);

    bool hit(const Ray& r, double tMin, double tMax, HitRecord& rec) const override;
    bool boundingBox(AABB& outBox) const override;

private:
    BVHNode() = default;

    AABB box_;
    std::unique_ptr<Hittable> left_;
    std::unique_ptr<Hittable> right_;
};

}  // namespace rt

#endif  // RAYTRACER_BVH_HPP
