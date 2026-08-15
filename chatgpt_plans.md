I think this is a very viable long-term direction, provided we resist the temptation to define it as “open-source Unreal.” That describes the destination, but it’s a terrible architectural blueprint.

The more interesting goal is:

> **An open, modular 3D development environment where the editor, runtime, tools, scripting system, renderer, simulation, and automation layer are all first-class and independently extensible.**

Blender is an unusually strong starting point because it already covers an enormous portion of the content-production side: modeling, rigging, animation, simulation, rendering, compositing, motion tracking, and video editing. Its current repository is huge—over 164,000 commits—and Blender itself is GPLv3. ([GitHub][1])

Box3D is also an unusually interesting choice. It's a young MIT-licensed C17 physics library from Erin Catto, with data-oriented design, multithreading, SIMD, continuous collision detection, determinism, recording/replay, and large-world support. The initial 0.1.0 release is explicitly alpha software, so we'd want to integrate against a carefully isolated abstraction rather than hard-wire the entire engine to its current API. ([GitHub][2])

## The core idea

I'd divide the project into roughly this:

```text
                    ┌──────────────────────────────┐
                    │          STUDIO              │
                    │                              │
                    │  Modeling / Animation       │
                    │  Materials / World Editing  │
                    │  Level Design                │
                    │  Scripting / Debugging      │
                    │  Sequencing / Cinematics    │
                    │  Profiling / Automation     │
                    └──────────────┬───────────────┘
                                   │
                        Project / Asset Graph
                                   │
              ┌────────────────────┴─────────────────────┐
              │                                          │
       ┌──────▼──────┐                            ┌──────▼──────┐
       │   EDITOR    │                            │   RUNTIME   │
       │             │                            │             │
       │ Blender     │                            │ Game        │
       │ UI          │                            │ Simulation  │
       │ Tools       │                            │ Rendering   │
       │ Inspectors  │                            │ Audio       │
       └──────┬──────┘                            │ Networking  │
              │                                   │ Scripting   │
              │                                   └──────┬──────┘
              └───────────────────┬──────────────────────┘
                                  │
                         ENGINE FOUNDATION
                                  │
      ┌─────────────┬─────────────┼──────────────┬────────────┐
      │             │             │              │            │
   Renderer      Physics       Animation       Audio       Platform
      │             │             │              │            │
      │          Box3D           │              │            │
      └─────────────┴─────────────┴──────────────┴────────────┘
```

That gives us a really important distinction:

### **The editor is not the engine.**

Blender becomes an exceptionally powerful authoring environment.

The runtime becomes a separate thing.

Eventually you should be able to build:

```text
MyGame/
    project/
    assets/
    source/
    scripts/
    shaders/
    materials/
    animations/
    scenes/
    plugins/
    tests/
```

…and then produce:

```text
MyGame.exe
MyGame-linux
MyGame.app
MyGame-web
MyGame-server
```

without shipping the entire editor.

That is much closer to what we actually want.

---

# 1. The project should have a tiny, boring foundation

This is probably the single most important design decision.

We want a **small core** that defines things like:

```text
Object
Component
Transform
Asset
Scene
World
Resource
Entity
System
Module
Plugin
Command
Event
Property
Reference
```

And almost nothing else.

Everything higher level builds on this.

For example:

```text
PhysicsSystem
    ↓
PhysicsBodyComponent
    ↓
Box3D adapter
```

rather than:

```text
GameObject
    ↓
Box3DObject
```

That distinction gives us freedom to replace Box3D later.

Same thing for rendering:

```text
RenderSystem
    ↓
RenderBackend
    ├── Vulkan
    ├── DirectX 12
    ├── Metal
    └── WebGPU
```

rather than embedding Vulkan everywhere.

This should be a **systems architecture**, not a single giant engine.

---

# 2. Blender should be treated as a superpower

We shouldn't fight Blender's ecosystem.

We should exploit it.

Blender already knows how to deal with:

* meshes
* topology
* UVs
* materials
* armatures
* animation
* cameras
* lights
* geometry nodes
* procedural content
* rendering
* compositing
* video
* Python
* enormous numbers of file formats

So instead of reinventing all of that, we'd build an integration layer.

Something like:

```text
Blender
   │
   ├── Modeling
   ├── Sculpting
   ├── Animation
   ├── Rigging
   ├── Geometry
   └── Material Authoring
           │
           ▼
      Project Graph
           │
           ▼
        Runtime
```

Eventually there could even be a **headless project compiler**.

For example:

```bash
engine build MyGame
engine cook MyGame
engine package MyGame
engine test MyGame
engine run MyGame
```

That becomes enormously useful for both humans and agents.

---

# 3. Agent-first should mean “everything is inspectable”

This is where I think we can make the project genuinely different.

Don't bolt an AI chatbot onto the editor.

Instead, make the software **natively automatable**.

Every meaningful action should have a structured representation.

For example:

```json
{
  "command": "scene.create_entity",
  "name": "Player",
  "components": [
    "Transform",
    "PhysicsBody",
    "CharacterController"
  ]
}
```

