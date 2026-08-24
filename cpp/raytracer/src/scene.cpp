#include "scene.hpp"

#include <cmath>
#include <limits>
#include <random>
#include <utility>

#include "bvh.hpp"
#include "sphere.hpp"

namespace rt {

void Scene::build() {
    objectCount_ = objects_.size();
    if (objects_.empty()) {
        root_.reset();
        return;
    }
    root_ = BVHNode::build(std::move(objects_));
    objects_.clear();
}

bool Scene::hit(const Ray& r, double tMin, double tMax, HitRecord& rec) const {
    if (!root_) {
        return false;
    }
    return root_->hit(r, tMin, tMax, rec);
}

Color Scene::backgroundGradient(const Ray& r) const {
    // A soft vertical gradient from white (horizon) to sky blue (zenith),
    // parameterized by the ray's y-direction so it reads as a simple sky
    // dome rather than a flat color when a ray escapes the scene.
    Vec3 unitDirection = unitVector(r.direction());
    double t = 0.5 * (unitDirection.y + 1.0);
    return (1.0 - t) * Color(1.0, 1.0, 1.0) + t * Color(0.5, 0.7, 1.0);
}

bool Scene::inShadow(const Point3& point, const Vec3& normal, const PointLight& light) const {
    // Bias the shadow ray's origin slightly along the surface normal
    // ("shadow acne" avoidance) in addition to using a small tMin, so a
    // surface never incorrectly self-shadows due to floating point error
    // in the intersection point.
    Point3 biasedOrigin = point + normal * 1e-4;
    Vec3 toLight = light.position - biasedOrigin;
    double distanceToLight = toLight.length();
    Vec3 direction = toLight / distanceToLight;

    Ray shadowRay(biasedOrigin, direction);
    HitRecord rec;
    // Stop just short of the light itself so the light "source" point
    // doesn't occlude itself.
    return hit(shadowRay, 1e-3, distanceToLight - 1e-3, rec);
}

Color Scene::rayColor(const Ray& r, int depth) const {
    if (depth <= 0) {
        return Color(0.0, 0.0, 0.0);
    }

    HitRecord rec;
    if (!hit(r, 1e-3, std::numeric_limits<double>::infinity(), rec)) {
        return backgroundGradient(r);
    }

    const Material& mat = *rec.material;

    if (mat.dielectric) {
        return dielectricScatter(r, rec, mat, depth);
    }

    Color albedo = mat.albedo;
    if (mat.checkered) {
        int ix = static_cast<int>(std::floor(rec.point.x * mat.checkerScale));
        int iz = static_cast<int>(std::floor(rec.point.z * mat.checkerScale));
        if ((ix + iz) % 2 != 0) {
            albedo = mat.checkerAlbedo;
        }
    }

    // Ambient term: a small always-on fraction of the surface color, a
    // cheap stand-in for the indirect/global illumination a real path
    // tracer would compute so shadowed regions aren't pure black.
    Color color = mat.ambient * albedo;

    Vec3 viewDir = unitVector(-r.direction());

    for (const PointLight& light : lights_) {
        Vec3 toLight = light.position - rec.point;
        double distance = toLight.length();
        Vec3 L = toLight / distance;

        double NdotL = dot(rec.normal, L);
        if (NdotL <= 0.0) {
            continue;  // surface faces away from this light entirely
        }
        if (inShadow(rec.point, rec.normal, light)) {
            continue;  // occluded: only the ambient term reaches this point
        }

        // Falloff chosen to look good at typical scene scales rather than
        // strict physical inverse-square, which would require very large
        // intensity constants for a small hand-placed scene like this one.
        double attenuation = light.intensity / (1.0 + 0.1 * distance + 0.01 * distance * distance);

        // Lambertian diffuse term.
        color += mat.diffuse * albedo * light.color * (NdotL * attenuation);

        // Blinn-Phong specular highlight: uses the half-vector between the
        // view and light directions rather than a true mirror reflection
        // vector, which is cheaper and visually close to Phong specular.
        Vec3 halfVector = unitVector(L + viewDir);
        double NdotH = std::max(0.0, dot(rec.normal, halfVector));
        double specFactor = std::pow(NdotH, mat.shininess);
        color += mat.specular * light.color * (specFactor * attenuation);
    }

    if (mat.reflectivity > 0.0 && depth > 1) {
        Vec3 reflectedDir = reflect(unitVector(r.direction()), rec.normal);
        Ray reflectedRay(rec.point + rec.normal * 1e-4, reflectedDir);
        Color reflectedColor = rayColor(reflectedRay, depth - 1);
        color = (1.0 - mat.reflectivity) * color + mat.reflectivity * reflectedColor;
    }

    return color;
}

Color Scene::dielectricScatter(const Ray& r, const HitRecord& rec, const Material& mat, int depth) const {
    if (depth <= 1) {
        return Color(0.0, 0.0, 0.0);
    }

    // Entering the sphere bends light by 1/ior; exiting it (frontFace is
    // false because HitRecord::setFaceNormal already flipped the normal to
    // face the ray) bends by ior/1, i.e. the reciprocal.
    double refractionRatio = rec.frontFace ? (1.0 / mat.refractiveIndex) : mat.refractiveIndex;

    Vec3 unitDirection = unitVector(r.direction());
    double cosTheta = std::fmin(dot(-unitDirection, rec.normal), 1.0);
    double sinTheta = std::sqrt(std::max(0.0, 1.0 - cosTheta * cosTheta));

    // Schlick's approximation to the Fresnel reflectance: the fraction of
    // light that reflects rather than refracts, which is what makes glass
    // look near-mirror-bright at grazing angles ("edge glow") while mostly
    // transparent head-on.
    double r0 = (1.0 - refractionRatio) / (1.0 + refractionRatio);
    r0 = r0 * r0;
    double reflectance = r0 + (1.0 - r0) * std::pow(1.0 - cosTheta, 5.0);

    Vec3 reflectedDir = reflect(unitDirection, rec.normal);
    Color reflectedColor = rayColor(Ray(rec.point + rec.normal * 1e-4, reflectedDir), depth - 1);

    // Beyond the critical angle, Snell's law has no real solution: all the
    // light reflects (total internal reflection), so there is no refracted
    // ray to blend in.
    bool totalInternalReflection = refractionRatio * sinTheta > 1.0;
    if (totalInternalReflection) {
        return reflectedColor;
    }

    Vec3 refractedDir = refract(unitDirection, rec.normal, refractionRatio);
    Color refractedColor = rayColor(Ray(rec.point - rec.normal * 1e-4, refractedDir), depth - 1);

    // A simple ray tracer has no per-pixel sample budget dedicated to
    // Russian-roulette-style stochastic reflect/refract selection the way
    // a path tracer would, so instead we deterministically blend both
    // outcomes by the Fresnel weight. This stays noise-free at the cost of
    // doubling the ray count at each glass bounce, which is a fine trade
    // at this project's modest max recursion depth.
    return reflectance * reflectedColor + (1.0 - reflectance) * refractedColor;
}

Scene buildDemoScene() {
    Scene scene;

    // Ground: a very large sphere whose visible cap reads as a flat,
    // checkered plane under the other objects.
    Material ground;
    ground.albedo = Color(0.85, 0.85, 0.85);
    ground.checkered = true;
    ground.checkerAlbedo = Color(0.12, 0.14, 0.16);
    ground.checkerScale = 1.0;
    ground.ambient = 0.12;
    ground.diffuse = 0.85;
    ground.specular = 0.05;
    ground.shininess = 8.0;
    scene.add(std::make_unique<Sphere>(Point3(0.0, -1000.0, 0.0), 1000.0, ground));

    // Centerpiece: a large mirror-like sphere.
    scene.add(std::make_unique<Sphere>(Point3(0.0, 1.0, 0.0), 1.0,
                                        Material::mirror(Color(0.92, 0.92, 0.95), 0.85)));

    // Matte red sphere.
    scene.add(std::make_unique<Sphere>(Point3(-2.4, 0.7, 0.3), 0.7, Material::matte(Color(0.75, 0.15, 0.15))));

    // Glossy blue sphere with a tight specular highlight.
    scene.add(std::make_unique<Sphere>(Point3(2.2, 0.6, -0.2), 0.6,
                                        Material::glossy(Color(0.15, 0.25, 0.7), 64.0, 0.7)));

    // Small glossy yellow-gold sphere.
    scene.add(std::make_unique<Sphere>(Point3(0.9, 0.35, 1.4), 0.35,
                                        Material::glossy(Color(0.8, 0.65, 0.15), 24.0, 0.4)));

    // Small copper-toned mirror sphere tucked in front.
    scene.add(std::make_unique<Sphere>(Point3(-1.0, 0.3, 1.6), 0.3, Material::mirror(Color(0.8, 0.55, 0.35), 0.7)));

    // A dark, low-reflectivity glossy green sphere for material variety.
    scene.add(std::make_unique<Sphere>(Point3(-3.6, 0.45, -1.2), 0.45,
                                        Material::glossy(Color(0.15, 0.55, 0.25), 48.0, 0.55)));

    // A glass sphere in front of the mirror centerpiece: refracts and
    // (at grazing angles) reflects, per Scene::dielectricScatter.
    scene.add(std::make_unique<Sphere>(Point3(1.6, 0.5, 2.0), 0.5, Material::glass(1.5)));

    // Centers/radii of the showcase spheres above, so the procedural field
    // below can avoid spawning something that overlaps one of them.
    const std::vector<std::pair<Point3, double>> showcase = {
        {Point3(0.0, 1.0, 0.0), 1.0},   {Point3(-2.4, 0.7, 0.3), 0.7}, {Point3(2.2, 0.6, -0.2), 0.6},
        {Point3(0.9, 0.35, 1.4), 0.35}, {Point3(-1.0, 0.3, 1.6), 0.3}, {Point3(-3.6, 0.45, -1.2), 0.45},
        {Point3(1.6, 0.5, 2.0), 0.5},
    };

    // A scattered field of small spheres around the showcase group, in the
    // style popularized by "Ray Tracing in One Weekend": enough objects
    // (a few hundred) that the BVH's O(log n) traversal meaningfully beats
    // the O(n) linear scan a flat object list would need. Positions are
    // jittered on a grid and materials are randomly rolled from the three
    // presets; the RNG uses a fixed seed so the demo scene — and therefore
    // output.ppm — is reproducible between runs.
    std::mt19937 rng(20240817u);
    std::uniform_real_distribution<double> unit(0.0, 1.0);

    constexpr int kGridRadius = 7;
    for (int i = -kGridRadius; i <= kGridRadius; ++i) {
        for (int j = -kGridRadius; j <= kGridRadius; ++j) {
            double radius = 0.14 + 0.07 * unit(rng);
            Point3 center(i + 0.7 * (unit(rng) - 0.5), radius, j + 0.7 * (unit(rng) - 0.5));

            bool overlaps = false;
            for (const auto& [c, r] : showcase) {
                if ((center - c).length() < r + radius + 0.2) {
                    overlaps = true;
                    break;
                }
            }
            if (overlaps) {
                continue;
            }

            double roll = unit(rng);
            Material mat;
            if (roll < 0.55) {
                mat = Material::matte(Color(unit(rng) * unit(rng), unit(rng) * unit(rng), unit(rng) * unit(rng)));
            } else if (roll < 0.85) {
                Color tint(0.5 + 0.5 * unit(rng), 0.5 + 0.5 * unit(rng), 0.5 + 0.5 * unit(rng));
                mat = Material::glossy(tint, 16.0 + 80.0 * unit(rng), 0.3 + 0.4 * unit(rng));
            } else {
                Color tint(0.6 + 0.4 * unit(rng), 0.6 + 0.4 * unit(rng), 0.6 + 0.4 * unit(rng));
                mat = Material::mirror(tint, 0.55 + 0.3 * unit(rng));
            }

            scene.add(std::make_unique<Sphere>(center, radius, mat));
        }
    }

    // Warm key light, upper-right and slightly in front.
    PointLight key;
    key.position = Point3(6.0, 9.0, 4.0);
    key.color = Color(1.0, 0.96, 0.9);
    key.intensity = 3.5;
    scene.addLight(key);

    // Cooler, dimmer fill light from the opposite side so shadowed faces
    // aren't pure black even outside the ambient term's reach.
    PointLight fill;
    fill.position = Point3(-6.0, 5.0, -4.0);
    fill.color = Color(0.6, 0.7, 1.0);
    fill.intensity = 1.2;
    scene.addLight(fill);

    scene.build();
    return scene;
}

}  // namespace rt
