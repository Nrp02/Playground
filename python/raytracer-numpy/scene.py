from __future__ import annotations

from dataclasses import dataclass, field

import numpy as np

import vec3
from ray import Rays

DIFFUSE = "diffuse"
METAL = "metal"


@dataclass
class Material:
    albedo: np.ndarray
    kind: str = DIFFUSE
    fuzz: float = 0.0

    def __post_init__(self):
        self.albedo = np.asarray(self.albedo, dtype=np.float64)


@dataclass
class Sphere:
    center: np.ndarray
    radius: float
    material: Material

    def __post_init__(self):
        self.center = np.asarray(self.center, dtype=np.float64)

    def intersect(self, rays: Rays, t_min: float, t_max: float):
        n = len(rays)
        oc = rays.origin - self.center
        a = vec3.dot(rays.direction, rays.direction)
        half_b = vec3.dot(oc, rays.direction)
        c = vec3.dot(oc, oc) - self.radius * self.radius
        discriminant = half_b * half_b - a * c

        valid = discriminant >= 0.0
        sqrt_d = np.sqrt(np.maximum(discriminant, 0.0))

        root1 = (-half_b - sqrt_d) / a
        root2 = (-half_b + sqrt_d) / a

        root1_ok = valid & (root1 >= t_min) & (root1 <= t_max)
        root2_ok = valid & ~root1_ok & (root2 >= t_min) & (root2 <= t_max)

        t = np.full(n, np.inf)
        t = np.where(root1_ok, root1, t)
        t = np.where(root2_ok, root2, t)
        hit = root1_ok | root2_ok

        return hit, t


@dataclass
class PointLight:
    position: np.ndarray
    color: np.ndarray
    intensity: float = 1.0

    def __post_init__(self):
        self.position = np.asarray(self.position, dtype=np.float64)
        self.color = np.asarray(self.color, dtype=np.float64)


@dataclass
class Scene:
    spheres: list[Sphere] = field(default_factory=list)
    lights: list[PointLight] = field(default_factory=list)
    background_top: np.ndarray = field(default_factory=lambda: np.array([0.5, 0.7, 1.0]))
    background_bottom: np.ndarray = field(default_factory=lambda: np.array([1.0, 1.0, 1.0]))
    ambient: float = 0.1

    def closest_hit(self, rays: Rays, t_min: float, t_max: float):
        n = len(rays)
        best_t = np.full(n, t_max)
        hit_any = np.zeros(n, dtype=bool)
        hit_sphere_idx = np.full(n, -1, dtype=np.int64)

        for idx, sphere in enumerate(self.spheres):
            hit, t = sphere.intersect(rays, t_min, best_t.copy())
            closer = hit & (t < best_t)
            best_t = np.where(closer, t, best_t)
            hit_sphere_idx = np.where(closer, idx, hit_sphere_idx)
            hit_any |= closer

        points = rays.at(best_t)
        normals = np.zeros_like(points)
        for idx, sphere in enumerate(self.spheres):
            mask = hit_sphere_idx == idx
            if np.any(mask):
                normals[mask] = vec3.normalize(points[mask] - sphere.center)

        return hit_any, best_t, points, normals, hit_sphere_idx

    def is_occluded(self, origin: np.ndarray, direction: np.ndarray, t_max: np.ndarray) -> np.ndarray:
        n = origin.shape[0]
        occluded = np.zeros(n, dtype=bool)
        shadow_rays = Rays(origin, direction)
        for sphere in self.spheres:
            hit, t = sphere.intersect(shadow_rays, 1e-3, np.inf)
            occluded |= hit & (t < t_max)
        return occluded

    def sky_color(self, direction: np.ndarray) -> np.ndarray:
        unit = vec3.normalize(direction)
        t = 0.5 * (unit[..., 1] + 1.0)
        t = t[..., np.newaxis]
        return (1.0 - t) * self.background_bottom + t * self.background_top


def build_demo_scene() -> Scene:
    ground = Sphere(center=[0.0, -1000.0, 0.0], radius=1000.0,
                     material=Material(albedo=[0.5, 0.5, 0.5], kind=DIFFUSE))

    s1 = Sphere(center=[0.0, 1.0, 0.0], radius=1.0,
                material=Material(albedo=[0.9, 0.9, 0.9], kind=METAL, fuzz=0.02))
    s2 = Sphere(center=[-2.4, 0.7, 0.5], radius=0.7,
                material=Material(albedo=[0.8, 0.2, 0.2], kind=DIFFUSE))
    s3 = Sphere(center=[2.2, 0.6, -0.3], radius=0.6,
                material=Material(albedo=[0.2, 0.4, 0.9], kind=METAL, fuzz=0.15))
    s4 = Sphere(center=[0.9, 0.4, 1.6], radius=0.4,
                material=Material(albedo=[0.3, 0.8, 0.3], kind=DIFFUSE))

    scene = Scene(spheres=[ground, s1, s2, s3, s4])
    scene.lights.append(PointLight(position=[5.0, 8.0, 4.0], color=[1.0, 1.0, 1.0], intensity=1.3))
    return scene
