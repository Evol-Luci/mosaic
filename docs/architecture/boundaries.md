# Mosaic Boundaries

Every external dependency sits behind a formal interface. The tiers that consume
these interfaces never reference the underlying implementation directly. This is
what makes Blender, Box3D, and the render backend replaceable — and what keeps the
[dependency rules](ARCHITECTURE.md#dependency-rules) enforceable.

## 1. The Blender boundary

Blender is a **source-authoring tool**, run as a **separate process**. Mosaic
Studio talks to it over IPC/Python. The engine never links or references Blender.

```cpp
class IAssetAuthoringBackend
{
public:
    virtual ImportResult import_asset(const ImportRequest&) = 0;
    virtual ExportResult export_asset(const ExportRequest&) = 0;
    virtual std::vector<AssetInfo> inspect_scene(const SceneInspectionRequest&) = 0;
    virtual ~IAssetAuthoringBackend() = default;
};
```

Blender is one implementation (`BlenderBridge`). Someone can later add
`MayaBridge`, `HoudiniBridge`, or `Cinema4DBridge` without changing the engine.

**Why a separate process:** crashing Blender must not crash the engine, and
crashing the runtime must not lose the editor. That isolation is worth a little
IPC complexity. It also keeps the engine free of Blender's GPL — only Studio
integrates Blender.

## 2. The physics boundary

Box3D is the initial backend, hidden behind `PhysicsBackend`. The public engine
API exposes only neutral types:

```text
PhysicsWorld
RigidBody
Collider
Shape
Joint
RaycastResult
ContactEvent
```

It never exposes `b3World`, `b3Body`, `b3Shape`, or any other `b3*` type.
Developers using Mosaic never have to know Box3D is underneath — which matters
because Box3D 0.1.0 is alpha and its API is still moving.

**Determinism is a boundary guarantee**, not just a backend feature: the public
physics API preserves fixed-timestep stepping, stable ordering, deterministic
queries, and record/replay. See [design/physics.md](../design/physics.md).

## 3. The render boundary

The renderer is backend-neutral. Higher-level code deals in engine concepts, never
in Vulkan/D3D12/Metal objects:

```text
RenderDevice
RenderGraph
Mesh   Material   Texture   Shader
Camera Light      Pass
```

Backends live under `modules/render_vulkan`, `modules/render_d3d12`,
`modules/render_metal`, `modules/render_webgpu`. Vulkan ships first (it forces the
abstraction to be honest); the interface is backend-neutral from day one. See
[design/rendering.md](../design/rendering.md).

## 4. The scripting boundary

The scripting engine is runtime-neutral. The engine and editor interact only with
the neutral `ScriptEngine` interface (`engine/script/script_engine.hpp`):

```cpp
class ScriptEngine
{
public:
    virtual ScriptResult run_string(const std::string& code) = 0;
    virtual void set_global(const std::string& name, const Value& val) = 0;
    virtual void bind_world(World& world) = 0;
    virtual void bind_commands(CommandStack& stack) = 0;
    virtual ~ScriptEngine() = default;
};
```

The CPython runtime and `pybind11` headers are strictly contained within
`modules/script_python/` (injected via `create_python_script_engine()`). Neither
`engine/` nor `studio/` include Python headers or link directly against `libpython`.
See [design/scripting.md](../design/scripting.md).

## 5. The audio boundary

The audio subsystem is backend-neutral. Higher-level systems and components interact
solely with the `AudioBackend` interface (`engine/audio/audio_backend.hpp`):

```cpp
class AudioBackend
{
public:
    virtual VoiceId play(AssetID clip_id, float gain, float pitch, bool looping,
                         const Vec3& pos, bool spatial) = 0;
    virtual void stop(VoiceId id) = 0;
    virtual void set_gain(VoiceId id, float gain) = 0;
    virtual void set_pitch(VoiceId id, float pitch) = 0;
    virtual void set_position(VoiceId id, const Vec3& pos) = 0;
    virtual bool is_playing(VoiceId id) = 0;
    virtual void set_listener(const Vec3& pos, const Vec3& forward, const Vec3& up) = 0;
    virtual void register_clip(AssetID id, const float* pcm, uint64_t frame_count,
                               uint32_t channels, uint32_t sample_rate) = 0;
    virtual ~AudioBackend() = default;
};
```

The miniaudio implementation lives under `modules/audio_miniaudio/` and contains all
`ma_*` symbols within its translation unit. The engine and asset cooker have zero
dependency on miniaudio. See [design/audio.md](../design/audio.md).

## 6. The UI and input boundary

The UI and input systems are strictly decoupled from operating system and windowing libraries:
- **Input Provider Seam (`engine/ui/pointer.hpp`):** `UiSystem` receives pointer interaction exclusively through an injected `std::function<PointerState()>` provider. Neither `engine/` nor `UiSystem` links against or includes GLFW or platform windowing headers. GLFW pointer sampling is confined entirely to the application shell (`app/main.cpp`); headless test runners and tools supply mock/null providers.
- **Font Cooker Boundary (`app/asset/font_cook.cpp`):** Font file parsing and glyph rasterization (`stb_truetype.h`) are strictly confined to the offline asset cooker. The runtime engine (`engine/ui/font_atlas.hpp`) only consumes the baked, neutral `FontAtlas` memory representation and serialized `MOSAFNT1` asset blobs. See [design/ui.md](../design/ui.md).

## 7. The networking boundary

The networking subsystem enforces strict protocol and transport neutrality:
- **Transport Seam (`engine/net/transport.hpp`):** Higher-level systems (`NetworkSystem`) interact exclusively with the pure-virtual `Transport` interface (`send()`, `poll()`, `disconnect()`). The engine core contains zero includes of `<sys/socket.h>`, `<winsock2.h>`, or third-party networking libraries (e.g. ENet).
- **Deterministic Loopback (`engine/net/loopback_transport.hpp`):** Headless tests and offline CI execute via in-memory FIFO message queues without opening network ports, preventing port collisions, firewall prompts, or non-deterministic packet timings.
- **Socket Implementation Module (`modules/net_enet/`):** Real UDP networking and third-party library bindings are segregated into dedicated backend modules, keeping `engine/` and `core/` completely socket-agnostic. See [design/net.md](../design/net.md).

## Where the Blender bridge code lives

The detailed Blender-side physics bridge from the source plans — zero-copy ctypes
transform buffers, the viewport modal sync loop, compound colliders, joints, local
anchor/axis computation, and headless bake-to-F-curve — is **not** engine code. It
is the **implementation of Mosaic Studio's live-link preview**, sitting above the
engine boundary inside Studio.

This is a deliberate decision (see the design spec, §2.1): the separate-engine
spine means Blender-hosted physics is a *Studio authoring convenience*, while the
engine's own physics runs through `PhysicsBackend` → Box3D. The two share concepts
but not code paths.