The UI does this.

A Python script does this.

A CLI does this.

An external program does this.

An AI agent does this.

They all reach the same underlying command system.

That's the important part.

So:

```text
Human
   │
   ├── GUI
   ├── Python
   └── CLI
         │
         ▼
     Command API
         │
         ▼
       Engine
         ▲
         │
   Agent / LLM / IDE
```

An agent shouldn't have to “click Blender.”

It should be able to say:

```text
Create a scene.
Add a terrain system.
Generate 400 trees.
Create a player controller.
Assign this material.
Bake navigation.
Run the simulation.
Capture a performance report.
```

And execute those operations through the same APIs humans use.

That also makes automation useful **without AI**.

---

# 4. AI should be a plugin, not a foundation

I strongly agree with your requirement here.

I'd make this an explicit architectural rule:

> **The engine must not depend on AI.**

Not even conceptually.

AI lives in something like:

```text
modules/
    ai/
        assistant/
        generation/
        agents/
        vision/
        code/
```

And the core engine doesn't know those modules exist.

When disabled:

```text
AI_ENABLED = false
```

means:

* no model initialization
* no network connections
* no AI subprocesses
* no telemetry required for AI
* no model files loaded
* no hidden inference
* no behavior changes
* no degraded core functionality

Ideally a minimal build could literally exclude the AI module entirely:

```bash
engine --without-ai
```

That would be a very powerful philosophical statement for an open-source project.

---

# 5. The plugin architecture is actually more important than AI

I would design the entire program around modules.

Something like:

```text
Core
 ├── Scene
 ├── Assets
 ├── ECS
 ├── Serialization
 ├── Events
 ├── Commands
 └── Reflection

Optional
 ├── Renderer
 ├── Physics
 ├── Animation
 ├── Audio
 ├── Networking
 ├── Terrain
 ├── Particles
 ├── Navigation
 ├── VFX
 ├── Video
 ├── AI
 └── Editor Tools
```

And then third parties can write:

```text
MyTerrainPlugin
MyFluidSolver
MyRenderer
MyImporter
MyNetworkingStack
MyVRSystem
MyGameFramework
```

without modifying the engine.

That's how we get the community to grow the software instead of waiting for “the core team” to implement everything.

---

# 6. We should steal one thing from Unreal conceptually

Not its architecture.

Its **concept of a unified project**.

The user shouldn't think:

> “I have a Blender project, an engine project, a physics project, a shader project, an audio project…”

They should think:

> **“I have a project.”**

Everything belongs to it.

```text
Project
│
├── Scenes
├── Worlds
├── Assets
├── Materials
├── Scripts
├── Shaders
├── Audio
├── Animations
├── Physics
├── UI
├── Cinematics
├── Plugins
├── Tests
└── Build Configurations
```

---

# 7. I'd make the asset system extremely serious

This will quietly become one of the most important pieces of the entire project.

We need:

```text
Asset
├── UUID
├── Type
├── Source
├── Dependencies
├── Metadata
├── Version
├── Import settings
└── Generated artifacts
```

So if:

```text
dragon.fbx
```

changes, the engine knows:

```text
dragon.fbx
   ↓
Mesh
Material
Skeleton
Animation
Collision
LOD
Texture references
```

and knows exactly what needs rebuilding.

That leads naturally toward a **content-addressed / dependency-aware build system**.

Which leads to something really cool:

### The editor becomes an IDE for 3D worlds.

---

# 8. Physics

Box3D is an excellent foundation here.

Its current feature set already covers a lot of the hard stuff: rigid bodies, continuous collision, contact events, multiple collision shapes, sensors, character movement, joints, multithreading, SIMD, deterministic simulation, recording/replay, etc. ([GitHub][2])

I'd put a wrapper around it:

```text
Physics Abstraction
        │
   ┌────┴────┐
   │         │
 Box3D     Other
```

That gives us:

```text
PhysicsWorld
RigidBody
Collider
Joint
CharacterController
Raycast
ShapeCast
Overlap
PhysicsMaterial
```

instead of exposing Box3D's API directly throughout the project.

That becomes especially useful if Box3D's API changes while it's still maturing.

---

# 9. The renderer should also be backend-neutral

This is another place where I'd resist locking ourselves in.

Conceptually:

```text
Render API
   │
   ├── Vulkan
   ├── DX12
   ├── Metal
   └── WebGPU
```

Then higher-level systems deal with:

```text
Mesh
Material
Texture
Shader
Camera
Light
Pass
RenderGraph
```

instead of DirectX/Vulkan objects.

I'd also make the **render graph** a first-class concept very early.

Because eventually we'll want:

```text
Shadow Pass
        ↓
Depth Prepass
        ↓
GBuffer
        ↓
Lighting
        ↓
Reflections
        ↓
Volumetrics
        ↓
Post Processing
        ↓
UI
```

and developers should be able to add/remove/rearrange those.

---

# 10. Scripting

I'd make scripting multi-language from the beginning at the API level, even if we only ship one language initially.

Probably:

