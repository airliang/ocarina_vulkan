# Ocarina Vulkan

## 1. About This Project

**Ocarina Vulkan** is a personal graphics engine and learning sandbox built on the Vulkan API. It is not a production game engine; it is a place to experiment with rendering techniques, API usage, and engine structure in a controlled codebase.

The project exists for two main goals:

- **Vulkan graphics API learning** — swapchain and offscreen passes, dynamic rendering (Vulkan 1.3 with classic render-pass fallback), descriptor sets, pipeline state, texture upload, and glTF loading.
- **Multithreaded, modern rendering architecture** — job scheduling with enkiTS, a dedicated render thread for GPU submission, async pipeline (PSO) compilation, parallel frustum culling, and staged resource loading without blocking the main SDL event loop.

Sample applications under `src/tests/` exercise triangle rendering, offscreen targets, bindless textures, culling, and glTF scenes.

---

## 2. Modules

The engine is split into layers. Applications and tests sit on top of **ocarina-framework**; framework code talks to **ocarina-rhi**; the Vulkan backend implements RHI on top of **ocarina-core**.

```mermaid
flowchart TB
    App["Tests / Application"]
    FW["ocarina-framework"]
    RHI["ocarina-rhi"]
    VK["ocarina-backend-vulkan"]
    Core["ocarina-core"]
    Ext["Third-party (ext/)"]

    App --> FW
    FW --> RHI
    RHI --> Core
    FW --> Core
    VK --> RHI
    VK --> Core
    Core --> Ext
    FW --> Ext
```

### ocarina-core

Foundation library shared across the project: STL wrappers, math types, logging, hashing, threading helpers, image utilities, and other engine-agnostic utilities. It has no knowledge of Vulkan or rendering.

### ocarina-rhi

Render Hardware Interface — a backend-agnostic graphics layer. It defines `Device`, `Texture`, buffers, `RHIRenderPass`, `CommandBuffer`, pipeline state, and descriptor abstractions. Application and framework code depend on RHI types, not on Vulkan handles directly.

### ocarina-backend-vulkan

Vulkan implementation of the RHI, loaded as a backend module. It owns instance/device/swapchain setup, command buffers, pipelines, dynamic rendering vs. classic render passes, shader compilation (via DXC/SPIRV-Cross), and resource creation. **ocarina-rhi** dispatches into this backend at runtime.

### ocarina-framework

High-level rendering and scene layer: `Renderer`, ECS (`EntityComponentSystem`), scene/camera/primitives, `PipelineManager` and async `PipelineCompileTask`, `GlobalGPUStorage`, `FrameResources`, glTF/async loaders, ImGui integration, and pass-group recording (`PassGroupId` → `RenderPassTask`). This is where multithread scheduling and the per-frame render loop live.

### Third-party references (under `src/ext/`)

| Library | Role |
|---------|------|
| **Vulkan SDK** | Graphics API |
| **SDL3** | Window and input |
| **enkiTS** | Job scheduler / thread pool |
| **Dear ImGui** | Debug and loading UI |
| **EASTL / mimalloc** | Containers and allocation |
| **spdlog** | Logging |
| **SPIRV-Cross** | Shader reflection and cross-compile |
| **tinygltf / tinyexr / stb** | Asset and image I/O |

---

## 3. Multithread Rendering Architecture

Scheduling uses **enkiTS** with `hardware_concurrency()` worker threads. Thread **0** is the thread that constructs `Renderer` (typically the main thread).

### Thread roles

