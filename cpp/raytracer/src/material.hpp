#ifndef RAYTRACER_MATERIAL_HPP
#define RAYTRACER_MATERIAL_HPP

#include "vec3.hpp"

namespace rt {

// Surface properties used by the Blinn-Phong local illumination model in
// Scene::rayColor(). This is a classic (non-physically-based) shading
// model: ambient/diffuse/specular are independent coefficients rather than
// a physically normalized BRDF, which keeps the math simple while still
// producing recognizably different "matte plastic" vs "shiny metal" looks.
struct Material {
    Color albedo{0.8, 0.8, 0.8};  // base surface color

    double ambient = 0.05;    // fraction of albedo always visible (fake global illumination)
    double diffuse = 0.9;     // Lambertian (N.L) response strength
    double specular = 0.3;    // Blinn-Phong highlight strength
    double shininess = 32.0;  // Phong exponent; higher = tighter/smaller highlight

    double reflectivity = 0.0;  // [0,1], blends in a recursive mirror bounce

    // When true, the surface is treated as a glass-like dielectric instead
    // of an opaque Blinn-Phong surface: Scene::rayColor() skips the
    // ambient/diffuse/specular terms entirely and instead recurses into a
    // Fresnel-weighted blend of a reflected and a refracted ray (see
    // Scene::dielectricScatter). None of the fields above apply when this
    // is set.
    bool dielectric = false;
    double refractiveIndex = 1.5;  // ~1.5 for common glass, ~1.33 for water

    // Optional procedural checker pattern: when enabled, albedo alternates
    // between `albedo` and `checkerAlbedo` based on the floor of world-space
    // coordinates, scaled by `checkerScale`. Used for the ground plane so
    // the render has spatial reference points instead of a flat color.
    bool checkered = false;
    Color checkerAlbedo{0.1, 0.1, 0.1};
    double checkerScale = 1.0;

    static Material matte(const Color& color, double amb = 0.08, double diff = 0.9) {
        Material m;
        m.albedo = color;
        m.ambient = amb;
        m.diffuse = diff;
        m.specular = 0.1;
        m.shininess = 8.0;
        m.reflectivity = 0.0;
        return m;
    }

    static Material glossy(const Color& color, double shininess, double specStrength = 0.6) {
        Material m;
        m.albedo = color;
        m.ambient = 0.05;
        m.diffuse = 0.7;
        m.specular = specStrength;
        m.shininess = shininess;
        m.reflectivity = 0.0;
        return m;
    }

    static Material mirror(const Color& color, double reflect = 0.85) {
        Material m;
        m.albedo = color;
        m.ambient = 0.02;
        m.diffuse = 0.25;
        m.specular = 0.9;
        m.shininess = 120.0;
        m.reflectivity = reflect;
        return m;
    }

    static Material glass(double indexOfRefraction = 1.5) {
        Material m;
        m.albedo = Color(1.0, 1.0, 1.0);
        m.ambient = 0.0;
        m.diffuse = 0.0;
        m.specular = 0.0;
        m.reflectivity = 0.0;
        m.dielectric = true;
        m.refractiveIndex = indexOfRefraction;
        return m;
    }
};

// A point light: emits equally in all directions from `position`. Falloff
// is handled with a simple inverse-quadratic-ish attenuation in scene.cpp
// (see Scene::rayColor) rather than true inverse-square, so scenes stay
// nicely lit without needing enormous intensity values.
struct PointLight {
    Point3 position;
    Color color{1.0, 1.0, 1.0};
    double intensity = 1.0;
};

}  // namespace rt

#endif  // RAYTRACER_MATERIAL_HPP