```text
Engine API
   │
   ├── C/C++
   ├── Python
   ├── JavaScript/TypeScript
   ├── Lua
   └── Future languages
```

But there should be one underlying reflected API.

For example:

```text
Entity
Component
Property
Method
Event
System
```

can all expose metadata.

That enables:

```text
Python
Agents
Editor inspectors
Documentation generation
Serialization
Networking
Debugging
Visual scripting
```

from the same information.

This is **huge**.

---

# 11. Reflection should become a central primitive

I'd actually put reflection incredibly early in the roadmap.

Imagine:

```cpp
struct Transform
{
    Vec3 position;
    Quat rotation;
    Vec3 scale;
};
```

and metadata says:

```text
Transform
 ├── position : Vec3
 ├── rotation : Quaternion
 └── scale    : Vec3
```

Then we automatically get:

```text
Inspector UI
Serialization
Undo/redo
Python bindings
Network replication
Editor search
Property animation
Agent manipulation
Documentation
```

One system.

That's the sort of foundational investment that pays dividends for years.

---

# 12. Undo/redo should be command-based

This follows naturally.

Don't make undo:

> “Restore the previous editor state.”

Make it:

```text
CreateEntityCommand
ModifyPropertyCommand
DeleteEntityCommand
AddComponentCommand
RemoveComponentCommand
MoveAssetCommand
```

Then:

```text
Undo
Redo
Macro recording
Automation
Scripting
Agent actions
Transaction logs
```

all become variations of the same mechanism.

Again, humans and agents use the same software.

---

# 13. The eventually enormous feature list

Eventually we'd want the platform to cover:

### Creation

Modeling
Sculpting
UV
Materials
Textures
Rigging
Animation
Geometry
Terrain
World building

### Engine

Rendering
Lighting
Shadows
Post-processing
Physics
Particles
VFX
Audio
Navigation
Networking
Input
UI
Localization
Save systems

### Production

Sequencer
Cinematics
Motion capture
Compositing
Color management
Video editing
Render farm
Asset management

### Programming

C/C++
Python
Visual scripting
Shader editor
Debugger
Profiler
Hot reload
Build system
Packaging

### Deployment

Windows
Linux
macOS
Android
iOS
Web
Consoles eventually
Dedicated servers
Headless builds

### Automation

CLI
Python API
Remote API
Project inspection
Structured commands
Agent API
Testing
CI/CD

That is the eventual “Unreal replacement.”

But **none of that belongs in v0.1**.

---

# A much more sane development sequence

I'd probably organize development around **capabilities**, not features.

## Phase 0 — Foundation

```text
Project format
Reflection
Serialization
Entity/component model
Asset IDs
Command system
Event system
Plugin system
Build system
Logging
Testing
CLI
```

This is the boring stuff.

It's also the stuff that determines whether the project survives five years.

## Phase 1 — First World

Get to:

```text
Open project
Create scene
Create entity
Transform objects
Camera
Lights
Meshes
Materials
Save
Load
Play
```

Then:

**“I can build a little 3D scene.”**

That's our first milestone.

## Phase 2 — Simulation

Integrate Box3D.

```text
Rigid bodies
Collision
Joints
Character controller
Queries
Physics debugging
Replay
```

Now we have an actual interactive engine.

## Phase 3 — Programming

```text
Runtime scripting
C++ API
Python
Hot reload
Debugger
Profiler
CLI
```

Now developers can build things.

## Phase 4 — Production

```text
Animation
Audio
Particles
VFX
UI
Navigation
Networking
Sequencer
```

## Phase 5 — Tool ecosystem

```text
Plugin SDK
Package manager
Marketplace/repository
Templates
Project generators
Documentation tooling
```

## Phase 6 — Agent ecosystem

Only after the underlying systems are good:

```text
Agent API
Scene reasoning
Code generation
Asset generation
Automated testing
Project maintenance
Optimization
```

That ordering matters.

**AI should make a great engine better, not make a mediocre engine appear easier to use.**

---

# One potentially controversial choice

I would seriously consider making the project **two related repositories/applications** eventually.

Something like:

```text
Project
├── engine
├── studio
├── SDK
└── tools
```

where:

### `engine`

The actual runtime.

Tiny as practical.

No UI.

No Blender dependency.

No AI.

No editor.

### `studio`

The full development environment.

Blender integration
Scene editor
Asset browser
Debugger
Profiler
Sequencer
Scripting
Visual tools
etc.

That gives us an extraordinarily useful property:

> **The game/runtime can remain radically smaller than the development environment.**

---

# Licensing deserves a deliberate decision

This one is important because Blender is GPLv3 while Box3D is MIT. Blender's repository explicitly notes that Blender as a whole is GPLv3, whereas individual files may use compatible licenses; Box3D is MIT. ([GitHub][1])

That doesn't prevent the project from existing—MIT code can be incorporated into GPL software—but it means we should decide early whether we're building:

**A. A Blender-derived GPL suite**

or

**B. A Blender-based ecosystem with a separable engine/runtime**

or

