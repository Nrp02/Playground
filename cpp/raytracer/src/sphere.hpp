#ifndef RAYTRACER_SPHERE_HPP
#define RAYTRACER_SPHERE_HPP

#include "hittable.hpp"
#include "material.hpp"

namespace rt {

class Sphere : public Hittable {
public:
    Sphere(const Point3& center, double radius, const Material& material)
        : center_(center), radius_(radius), material_(material) {}

    bool hit(const Ray& r, double tMin, double tMax, HitRecord& rec) const override {
        // Ray-sphere intersection: solve |P(t) - C|^2 = r^2 for t, where
        // P(t) = origin + t*dir. Expanding gives a quadratic a*t^2 + b*t + c
        // with a = dir.dir, b = 2*dir.(origin-center), c = (origin-center).(origin-center) - r^2.
        // Using h = b/2 avoids the factor of 2/4 noise in the quadratic
        // formula (a common, purely algebraic simplification).
        Vec3 oc = r.origin() - center_;
        double a = r.direction().lengthSquared();
        double halfB = dot(oc, r.direction());
        double c = oc.lengthSquared() - radius_ * radius_;

        double discriminant = halfB * halfB - a * c;
        if (discriminant < 0.0) {
            return false;
        }
        double sqrtD = std::sqrt(discriminant);

        // Prefer the nearer root, but reject it if it falls outside the
        // valid [tMin, tMax] range and fall back to the farther root.
        double root = (-halfB - sqrtD) / a;
        if (root < tMin || root > tMax) {
            root = (-halfB + sqrtD) / a;
            if (root < tMin || root > tMax) {
                return false;
            }
        }

        rec.t = root;
        rec.point = r.at(root);
        Vec3 outwardNormal = (rec.point - center_) / radius_;
        rec.setFaceNormal(r, outwardNormal);
        rec.material = &material_;
        return true;
    }

    bool boundingBox(AABB& outBox) const override {
        Vec3 extent(radius_, radius_, radius_);
        outBox = AABB(center_ - extent, center_ + extent);
        return true;
    }

    const Point3& center() const { return center_; }
    double radius() const { return radius_; }

private:
    Point3 center_;
    double radius_;
    Material material_;
};

}  // namespace rt

#endif  // RAYTRACER_SPHERE_HPP
