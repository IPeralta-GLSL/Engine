# Threshold Engine

Threshold Engine is a real-time graphics engine and rendering framework **based on [Donut](https://github.com/NVIDIA-RTX/Donut)**, the open-source (MIT) rendering framework originally created by NVIDIA DevTech. Threshold Engine keeps Donut's reusable rendering passes, scene graph and graphics-API abstraction, and extends them with an integrated ImGui level editor, an ECS-based stage/attribute system, and ray-traced sample renderers.

It is a rendering/framework layer, not a full gameplay engine: it provides rendering, scene import, tooling and application scaffolding, while game logic lives in the application.

## Heritage

The underlying Donut framework originated from the VRWorks Multi-Projection SDK and has been used to build NVIDIA's Asteroids demo, the DLSS SDK and the RTXDI SDK. Threshold Engine builds on that foundation; see `LICENSE.txt` and `ThirdPartyLicenses.txt` for the original NVIDIA copyright and license notices, which are retained.

## Requirements

* Windows or Linux (x64 or ARM64)
* CMake 3.31+
* A C++17 compiler (Visual Studio 2022, GCC 11+ or Clang 14+)
* A shader compiler (FXC for DX11, DXC for DX12, DXC-SPIR-V for Vulkan)

## Dependencies

All dependencies are vendored under `thirdparty/`:

* **cgltf** — glTF scene loading
* **JsonCpp** — JSON scene/configuration files
* **stb** — image I/O
* **NVRHI**, **Dear ImGui**, **GLFW** — rendering, UI and windowing (`DONUT_WITH_NVRHI`)
* **tinyexr** — EXR images (`DONUT_WITH_TINYEXR`)
* **EnTT** — ECS for the stage/attribute system

## Structure

Threshold Engine is organized as static libraries:

* `threshold_core` — math, VFS, logging, JSON and utilities (no graphics).
* `threshold_engine` — scene import/maintenance, animations, materials, texture cache, descriptor tables, console variables.
* `threshold_render` — rendering passes: forward/deferred shading, TAA, SSAO, shadow maps, procedural sky, tone mapping, bloom.
* `threshold_app` — application framework: graphics device managers, UI bindings, camera, media file systems.
* `threshold_ecs` — ECS stage/prim/attribute system used by the editor and samples.

Engine/render shaders live in `shaders/` and are built by the `threshold_shaders` target.

## Editor

`Donut-Samples/examples/editor` builds the **Threshold Engine Editor**: a Dear ImGui docking workspace (Stage, Attribute, Content, Console) whose viewport is the engine's own ray-traced renderer (bindless RT + denoised shadows + SSAO + bloom + tone mapping) rendered full-window with the UI overlaid. Supports mouse/keyboard and gamepad navigation, stage open/save, archtype instancing and live attribute editing.

## Examples

Sample projects live in [`Donut-Samples/`](../Donut-Samples/README.md) in this repository, including the editor, bindless ray tracing, deferred shading, meshlets, and more.

## Build

The engine is consumed as part of a larger CMake project (the samples):

```
cmake -S Donut-Samples -B build
cmake --build build -j8
```

Binaries are placed in `Donut-Samples/bin/`.

## Features

### Graphics APIs

Through NVRHI: Vulkan 1.2+, Direct3D 12 (Windows SDK 19041+), Direct3D 11. Device/window creation is handled by `DeviceManager` in `threshold_app`.

### Scene formats

glTF 2.0 import (no morph targets, no KTX2; DDS supported), plus JSON-based [scene layout files](doc/SceneFile.md) that combine multiple models, lights, cameras and animations. Supported glTF extensions: `KHR_materials_pbrSpecularGlossiness`, `KHR_materials_transmission`, `KHR_lights_punctual`, `MSFT_texture_dds`.

### Render passes

Forward shading, G-buffer fill, deferred shading, temporal AA, adaptive tone mapping, SSAO, procedural sky, bloom. See [include/donut/render](include/donut/render).

### Ray tracing

The framework provides a bindless resource table populated with textures and geometry buffers at scene load; acceleration structures and RT passes are built on the application side (see the bindless ray-tracing sample and the editor viewport).

## License

Threshold Engine is licensed under the [MIT License](LICENSE.txt). Portions derived from Donut retain NVIDIA's copyright notice.