| Thread | Responsibility |
|--------|----------------|
| **Main (0)** | Application setup, `Renderer::run()`, waits on async load, SDL event loop after load |
| **Render (1, pinned)** | `RenderTask` — per-frame update, **dispatch culling job** (`RendererPrimitiveCullTask` via enkiTS) and wait, dispatch recording jobs, **swapchain acquire** (`begin_frame`), **graphics queue submit** (`execute_command_buffers`), **present** (`end_frame`); `LoadingImguiTask` during load |
| **Cmd record (workers)** | `RenderPassTask` — command buffer recording (`begin` / render passes / draw / `end`) per `PassGroupId`; dispatched from the render thread, executed on an enkiTS worker (`m_SetSize = 1` per group) |
| **Workers (2…N−1)** | `AsyncLoader`, parallel frustum cull (`RendererPrimitiveCullTask`), async `PipelineCompileTask` (PSO creation) |

### How work is dispatched

```mermaid
sequenceDiagram
    participant Main as Main thread
    participant Render as Render thread
    participant Worker as Worker threads

    Note over Main,Worker: Startup / load
    Main->>Worker: AsyncLoader (shaders, meshes, materials)
    Main->>Render: LoadingImguiTask (loading UI)
    Main->>Main: Wait for load, finalize GPU buffers
    Main->>Render: Start RenderTask

    Note over Main,Worker: Steady state (each frame)
    Render->>Render: Acquire swapchain image (begin_frame)
    Render->>Render: Cull, update components
  loop Each PassGroupId in order
        Render->>Worker: RenderPassTask (record command buffer)
        Render->>Render: Wait, queue submit
    end
    Render->>Render: Present swapchain (end_frame)
    Main->>Main: SDL events (parallel)
    Worker->>Worker: PSO compile / cull (overlap with GPU)
```

**Main thread** kicks off loading and then runs the window loop. It does not record draw commands after the render thread starts.

**Render thread** owns the frame loop: camera update, **dispatch of the parallel frustum culling job** (`RendererPrimitiveCullTask` onto worker threads, then wait), queue population, and orchestration of **RenderPassTask** instances (grouped by `PassGroupId` — e.g. Offscreen, GBuffer, UI). Each non-empty group records on a worker; the render thread waits, submits recorded command buffers to the graphics queue, and presents the swapchain image when the frame ends.

**Worker threads** handle CPU-heavy work that should not block presentation: asset loading, SIMD frustum culling, pipeline creation, and **command buffer recording** when `RenderPassTask` runs. The render thread dispatches one recording task per non-empty pass group, waits for completion, then submits the recorded buffers. Missing PSOs are skipped for the current frame rather than stalling the render thread.

---

## 4. Building, glTF Scenes, and Examples

### Build

**Requirements:** CMake 3.x, Visual Studio 2022 (or compatible C++20 toolchain), Vulkan SDK.

```bash
cmake -B build -G "Visual Studio 17 2022"
cmake --build build --config Debug --target test-asyncLoadGLTF
```

Binaries are written to `bin/Debug/` (or `Release`). Run from the repository root so relative paths to `res/` resolve correctly.

### Tracy profiler (optional)