**C. A collection of independently licensed components around Blender**

I lean strongly toward **B/C architecturally**, even if the flagship Studio application ends up GPL.

It gives us much more freedom as the project grows.

I'd want the legal architecture settled before the repository starts accumulating thousands of lines.

---

# And now the name

I actually think the codename should **not** sound like an existing game engine.

Avoid:

```text
GameX
EngineX
3D Studio X
OpenSomething
NextUnreal
```

That's immediately limiting.

We want something that sounds like a **piece of creative infrastructure**.

A few directions:

### **Project Mosaic**

Probably my favorite.

Everything fits together:

```text
Models
Code
Physics
Animation
Audio
Worlds
Plugins
People
Agents
```

Many pieces become one thing.

And **Mosaic** doesn't imply a specific medium.

It could belong to a game developer, filmmaker, simulation researcher, artist, or programmer.

### **Project Forge**

Very strong metaphor.

Raw materials → creation → finished artifact.

Downside: “Forge” is an extremely crowded software naming space.

### **Project Foundry**

Also conceptually excellent.

The idea of a place where software and worlds are manufactured.

Downside: commercially crowded and already associated with several companies/products.

### **Project Prism**

Nice metaphor:

One source → many outputs.

Games
Films
Simulation
Visualization
XR

But again, a crowded name.

### **Project Atlas**

Great for worlds, environments, large-scale projects.

But very heavily used.

### **Project Mosaic**

Has the least “game engine” baggage.

And there's a nice philosophical meaning:

> **A tool whose purpose is to let many different pieces become something larger.**

---

# My current architectural thesis

I'd write the project's first manifesto almost this simply:

```text
Build worlds.

Model them.
Program them.
Simulate them.
Animate them.
Render them.
Play them.
Film them.
Automate them.

The editor is a tool.
The runtime is a tool.
The language is a tool.
AI is a tool.

No single workflow is mandatory.

Everything should be inspectable.
Everything should be scriptable.
Everything should be replaceable.
Everything should be extensible.
```

And the really interesting part is that this isn't actually trying to copy Unreal's philosophy.

It's closer to:

**Blender + Unreal + Godot + VS Code + Houdini + Git + an automation API**

with a deliberate rule that **the user and the agent are simply two different clients of the same underlying systems.**

That, to me, is the project worth spending years on.

My current favorite codename is **Mosaic**, but I'd keep the codename provisional until we've defined the architecture a little further. The next thing I'd nail down is the **actual v0.1 technical architecture—repository layout, core modules, entity/component model, project format, plugin API, Blender boundary, Box3D boundary, rendering abstraction, and scripting API**.

[1]: https://github.com/blender/blender "GitHub - blender/blender: Official mirror of Blender · GitHub"
[2]: https://github.com/erincatto/box3d?utm_source=chatgpt.com "GitHub - erincatto/box3d: Box3D is a 3D physics engine for games · GitHub"

Good. Let’s stop hand-waving and make some actual decisions.

I’m going to treat this as the **Architecture & Technical Specification for Project Mosaic v0.1**. These aren’t immutable forever, but they should be strong enough that somebody can start opening GitHub issues and writing code against them.

One important constraint from the actual upstream projects: Blender is a GPLv3 application with an enormous, mature codebase, while Box3D is a much smaller C17/MIT library with a clean C API and strong data-oriented/multithreaded design. Blender currently describes itself as covering the full 3D creation pipeline, and Box3D explicitly provides collision queries, rigid-body physics, joints, character movement, determinism, and replay. ([GitHub][1])

That strongly supports the architecture below.

# Project Mosaic — v0.1 Architecture

## 0. The foundational rule

**Mosaic is a platform, not an application.**

The platform has three layers:

```text
┌──────────────────────────────────────────────┐
│                 MOSAIC STUDIO                │
│                                              │
│ Blender integration                          │
│ Scene editing                                │
│ Asset tools                                  │
│ Scripting                                    │
│ Debugging                                    │
│ Sequencing                                   │
│ Project management                           │
└──────────────────────┬───────────────────────┘
                       │
                       │ Public Mosaic APIs
                       ▼
┌──────────────────────────────────────────────┐
│                MOSAIC ENGINE                 │
│                                              │
│ Scene / ECS                                   │
│ Assets                                        │
│ Rendering                                     │
│ Physics                                       │
│ Animation                                     │
│ Audio                                         │
│ Input                                         │
│ Networking                                    │
│ Scripting                                     │
│ Serialization                                 │
└──────────────────────┬───────────────────────┘
                       │
                       ▼
┌──────────────────────────────────────────────┐
│              MOSAIC FOUNDATION               │
│                                              │
│ Memory / Jobs / Math / Reflection             │
│ Events / Commands / Logging / IO              │
│ Plugin ABI / Resource system                  │
└──────────────────────────────────────────────┘
```

And then underneath those:

```text
External / replaceable implementations

Blender
Box3D
Vulkan
D3D12
Metal
Python
C/C++
etc.
```

**Foundation does not know about Blender.**

**Engine does not know about Blender.**

