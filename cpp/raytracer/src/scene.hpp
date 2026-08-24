#ifndef RAYTRACER_SCENE_HPP
#define RAYTRACER_SCENE_HPP

#include <memory>
#include <vector>

#include "hittable.hpp"
#include "material.hpp"
#include "ray.hpp"
#include "vec3.hpp"

namespace rt {

// Owns every object and light in the world, and knows how to shade a ray
// against them. Scene is move-only (it holds unique_ptr<Hittable>), which
// is fine: it gets built once by buildDemoScene() and then read concurrently
// by every render thread — rayColor() and hit() are const and touch no
// mutable state, so a single Scene instance can be safely shared (by const
// reference) across threads without any locking.
class Scene {
public:
    Scene() = default;

    void add(std::unique_ptr<Hittable> object) { objects_.push_back(std::move(object)); }
    void addLight(const PointLight& light) { lights_.push_back(light); }

    // Compacts every object added via add() into a BVH (see bvh.hpp) so
    // that hit() runs in roughly O(log n) instead of O(n). Must be called
    // once after all add() calls and before the first hit()/rayColor()
    // call; buildDemoScene() does this before returning. Safe to skip if
    // the scene has zero objects (hit() then always reports a miss).
    void build();

    // Finds the closest intersection (if any) among all objects in the
    // range t in [tMin, tMax]. Requires build() to have been called if any
    // objects were added.
    bool hit(const Ray& r, double tMin, double tMax, HitRecord& rec) const;

    // Traces `r` through the scene and returns the shaded color: direct
    // Blinn-Phong lighting with shadow rays, plus up to `depth` bounces of
    // recursive mirror reflection for reflective materials. Rays that miss
    // everything resolve to a soft sky gradient.
    Color rayColor(const Ray& r, int depth) const;

    size_t objectCount() const { return objectCount_; }
    size_t lightCount() const { return lights_.size(); }

private:
    Color backgroundGradient(const Ray& r) const;
    bool inShadow(const Point3& point, const Vec3& normal, const PointLight& light) const;

    // Handles Material::dielectric surfaces (glass): recurses into a
    // Fresnel-weighted blend of a reflected ray and a refracted ray rather
    // than the Blinn-Phong path the rest of rayColor() uses.
    Color dielectricScatter(const Ray& r, const HitRecord& rec, const Material& mat, int depth) const;

    std::vector<std::unique_ptr<Hittable>> objects_;  // staging area until build() is called
    std::unique_ptr<Hittable> root_;                  // BVH root after build(); null before/if empty
    size_t objectCount_ = 0;
    std::vector<PointLight> lights_;
};

// Builds the fixed demo scene rendered by main.cpp: a checkered ground
// plane (approximated by a very large sphere) and several smaller spheres
// covering the matte / glossy / mirror material presets, lit by a warm key
// light and a cooler, dimmer fill light.
Scene buildDemoScene();

}  // namespace rt

#endif  // RAYTRACER_SCENE_HPP
