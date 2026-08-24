from __future__ import annotations

import numpy as np

import vec3
from ray import Rays
from scene import METAL, Scene


def trace(rays: Rays, scene: Scene, max_depth: int, rng: np.random.Generator) -> np.ndarray:
    n = len(rays)
    color = np.zeros((n, 3))
    attenuation = np.ones((n, 3))
    active = np.ones(n, dtype=bool)
    cur_origin = rays.origin.copy()
    cur_dir = rays.direction.copy()

    for _ in range(max_depth):
        if not active.any():
            break

        idxs_active = np.where(active)[0]
        sub_rays = Rays(cur_origin[idxs_active], cur_dir[idxs_active])
        hit, t, points, normals, sphere_idx = scene.closest_hit(sub_rays, 1e-3, np.inf)

        miss_local = ~hit
        miss_global = idxs_active[miss_local]
        if miss_global.size:
            sky = scene.sky_color(sub_rays.direction[miss_local])
            color[miss_global] += attenuation[miss_global] * sky
            active[miss_global] = False

        hit_local = hit
        hit_global = idxs_active[hit_local]
        if hit_global.size == 0:
            continue

        pts = points[hit_local]
        norms = normals[hit_local]
        sidx = sphere_idx[hit_local]

        n_hit = hit_global.size
        albedo = np.zeros((n_hit, 3))
        kind = np.empty(n_hit, dtype=object)
        fuzz = np.zeros(n_hit)
        for sp_i, sphere in enumerate(scene.spheres):
            m = sidx == sp_i
            if np.any(m):
                albedo[m] = sphere.material.albedo
                kind[m] = sphere.material.kind
                fuzz[m] = sphere.material.fuzz

        direct = np.full((n_hit, 3), scene.ambient)
        for light in scene.lights:
            light_vec = light.position - pts
            light_dist = vec3.length(light_vec)
            light_dir = light_vec / light_dist[:, np.newaxis]
            ndotl = np.clip(vec3.dot(norms, light_dir), 0.0, None)
            shadow_origin = pts + norms * 1e-4
            occluded = scene.is_occluded(shadow_origin, light_dir, light_dist)
            lit = (~occluded).astype(np.float64) * ndotl * light.intensity
            direct += lit[:, np.newaxis] * light.color

        local_color = direct * albedo
        color[hit_global] += attenuation[hit_global] * local_color

        is_metal = kind == METAL

        new_origin = cur_origin.copy()
        new_dir = cur_dir.copy()

        if np.any(is_metal):
            metal_global = hit_global[is_metal]
            incoming = sub_rays.direction[hit_local][is_metal]
            reflected = vec3.reflect(incoming, norms[is_metal])
            jitter = vec3.random_unit_vectors(int(is_metal.sum()), rng)
            fuzzed = vec3.normalize(reflected + fuzz[is_metal][:, np.newaxis] * jitter)
            new_origin[metal_global] = pts[is_metal] + norms[is_metal] * 1e-4
            new_dir[metal_global] = fuzzed
            attenuation[metal_global] *= albedo[is_metal] * 0.6

        terminate_global = hit_global[~is_metal]
        if terminate_global.size:
            active[terminate_global] = False

        cur_origin = new_origin
        cur_dir = new_dir

    remaining = np.where(active)[0]
    if remaining.size:
        color[remaining] += attenuation[remaining] * scene.sky_color(cur_dir[remaining])

    return color


def render(scene: Scene, camera, width: int, height: int, samples_per_pixel: int,
           max_depth: int, seed: int = 42) -> np.ndarray:
    rng = np.random.default_rng(seed)

    px = np.arange(width)
    py = np.arange(height)
    grid_x, grid_y = np.meshgrid(px, py)
    grid_x = grid_x.ravel().astype(np.float64)
    grid_y = grid_y.ravel().astype(np.float64)
    n_pixels = width * height

    accum = np.zeros((n_pixels, 3))

    for _ in range(samples_per_pixel):
        jitter_x = rng.random(n_pixels)
        jitter_y = rng.random(n_pixels)
        s = (grid_x + jitter_x) / (width - 1)
        t = 1.0 - (grid_y + jitter_y) / (height - 1)

        origin, direction = camera.get_rays(s, t)
        rays = Rays(origin, direction)

        sample_color = trace(rays, scene, max_depth, rng)
        accum += sample_color

    avg = accum / samples_per_pixel
    avg = vec3.clamp01(avg)
    gamma_corrected = np.sqrt(avg)

    return gamma_corrected.reshape(height, width, 3)