**Studio knows about both.**

That separation is sacred.

---

# 1. Repository layout

I want this to be a **monorepo initially**.

Not because monorepos are fashionable, but because early on we're going to be changing interfaces constantly and splitting repositories too early creates artificial friction.

I'd start with:

```text
mosaic/
│
├── CMakeLists.txt
├── CMakePresets.json
├── LICENSE
├── README.md
├── CONTRIBUTING.md
├── CODE_OF_CONDUCT.md
│
├── docs/
│   ├── architecture/
│   ├── api/
│   ├── design/
│   ├── rfcs/
│   └── tutorials/
│
├── foundation/
│   ├── core/
│   ├── memory/
│   ├── math/
│   ├── reflection/
│   ├── serialization/
│   ├── events/
│   ├── commands/
│   ├── jobs/
│   ├── io/
│   ├── logging/
│   └── platform/
│
├── engine/
│   ├── scene/
│   ├── ecs/
│   ├── assets/
│   ├── rendering/
│   ├── physics/
│   ├── animation/
│   ├── audio/
│   ├── input/
│   ├── scripting/
│   ├── networking/
│   └── runtime/
│
├── editor/
│   ├── shell/
│   ├── viewport/
│   ├── inspector/
│   ├── outliner/
│   ├── asset_browser/
│   ├── console/
│   ├── debugger/
│   ├── profiler/
│   └── tools/
│
├── studio/
│   ├── app/
│   ├── workspace/
│   └── blender/
│
├── modules/
│   ├── box3d/
│   ├── render_vulkan/
│   ├── render_d3d12/
│   ├── render_metal/
│   ├── python/
│   ├── ai/
│   └── ...
│
├── tools/
│   ├── mosaic-cli/
│   ├── assetc/
│   ├── scenec/
│   ├── shaderc/
│   ├── packager/
│   └── projectgen/
│
├── templates/
│   ├── empty/
│   ├── game/
│   ├── visualization/
│   └── cinematic/
│
└── tests/
    ├── unit/
    ├── integration/
    ├── rendering/
    ├── physics/
    └── projects/
```

### Why this structure?

Because directory ownership communicates architecture.

If someone has to ask:

> “Can this file depend on the editor?”

the directory structure should already answer:

**No.**

---

# 2. Language decisions

I'd make these decisions now:

### Core language: **C++20**

Not C++17.

We want modern C++ without immediately chasing every new standard feature.

C++20 gives us useful things such as:

* concepts
* `std::span`
* coroutines where appropriate
* stronger constexpr
* ranges
* improved threading primitives

But we should establish a **boring C++ subset** so the code remains approachable.

### Third-party libraries

Use third-party libraries aggressively where they provide commodity functionality, but don't let third-party APIs leak through the whole engine.

For example:

```text
Mosaic Image API
       │
       ▼
     stb
```

rather than having:

```cpp
stb_image(...)
```

all over the codebase.

Same principle with Box3D.

---

# 3. Foundation

Foundation is deliberately small.

## Core

```cpp
EntityID
AssetID
TypeID
ModuleID
CommandID
Version
Result<T>
Optional<T>
Span<T>
```

Plus:

```text
assertions
error handling
UUID generation
hashing
strings
containers
time
thread primitives
```

No rendering.

No physics.

No scene graph.

No editor.

---

# 4. Math

We need our own math layer.

```text
Vec2
Vec3
Vec4

Quat

Mat3
Mat4

Transform
AABB
Sphere
Ray
Plane
Frustum

Color
Rect
```

Probably SIMD-friendly implementation underneath, but the API should remain clean.

This also lets us establish one engine-wide convention for:

* handedness
* coordinate system
* rotation order
* radians/degrees
* units
* precision

### Decision:

**Right-handed, Y-up, meters, radians.**

We should make this explicit immediately.

Blender's ecosystem has lots of coordinate-conversion wrinkles; the adapter owns those conversions.

---

# 5. Entity/component architecture

I would **not** copy Blender's data model.

And I would **not** blindly copy Unreal's `Actor / Component / UObject` architecture.

Instead:

```text
World
 └── Entity
      ├── Transform
      ├── MeshRenderer
      ├── PhysicsBody
      ├── AudioSource
      └── Script
```

Entities are just identifiers.

Components own data.

Systems process components.

Conceptually:

```text
Entity
   │
   ├── TransformComponent
   ├── RenderComponent
   ├── PhysicsComponent
   └── ScriptComponent

        ↓

TransformSystem
RenderSystem
PhysicsSystem
ScriptSystem
```

### Important distinction

The entity **isn't the object**.

It's an identity.

This makes things much easier to serialize, replicate, clone, inspect, and automate.

---

# 6. World / Scene model

This is where I want a hybrid.

```text
Project
 ├── Assets
 ├── Scenes
 ├── Worlds
 └── Config
```

A scene is an asset.

A world is a runtime container.

For example:

```text
MainMenu.scene
Level01.scene
BossArena.scene
Character.scene
```

And:

