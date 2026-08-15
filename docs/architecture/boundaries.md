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
