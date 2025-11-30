# Project CSI (Raytracer)
FYP: Raytracing engine as backend

Primary remote `origin` now points to `Project-CSI`.

[![CI](https://github.com/fyp/raytracer/actions/workflows/ci.yml/badge.svg?branch=main)](https://github.com/fyp/raytracer/actions/workflows/ci.yml)
[![ASAN](https://github.com/fyp/raytracer/actions/workflows/ci.yml/badge.svg?query=workflow%3Aasan)](https://github.com/fyp/raytracer/actions/workflows/ci.yml)
[![Release](https://github.com/fyp/raytracer/actions/workflows/release.yml/badge.svg?branch=main)](https://github.com/fyp/raytracer/actions/workflows/release.yml)

## Build & Run

This project uses CMake. A `CMakePresets.json` file is provided to make configuring consistent.

Quick local build (from project root):

```bash
cmake --preset default
cmake --build build -- -j2
./build/Raytracer
```

Notes:
- The default preset sets `REQUIRE_MESHES=OFF`, so missing optional `.obj` assets will not stop configure.
- If you want configure to fail when mesh assets are missing (useful for CI / reproducible builds), run:

```bash
cmake --preset require-meshes
```

That preset sets `REQUIRE_MESHES=ON` and will fail configure if any required mesh is missing.

## VS Code integration

I added `.vscode/tasks.json` and `.vscode/launch.json` to make it easy to build and run from VS Code.

- Use the CMake Tools extension to pick the `default` preset and run Configure/Build from the status bar.
- Or run the `CMake: Configure (preset)` and `CMake: Build` tasks from the Command Palette.
- The `Run Raytracer` launch configuration will build the project and launch the `Raytracer` executable.

## Continuous Integration

A sample GitHub Actions workflow is included at `.github/workflows/ci.yml`. The CI uses the strict preset (`require-meshes`) so PRs will fail if required assets are not present. This helps catch missing assets early.

If you'd like the CI to instead provision placeholder assets or download them from a CDN, I can add that step.

## CI / CD

This repository includes GitHub Actions workflows under `.github/workflows/`:

- `ci.yml` (runs on push & pull_request): builds the project, runs it under LLDB (captures run.log), and uploads the log as an artifact. It also includes an `asan` job that builds with AddressSanitizer and uploads the ASAN run log.
- `release.yml` (runs on tag push `v*`): builds a Release, installs into `build/staging`, zips the staging directory and creates a GitHub Release with the zip attached.

How to trigger a release:

1. Create a signed or annotated tag locally, for example:

```bash
git tag -a v1.0.0 -m "Release v1.0.0"
git push origin v1.0.0
```

2. GitHub Actions will run `release.yml`, build a Release artifact and publish a GitHub Release with the zip file.

If you want stricter CI (e.g., fail when meshes are missing), configure the `require-meshes` preset or update `ci.yml` accordingly.

## System dependencies

This project now prefers a system-installed GLM (OpenGL Mathematics) package.

On Ubuntu/Debian (CI):

```bash
sudo apt-get update
sudo apt-get install -y libglm-dev
```

On macOS (Homebrew):

```bash
brew install glm
```

If `cmake` cannot find `glm` during configure it will stop with a clear error instructing you to install the package or set `CMAKE_PREFIX_PATH`/`glm_DIR` to point at an installed glmConfig.cmake.

## Install / staging

You can install the built artifacts and assets into a staging directory inside the build tree with the convenience target:

```bash
cmake --build build --target install-staging
```

This will create `build/staging` containing the installed `bin/` and `share/` layout.