```text
World
 ├── loaded scenes
 ├── entities
 ├── systems
 └── runtime state
```

That lets us eventually have:

```text
World streaming
Sub-scenes
Prefab-like assets
Level instances
Multiplayer worlds
Procedural worlds
```

without baking those concepts into the editor.

---

# 7. Serialization

This deserves serious design attention.

I propose:

### Human-readable project metadata

```text
JSON / TOML-like
```

### Binary runtime data

```text
Mosaic Resource Binary
```

### Every asset has:

```text
UUID
type
version
source
dependencies
metadata
```

For example:

```text
assets/
    81d3...asset
```

contains metadata saying:

```text
Type: Mesh
Source: Models/Robot.blend
Importer: blender
Version: 3
Dependencies:
  Material: ...
  Texture: ...
```

The generated runtime artifact can then be completely different.

---

# 8. Asset pipeline

This should be one of the earliest systems.

```text
Source Asset
      │
      ▼
Importer
      │
      ▼
Intermediate Representation
      │
      ▼
Cooker
      │
      ▼
Runtime Asset
```

Example:

```text
robot.blend
     ↓
Blender Importer
     ↓
Mosaic Mesh IR
     ↓
Mesh Cooker
     ↓
robot.mesh
```

This means Blender is a **source authoring tool**, not the runtime file format.

That's a very important distinction.

---

# 9. Blender integration

Here's where I'd deliberately **not fork Blender** initially.

Blender should run as a separate application/process.

Mosaic Studio communicates with it through an integration layer.

Initially:

```text
Mosaic Studio
      │
      │ Python / IPC
      ▼
   Blender
```

Blender can:

* import/export assets
* perform modeling operations
* bake data
* generate geometry
* manipulate animations
* provide authoring functionality

The Mosaic side receives canonical assets.

Later we can build deeper integration.

### Why separate processes?

Because crashing Blender shouldn't crash the engine.

And crashing the runtime shouldn't lose the editor.

That's worth a little complexity.

---

# 10. The Blender boundary

I want a formal interface:

```text
BlenderBridge
```

Something like:

```cpp
class IAssetAuthoringBackend
{
public:
    virtual ImportResult import_asset(
        const ImportRequest&
    ) = 0;

    virtual ExportResult export_asset(
        const ExportRequest&
    ) = 0;

    virtual std::vector<AssetInfo> inspect_scene(
        const SceneInspectionRequest&
    ) = 0;
};
```

Blender is one implementation.

Someday somebody can build:

```text
MayaBridge
HoudiniBridge
Cinema4DBridge
CustomBridge
```

without changing the engine.

---

# 11. Box3D integration

Box3D gets exactly the same treatment.

```text
Mosaic Physics API
       │
       ▼
PhysicsBackend
       │
       ▼
     Box3D
```

The public engine API becomes:

```cpp
PhysicsWorld
RigidBody
Collider
Shape
Joint
RaycastResult
ContactEvent
```

rather than:

```cpp
b3World
b3Body
b3Shape
```

Box3D already has a particularly attractive feature set for us—including continuous collision detection, contact/sensor events, character movement, queries, multithreading, SIMD, determinism, and recording/replay—so it makes sense as our initial backend. ([GitHub][2])

But developers never have to know we're using it.

That's important.

---

# 12. Rendering architecture

This deserves its own abstraction.

```text
Renderer
   │
   ├── RenderDevice
   ├── RenderGraph
   ├── Resources
   ├── Pipelines
   └── Passes
```

Backends:

```text
modules/
    render_vulkan/
    render_d3d12/
    render_metal/
```

### v0.1

I'd actually ship **one backend first**.

I'd lean toward:

**Vulkan**

because it gives us a strong cross-platform baseline and forces the abstraction to be honest.

But I would keep the interface backend-neutral from day one.

---

# 13. Render Graph

This should be a foundational engine concept.

Rather than:

```cpp
draw_shadows();
draw_world();
draw_postfx();
```

we describe resources and passes:

```text
ShadowMap
Depth
GBuffer
Lighting
Reflection
PostProcess
UI
```

The render graph resolves dependencies.

Eventually this gets us:

* automatic synchronization
* transient resources
* pass reordering
* debugging
* render graph visualization
* backend optimization

This is an investment that will matter enormously later.

---

# 14. Scripting

Here's where I want us to be slightly unusual.

### Primary embedded language: Python

Because:

* Blender already has Python
* excellent ecosystem
* great for tooling
* ideal for automation
* ideal for agents
* approachable

But Python is **not** the only programming model.

The runtime API should eventually support:

```text
Native C/C++
Python
Possibly Lua
Possibly WASM / other sandboxed scripting
```

And all of them target the same reflected engine API.

---

# 15. Reflection

This may actually be the most strategically important subsystem in the entire project.

Everything gets metadata.

```cpp
struct Transform
{
    Vec3 position;
    Quat rotation;
    Vec3 scale;
};
```

Reflection knows:

```text
Transform
 ├── position : Vec3
 ├── rotation : Quaternion
 └── scale    : Vec3
```

