---
title: "VetraPath — Monte Carlo Path Tracer"
summary: "A physically-based Monte Carlo path tracer built from scratch in C++17, with an interactive real-time viewport, BVH acceleration, and AI denoising."
tags: ["Graphics", "CV"]
stack: ["C++17", "OpenGL", "Dear ImGui", "Intel OIDN", "CMake"]
order: 1
---

## Overview

VetraPath is a physically-based renderer built from scratch — a Monte Carlo path
tracer with unbiased global illumination and an interactive viewport.

## Rendering & materials

- Unbiased global illumination: Multiple Importance Sampling (power heuristic),
  Next Event Estimation, and Russian Roulette termination
- Full BSDF library: Lambertian, rough metal, dielectric with Fresnel/refraction,
  GGX microfacet, and emissive area lights

## Performance

- Bounding Volume Hierarchy via the Surface Area Heuristic (SAH), cutting
  ray–scene intersection from O(n) to O(log n) on high-poly meshes
- Multi-threaded tile-based renderer (std::thread) with Intel Open Image Denoise
  for production-quality, low-noise frames at reduced sample counts

## Interactive viewport

- Dear ImGui + GLFW + OpenGL viewport with real-time progressive rendering,
  fly-camera, live material/lighting controls, and hot-reloading
- OBJ/MTL mesh loader and tinyxml2 scene serialisation for reproducible renders
