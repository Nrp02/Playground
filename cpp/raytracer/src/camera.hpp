#ifndef RAYTRACER_CAMERA_HPP
#define RAYTRACER_CAMERA_HPP

#include <cmath>

#include "ray.hpp"
#include "vec3.hpp"

namespace rt {

// A simple pinhole camera defined by an eye point, a look-at target, an
// "up" hint, a vertical field of view, and the output image's aspect
// ratio. Given normalized viewport coordinates (s, t) in [0,1], getRay()
// produces the ray from the eye through that point on the image plane.
class Camera {
public:
    Camera(const Point3& lookFrom, const Point3& lookAt, const Vec3& vup,
           double verticalFovDegrees, double aspectRatio) {
        double theta = verticalFovDegrees * M_PI / 180.0;
        double h = std::tan(theta / 2.0);
        double viewportHeight = 2.0 * h;
        double viewportWidth = aspectRatio * viewportHeight;

        // Orthonormal basis for the camera: w points from the target back
        // to the eye (camera looks down -w), u is the camera's "right",
        // v is the camera's "up" after correcting for the vup hint.
        w_ = unitVector(lookFrom - lookAt);
        u_ = unitVector(cross(vup, w_));
        v_ = cross(w_, u_);

        origin_ = lookFrom;
        horizontal_ = viewportWidth * u_;
        vertical_ = viewportHeight * v_;
        lowerLeftCorner_ = origin_ - horizontal_ / 2.0 - vertical_ / 2.0 - w_;
    }

    Ray getRay(double s, double t) const {
        Vec3 direction = lowerLeftCorner_ + s * horizontal_ + t * vertical_ - origin_;
        return Ray(origin_, direction);
    }

private:
    Point3 origin_;
    Point3 lowerLeftCorner_;
    Vec3 horizontal_;
    Vec3 vertical_;
    Vec3 u_, v_, w_;
};

}  // namespace rt

#endif  // RAYTRACER_CAMERA_HPP