Then the engine can automatically generate:

```text
Inspector
Serialization
Python bindings
Documentation
Undo/redo
Automation
Agent tools
Network replication
Animation tracks
```

from the same metadata.

That is exactly how we get:

> **human-friendly and agent-friendly without creating two separate systems.**

---

# 16. Command architecture

This is another one I'd insist on early.

Every mutating action goes through a command.

```text
CreateEntity
DeleteEntity
SetProperty
AddComponent
RemoveComponent
MoveAsset
ImportAsset
BuildProject
RunScene
```

Commands have:

```text
ID
parameters
permissions
execution
undo
serialization
```

So a human clicking a UI button:

```text
Create Entity
```

and an agent doing:

```json
{
  "command": "scene.create_entity",
  "name": "Player"
}
```

are fundamentally performing the same operation.

That's the heart of the agent architecture.

---

# 17. Event system

Commands change state.

Events announce state changes.

```text
EntityCreated
EntityDestroyed
ComponentAdded
ComponentChanged
AssetImported
SceneLoaded
PhysicsContact
BuildCompleted
```

This keeps modules decoupled.

For example:

```text
Physics
   ↓
PhysicsContact event

Audio
   ↓
plays sound

Game logic
   ↓
applies damage

Telemetry
   ↓
records event
```

Physics doesn't need to know any of those things exist.

---

# 18. Jobs and multithreading

I'd establish a centralized job system early.

```text
JobSystem
 ├── worker threads
 ├── task queues
 ├── dependencies
 └── synchronization
```

Systems submit work.

They don't create random thread pools.

Then:

```text
Physics
Rendering
Animation
Asset cooking
Pathfinding
Audio processing
```

can all use the same infrastructure.

Box3D itself uses extensive multithreading and SIMD, so we shouldn't design an engine threading model that fights the physics backend. ([GitHub][2])

---

# 19. Plugin architecture

This is where I want to be strict.

There are **three plugin tiers**.

### Static modules

Compiled into the engine.

```text
Renderer
Physics
Platform
```

### Dynamic native plugins

```text
.dll
.so
.dylib
```

for:

```text
Tools
Importers
Gameplay frameworks
Editor extensions
Custom systems
```

### Script plugins

Python packages.

This gives us:

```text
maximum performance
+
maximum flexibility
+
easy experimentation
```

---

# 20. The CLI is a first-class citizen

This is absolutely not optional.

We want:

```bash
mosaic create game MyGame
mosaic editor MyGame
mosaic run MyGame
mosaic build MyGame
mosaic test MyGame
mosaic cook MyGame
mosaic package MyGame
mosaic inspect MyGame
mosaic asset import ...
```

And crucially:

```bash
mosaic --json ...
```

Every command should optionally return machine-readable data.

That's incredibly useful for CI/CD and agents.

---

# 21. The agent API

I don't want an “AI API.”

I want a **Mosaic Automation API**.

AI is merely one consumer.

For example:

```text
Mosaic Automation API
      │
      ├── CLI
      ├── Python
      ├── IPC
      ├── Remote API
      ├── IDE integrations
      └── AI agents
```

An agent might query:

```text
project.list_assets
scene.list_entities
entity.get_components
component.get_properties
asset.inspect
build.status
runtime.get_metrics
```

and mutate through commands.

### Security

This should be permissioned:

```text
READ_PROJECT
MODIFY_PROJECT
EXECUTE_CODE
EXECUTE_PROCESS
NETWORK_ACCESS
FILE_ACCESS
BUILD_PROJECT
DEPLOY_PROJECT
```

An agent shouldn't automatically have god mode.

---

# 22. AI subsystem

Then AI becomes:

```text
modules/ai/
```

with providers:

```text
Local LLM
Remote API
Vision model
Embedding model
Speech
Image generation
Code generation
```

But the core only sees:

```text
IAgentProvider
```

and preferably even that lives above Engine.

### AI disabled

The modules don't load.

No hidden fallback.

No behavior changes.

No model dependencies.

No mysterious network traffic.

The Studio settings can say:

```text
AI Features
[ ] Enable AI subsystem
```

and that's it.

---

# 23. Editor architecture

The editor isn't “a bunch of windows.”

It's a collection of tools that manipulate engine state.

```text
Editor Shell
 ├── Viewport
 ├── Outliner
 ├── Inspector
 ├── Asset Browser
 ├── Console
 ├── Script Editor
 ├── Debugger
 ├── Profiler
 └── Sequencer
```

Every tool talks to:

```text
Engine
   ↓
Commands
   ↓
Events
```

not directly to other editor tools.

---

# 24. Project file

I'd make the root project file something dead simple:

```text
MyGame/
    project.mosaic
```

Something conceptually like:

```yaml
project:
  name: MyGame
  version: 0.1

engine:
  version: 0.1

startup:
  scene: scenes/main.scene

modules:
  - rendering.vulkan
  - physics.box3d
  - scripting.python

platforms:
  - windows
  - linux
```

But we should not cement YAML itself until we test the serialization design.