[Tracy](https://github.com/wolfpld/tracy) is integrated as a git submodule at `src/ext/tracy` (v0.11.1).

```bash
git submodule update --init src/ext/tracy
cmake -B build -DOCARINA_ENABLE_TRACY=ON
cmake --build build --config Debug --target test-vulkan-triangle
```

Run the test, then connect with the [Tracy desktop app](https://github.com/wolfpld/tracy/releases). Instrument code with `core/profiler.h` macros (`OC_PROFILE_FUNCTION`, `OC_PROFILE_SCOPE_N`, `OC_PROFILE_FRAME_MARK`). See `src/ext/tracy/INTEGRATION.md` for manual setup without a submodule.

### glTF scene resources in this repo

Sample scenes live under `res/`. Each scene is a folder with a `glTF/` subfolder containing the `.gltf` file, a `.bin` buffer, and texture images referenced by URI.

| Scene | Path | Notes |
|-------|------|-------|
| **Sponza** | `res/Sponza/glTF/Sponza.gltf` | Large interior; many `.jpg` / `.png` textures + `Sponza.bin` |
| **Flight Helmet** | `res/FlightHelmet/glTF/FlightHelmet.gltf` | Smaller PBR asset; textures + `FlightHelmet.bin` |

PBR mesh shading for glTF uses the built-in shaders:

- `res/shaderlibrary/builtin/mesh.vert`
- `res/shaderlibrary/builtin/mesh.frag`

Parsing is done with **tinygltf** (`src/ext/tinygltf/`). Geometry is staged into `GlobalGPUStorage`; textures and materials are created on the loader worker thread.

### How glTF loading works

`GltfAsyncLoader` extends `AsyncLoader` and runs on an enkiTS **worker thread** when you call `renderer.run()`:

1. **Compile pipelines** — resolve shaders, cache pipeline layouts, kick off async PSO creation for the swapchain pass.
2. **Parse glTF** — load `.gltf` / `.glb` via tinygltf; resolve external images relative to the glTF file’s directory.
3. **Build scene** — walk the node graph, append mesh geometry, create `Material`s and `Texture`s, populate a `Scene` of `Primitive`s.
4. **Complete callback** (main thread, after load) — wire push constants, assign `renderer.set_scene()`, then `GlobalGPUStorage::finalize()` uploads mega vertex/index buffers before the render loop starts.

While loading, the **render thread** shows a loading UI (`LoadingImguiTask`); the **main thread** can continue processing SDL events.

### glTF loading example

Based on `src/tests/test_load_gltf.cpp`:

```cpp
#include "framework/gltf_async_loader.h"
#include "framework/pipeline_compile_task.h"
#include "framework/pass_group_id.h"
// ...

const fs::path repo_root = /* path to project root */;
const fs::path gltf_path = repo_root / "res/Sponza/glTF/Sponza.gltf";

std::vector<PipelineCompileTask::Entry> pipeline_entries;
pipeline_entries.push_back(PipelineCompileTask::Entry::make_graphics(
    fs::absolute(repo_root / "res/shaderlibrary/builtin/mesh.vert").string(),
    fs::absolute(repo_root / "res/shaderlibrary/builtin/mesh.frag").string()));

GltfAsyncLoader gltf_loader(
    &renderer.task_scheduler(),
    &device,
    &pipeline_entries,
    fs::absolute(gltf_path).string());

RHIRenderPass* render_pass = device.create_render_pass(swapchain_pass_creation);
renderer.pass_group(PassGroupId::UI).add_render_pass(render_pass);

renderer.set_async_loader(&gltf_loader, nullptr, [&]() {
    Scene& scene = gltf_loader.get_scene();
    for (uint32_t i = 0; i < scene.primitive_count(); ++i) {
        Primitive& primitive = scene.primitive(i);
        primitive.set_update_push_constant_function(update_push_constant);
    }
    renderer.set_scene(&scene);
});

renderer.run();
window->run([](double) {});
```

To switch scenes, point `gltf_path` at `res/FlightHelmet/glTF/FlightHelmet.gltf` instead. Optional `LoadingProgressListener` can be passed to `renderer.set_loading_progress_listener()` for per-primitive progress in the loading UI.

### Other test targets

| Target | Description |
|--------|-------------|
| `test-vulkan-triangle` | Minimal triangle + swapchain |
| `test-vulkan-offscreen` | Offscreen RT → swapchain blit (`PassGroupId::Offscreen` then `UI`) |
| `test-vulkan-texture` | Textured quad |
| `test-vulkan-bindless` | Bindless sampling |
| `test-culling` | Parallel frustum culling |
| `test-asyncLoadGLTF` | Full glTF load (Sponza or Flight Helmet) |

Pass group registration example:

```cpp
renderer.pass_group(PassGroupId::Offscreen).add_render_pass(offscreen_pass);
renderer.pass_group(PassGroupId::UI).add_render_pass(swapchain_pass);
```

A frame graph for automatic pass dependencies and resource barriers is planned as a follow-up; the current design uses explicit pass groups and ordered recording.