The **schema** matters more than the syntax.

---

# 25. The first actual `v0.1`

Now we're getting somewhere.

I would define v0.1 very narrowly:

### Foundation

* C++20 core
* math
* UUIDs
* reflection
* serialization
* logging
* jobs
* commands
* events

### Engine

* ECS
* World
* Scene
* asset registry
* runtime loop
* transform system

### Rendering

* Vulkan backend
* mesh
* material
* texture
* camera
* directional/point light
* basic render graph

### Physics

* Box3D adapter
* rigid bodies
* primitive colliders
* raycasts
* contacts

### Scripting

* Python embedding
* engine reflection bindings
* command access

### Tools

* CLI
* project generator
* asset compiler

### Studio

* viewport
* scene hierarchy
* inspector
* asset browser
* console
* play/stop
* basic Blender bridge

That's enough.

Not “Unreal competitor.”

Instead:

> **A developer can create a project, bring in a model, put it in a world, move it around, attach physics, write code, press Play, and build the project.**

That's the first mountain.

---

# 26. The first demo should be embarrassingly simple

Not a game.

We build something like:

```text
Mosaic Demo
│
├── terrain
├── environment
├── physics objects
├── controllable character
├── camera
└── simple scripted interaction
```

The developer workflow should be:

```text
mosaic create game Demo
        ↓
open Studio
        ↓
import Blender asset
        ↓
drag into scene
        ↓
add physics
        ↓
attach Python script
        ↓
Play
        ↓
Build
```

That is the first “holy shit, this works” moment.

---

# 27. The architecture dependency rules

I'd actually enforce these in CI.

```text
Foundation
    ↓
Engine
    ↓
Editor
    ↓
Studio
```

Allowed:

```text
Engine → Foundation
Editor → Engine
Studio → Editor
```

Forbidden:

```text
Foundation → Engine
Foundation → Editor
Engine → Editor
Engine → Blender
Engine → AI
```

Modules may depend on the interfaces they implement, but not on arbitrary editor code.

This keeps the architecture from slowly rotting.

---

# 28. One thing I would NOT do

I would **not fork Blender into the Mosaic repository on day one.**

That would feel productive and probably be a long-term trap.

Blender is already enormous and mature. Its official repository currently shows more than 164,000 commits and encompasses the entire 3D pipeline. ([GitHub][1])

Instead:

```text
Mosaic
   │
   └── BlenderBridge
            │
            ▼
         Blender
```

Then later we can decide whether deeper integration justifies:

```text
Mosaic Studio
   └── embedded Blender functionality
```

after the platform itself has proven what it needs.

---

# 29. And I want one unusual design document

From day one, maintain:

**`ARCHITECTURE.md`**

with a section called:

## “Things Mosaic Will Never Do”

Examples:

```text
The runtime will never require the editor.

The engine will never require AI.

The core will never depend on Blender.

The public engine API will never expose Box3D types.

Plugins will not require modifying core source.

Automation will not require simulating mouse/keyboard input.

AI features will never be mandatory.

The project format will not depend on a proprietary service.

A rendering backend will not define the engine's rendering API.
```

That document becomes our guardrail.

Because the hardest part of a ten-year open-source project isn't inventing features.

It's **preventing future contributors from accidentally destroying the architecture while trying to add them.**

---

# So our actual v0.1 stack is

```text
                     MOSAIC STUDIO
                           │
                  ┌────────┴────────┐
                  │                 │
              EDITOR            BLENDER
                  │              BRIDGE
                  │                 │
                  └────────┬────────┘
                           │
                    MOSAIC ENGINE
                           │
         ┌─────────────────┼──────────────────┐
         │                 │                  │
        ECS            RENDERER            PHYSICS
         │             Vulkan               Box3D
         │
     Reflection
         │
     Serialization
         │
      Commands
         │
       Events
         │
     Job System
         │
       Math/IO
         │
                     FOUNDATION
```

And above all of that:

```text
                 AUTOMATION API
                       │
          ┌────────────┼────────────┐
          │            │            │
         CLI         Python       Agents
```

That's the architecture I'd be comfortable putting into a public repository.

And I think there's one particularly important consequence of this design:

**Mosaic isn't fundamentally an “engine with an editor.”**

It's a **programmable 3D operating environment** where the engine, editor, content pipeline, scripting system, and automation layer all operate on the same underlying project model.

That is a much more interesting target than “open-source Unreal.”

### The next thing I'd spec

The next layer down is where this gets really fun: **the actual data model**.

I'd nail down the exact definitions of `Entity`, `Component`, `World`, `Scene`, `Asset`, `Resource`, `System`, `Command`, `Event`, `Module`, and `Plugin`, including how they are serialized and how Python sees them.

Once those are correct, most of the rest of the engine can grow around them without us having to rewrite the foundation every six months.

[1]: https://github.com/blender/blender "GitHub - blender/blender: Official mirror of Blender · GitHub"
[2]: https://github.com/erincatto/box3d "GitHub - erincatto/box3d: Box3D is a 3D physics engine for games · GitHub"
