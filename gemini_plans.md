Building a production-ready, open-source 3D suite capable of competing with Unreal Engine requires a decoupled, modular architecture from day one. Integrating Blender’s DCC (Digital Content Creation) depth with Erin Catto’s deterministic C17 **Box3D** physics engine gives you a robust baseline for both interactive DCC tools and a high-performance runtime.

---

### Core Architecture & Strategy

To balance Blender's heavy GPL pipeline with the speed and permissiveness required for standalone commercial games and simulations, the suite should be split into three decoupled tiers:

```
┌─────────────────────────────────────────────────────────────┐
│                      Authoring Layer                        │
│   Blender Viewport + Node Logic + Python/C++ Native Addons  │
└──────────────┬───────────────────────────────┬──────────────┘
               │ Live-Link / USD / IPC         │ MCP / Headless RPC
┌──────────────▼───────────────────────────────▼──────────────┐
│                    Runtime Engine (Core)                    │
│   C/C++ Modern Renderer (Vulkan/Metal) + Box3D Simulation    │
└─────────────────────────────────────────────────────────────┘

```

* **Editor & DCC (Blender-Native):** Leverages Blender’s existing UI, sculpting, animation curves, geometry nodes, and compositor. Physics authoring connects directly to Box3D through native C/Python bindings.
* **Standalone High-Performance Runtime:** A lightweight standalone player (built in C++20 / C17) linking Box3D, audio, and a modern renderer (e.g., Vulkan/DirectX 12/Metal via a backend like Sokol or bgfx/wgpu) so shipped games and demos run independently without Blender runtime overhead.
* **Deterministic Physics (Box3D):** Provides cross-platform determinism, Soft Step solvers, SIMD contact solving, continuous collision detection (CCD), and character controllers.

---

### Agent & Human Dual-Interface Architecture

To ensure the engine is effortless for human creators while remaining fully manipulable by AI agents without visual clutter, the system is designed around headless state representation:

* **Complete UI Decoupling:** AI features exist entirely as an optional, background daemon. When disabled, the editor is a 100% vanilla viewport/node workflow with zero agent UI footprint.
* **Model Context Protocol (MCP) & Headless RPC Server:** An embedded local socket/IPC server exposes the entire scene graph as a structured AST (Abstract Syntax Tree). Agents can read scene state, instantiate prefabs, adjust Box3D rigid body colliders, or trigger bake routines via standard JSON/RPC without relying on computer-vision screen capture.
* **Deterministic Simulation Snapshots:** Box3D’s native record/replay and memory snapshot capabilities allow AI agents to step physics tick-by-tick, evaluate reward/loss functions, and optimize scene layouts or gameplay parameters automatically.

---

### Phased Development Roadmap

**Phase 1: Box3D Integration & MVP Viewport Runner**

* Build a native C/Python bridge (`box3d-blender`) implementing dynamic/static rigid bodies, compound hulls, joints, and trigger sensors.
* Implement an interactive "Play in Viewport" mode directly inside Blender’s 3D view.
* Expose the core scene API over an initial Python socket for headless command injection.

**Phase 2: Standalone Export Pipeline & Scripting Layer**

* Implement a 1-click packager that serializes Blender scenes (meshes, materials, Box3D physics definitions) into a binary runtime container.
* Build the standalone C++ lightweight game client to run the exported packages.
* Choose and integrate a primary scripting layer (e.g., Luau, C# bindings, or typed Python/WebAssembly) with zero-cost Box3D bindings.

**Phase 3: Visual Logic Nodes & Real-Time Production Tooling**

* Extend Blender’s node system for gameplay logic, state machines, animation state blending, and audio triggers.
* Implement camera sequencer tools for real-time virtual production and cinematic playback.
* Ship the plug-and-play MCP agent interface for assisted asset placement, procedural level generation, and physics debugging.

**Phase 4: High-End Rendering & Scalability (Targeting Unreal Parity)**

* Transition standalone runtime to modern GPU-driven rendering pipelines (mesh shaders, GPU scene hierarchies, software/hardware raytracing).
* Implement level-of-detail streaming and double-precision coordinate systems for massive open-world scales.

Designing the native C/Python Box3D integration layer requires a setup that delivers zero-copy transforms, non-blocking tick loops inside Blender, and deterministic synchronization between Blender's scene data and the native physics world.

---

### Layered Architecture

The integration divides into three distinct tiers to keep C-level performance isolated from Python overhead:

```
┌─────────────────────────────────────────────────────────────┐
│                   Blender UI & Addon (Python)               │
│   Custom Panels • Rigidbody Properties • Operator Callbacks │
└──────────────────────────────┬──────────────────────────────┘
                               │ ctypes / CFFI / pybind11
┌──────────────────────────────▼──────────────────────────────┐
│                  Native Bridge (`libbox3d_bridge`)          │
│   Lifecycle Management • Transform Buffers • Event Queues   │
└──────────────────────────────┬──────────────────────────────┘
                               │ Direct C17 ABI
┌──────────────────────────────▼──────────────────────────────┐
│                      Box3D Core (C17)                       │
│    b3World • b3BodyId • b3ShapeId • b3ContactEvents • SIMD  │
└─────────────────────────────────────────────────────────────┘

```

---

### Core Data Structures & Memory Layout

To allow high-speed batch synchronization across thousands of objects per frame, the native bridge holds contiguous memory arrays that Python can read via `ctypes.Array` or `memoryview` with zero allocations per tick:

```c
// Native transform block for batch updates
typedef struct {
    uint32_t body_id;
    float position[3];
    float rotation[4]; // Quaternion (x, y, z, w)
    float linear_velocity[3];
    float angular_velocity[3];
} B3TransformData;

// Native world handle managing lifecycle and batched output
typedef struct {
    b3WorldId world_id;
    uint32_t capacity;
    uint32_t active_body_count;
    B3TransformData* transform_buffer;
} B3BridgeContext;

```

---

### Native C Bridge Interface (`box3d_bridge.h`)

The C API exports flat, deterministic C functions that Python binds against directly:

```c
#ifndef BOX3D_BRIDGE_H
#define BOX3D_BRIDGE_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// World Management
void* b3_bridge_create_world(float gravity_x, float gravity_y, float gravity_z);
void  b3_bridge_destroy_world(void* ctx);
void  b3_bridge_step(void* ctx, float time_step, int32_t sub_step_count);

// Body Creation & Management
uint32_t b3_bridge_create_body(void* ctx, int32_t body_type, const float* position, const float* rotation);
void     b3_bridge_destroy_body(void* ctx, uint32_t body_id);

// Shape Attachments
uint32_t b3_bridge_attach_box(void* ctx, uint32_t body_id, float hx, float hy, float hz, float density, float friction);
uint32_t b3_bridge_attach_sphere(void* ctx, uint32_t body_id, float radius, float density, float friction);
uint32_t b3_bridge_attach_capsule(void* ctx, uint32_t body_id, float radius, float half_height, float density, float friction);
uint32_t b3_bridge_attach_convex_hull(void* ctx, uint32_t body_id, const float* points, uint32_t point_count, float density, float friction);

// Batch Sync
const B3TransformData* b3_bridge_get_transform_buffer(void* ctx, uint32_t* out_count);

#ifdef __cplusplus
}
#endif
#endif

```

---

### Python Integration & Viewport Modal Loop

The Python layer leverages Blender’s `bpy.app.timers` or a viewport `Modal Operator` to step the simulation at a fixed tick rate and synchronize matrices directly into `bpy.types.Object`.

**Custom Blender Properties (`properties.py`)**

```python
import bpy

class Box3DBodySettings(bpy.types.PropertyGroup):
    enabled: bpy.props.BoolProperty(name="Enable Physics", default=False)
    body_type: bpy.props.EnumProperty(
        name="Type",
        items=[
            ('STATIC', "Static", "Static immovable geometry"),
            ('KINEMATIC', "Kinematic", "Driven by animation curves"),
            ('DYNAMIC', "Dynamic", "Simulated with Box3D forces"),
        ],
        default='DYNAMIC'
    )
    shape_type: bpy.props.EnumProperty(
        name="Collider",
        items=[
            ('BOX', "Box", "Oriented bounding box"),
            ('SPHERE', "Sphere", "Bounding sphere"),
            ('CAPSULE', "Capsule", "Vertical capsule"),
            ('HULL', "Convex Hull", "Convex mesh hull"),
        ],
        default='BOX'
    )
    density: bpy.props.FloatProperty(name="Density", default=1.0, min=0.0)
    friction: bpy.props.FloatProperty(name="Friction", default=0.5, min=0.0, max=1.0)
    restitution: bpy.props.FloatProperty(name="Restitution", default=0.1, min=0.0, max=1.0)

```

**Viewport Modal Engine Sync Loop (`operator.py`)**

```python
import bpy
import ctypes
from mathutils import Matrix, Quaternion, Vector

class OT_Box3DViewportRunner(bpy.types.Operator):
    bl_idname = "box3d.viewport_runner"
    bl_label = "Run Box3D Simulation"

    def __init__(self):
        self._timer = None
        self.bridge = ctypes.CDLL("./libbox3d_bridge.so")
        self.world_ctx = None
        self.object_map = {}  # {body_id: bpy.types.Object}

    def modal(self, context, event):
        if event.type == 'ESC':
            return self.cancel(context)

        if event.type == 'TIMER':
            # 1. Step simulation at fixed 60Hz sub-stepping
            self.bridge.b3_bridge_step(self.world_ctx, ctypes.c_float(1.0 / 60.0), ctypes.c_int32(4))

            # 2. Retrieve batched transforms
            count = ctypes.c_uint32()
            buffer_ptr = self.bridge.b3_bridge_get_transform_buffer(self.world_ctx, ctypes.byref(count))
            
            # 3. Synchronize Blender transforms
            transforms = ctypes.cast(buffer_ptr, ctypes.POINTER(B3TransformData * count.value)).contents
            for i in range(count.value):
                data = transforms[i]
                obj = self.object_map.get(data.body_id)
                if obj:
                    obj.location = Vector((data.position[0], data.position[1], data.position[2]))
                    obj.rotation_quaternion = Quaternion((data.rotation[3], data.rotation[0], data.rotation[1], data.rotation[2]))

            context.area.tag_redraw()

        return {'PASS_THROUGH'}

    def execute(self, context):
        self.world_ctx = self.bridge.b3_bridge_create_world(
            ctypes.c_float(0.0), ctypes.c_float(0.0), ctypes.c_float(-9.81)
        )
        self.register_scene_bodies(context)
        wm = context.window_manager
        self._timer = wm.event_timer_add(1.0 / 60.0, window=context.window)
        wm.modal_handler_add(self)
        return {'RUNNING_MODAL'}

    def cancel(self, context):
        if self._timer:
            context.window_manager.event_timer_remove(self._timer)
        if self.world_ctx:
            self.bridge.b3_bridge_destroy_world(self.world_ctx)
        return {'CANCELLED'}

```

---

### Geometry Extraction for Colliders

To pass Blender mesh geometry efficiently to Box3D's convex hull constructor:

* **Convex Hull Extraction:** Read vertices directly via `mesh.vertices.foreach_get('co', flat_float_array)` and forward the raw pointer into `b3_bridge_attach_convex_hull`.
* **Primitive Fallback:** Calculate local-space bounding dimensions from `obj.dimensions` for native primitive shapes (`b3_bridge_attach_box`, `b3_bridge_attach_sphere`), avoiding mesh hull compute overhead whenever possible.
* **Compound Shapes:** Support hierarchy parenting where child meshes flagged with physics properties register as additional shapes onto the parent's `b3BodyId`.

Implementing compound colliders and constraint joints requires computing local-space coordinate transforms between parent-child hierarchies and mapping them directly to Box3D's multi-shape body structures and joint solvers.

---

### 1. Compound Collider Pipeline

In Box3D, a single `b3BodyId` can host multiple `b3ShapeId` instances. Each shape is positioned using a local transform offset (position and orientation) relative to the body's center of mass / root origin.

```
┌──────────────────────────────────────────────────────────────┐
│                    Root Dynamic Body (b3BodyId)              │
├──────────────────────────────┬───────────────────────────────┤
│  Shape 1 (Box)               │ Shape 2 (Capsule)             │
│  Offset: [0.0, 0.0, 0.0]     │ Offset: [0.0, 1.2, 0.0]       │
│  Rot: [0, 0, 0, 1]           │ Rot: [0.707, 0, 0, 0.707]     │
└──────────────────────────────┴───────────────────────────────┘

```

#### Native Bridge Interface for Compound Shapes

```c
typedef struct {
    float position[3];
    float rotation[4]; // Quaternion (x, y, z, w)
} B3TransformOffset;

// Attach shapes with local offsets to a target body
uint32_t b3_bridge_attach_box_offset(
    void* ctx, uint32_t body_id, 
    float hx, float hy, float hz, 
    const B3TransformOffset* offset, 
    float density, float friction, float restitution
);

uint32_t b3_bridge_attach_capsule_offset(
    void* ctx, uint32_t body_id, 
    float radius, float half_height, 
    const B3TransformOffset* offset, 
    float density, float friction, float restitution
);

```

#### Blender Hierarchy Extractor (Python)

When traversing the Blender hierarchy, child objects marked as colliders are extracted relative to the root body's world matrix:

```python
import mathutils

def attach_compound_hierarchy(bridge, world_ctx, root_obj, body_id):
    inv_root_matrix = root_obj.matrix_world.inverted()

    for child in root_obj.children:
        if not child.box3d_collider.enabled:
            continue

        # Compute relative transform from root body to child collider
        local_transform = inv_root_matrix @ child.matrix_world
        loc, rot, _ = local_transform.decompose()

        c_offset = B3TransformOffset()
        c_offset.position = (loc.x, loc.y, loc.z)
        c_offset.rotation = (rot.x, rot.y, rot.z, rot.w)

        settings = child.box3d_collider
        if settings.shape_type == 'BOX':
            dim = child.dimensions * 0.5
            bridge.b3_bridge_attach_box_offset(
                world_ctx, body_id, dim.x, dim.y, dim.z,
                c_offset, settings.density, settings.friction, settings.restitution
            )
        elif settings.shape_type == 'CAPSULE':
            bridge.b3_bridge_attach_capsule_offset(
                world_ctx, body_id, settings.radius, settings.half_height,
                c_offset, settings.density, settings.friction, settings.restitution
            )

```

---

### 2. Native Joint Definitions (`box3d_bridge_joints.h`)

Joints in Box3D connect two bodies via local anchor points and local reference axes.

```c
#ifndef BOX3D_BRIDGE_JOINTS_H
#define BOX3D_BRIDGE_JOINTS_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    B3_JOINT_FIXED = 0,
    B3_JOINT_REVOLUTE,     // Hinge (1-DoF rotation)
    B3_JOINT_PRISMATIC,    // Slider (1-DoF translation)
    B3_JOINT_DISTANCE,     // Soft Spring / Rod
    B3_JOINT_6DOF          // Generic configurable limits
} B3JointType;

typedef struct {
    uint32_t body_id_a;
    uint32_t body_id_b;
    float local_anchor_a[3];
    float local_anchor_b[3];
    float local_axis_a[3];
    float local_axis_b[3];
    bool collide_connected;
} B3JointBaseDef;

typedef struct {
    B3JointBaseDef base;
    bool enable_limit;
    float lower_angle;
    float upper_angle;
    bool enable_motor;
    float motor_speed;
    float max_motor_torque;
} B3RevoluteJointDef;

typedef struct {
    B3JointBaseDef base;
    float target_distance;
    float frequency_hz;   // 0 = rigid distance joint, >0 = soft spring
    float damping_ratio;  // 1.0 = critical damping
    float min_distance;
    float max_distance;
} B3DistanceSpringJointDef;

typedef struct {
    B3JointBaseDef base;
    float linear_lower_limits[3];
    float linear_upper_limits[3];
    float angular_lower_limits[3];
    float angular_upper_limits[3];
    float spring_frequency[6]; // X, Y, Z linear + 3 rotational axes
    float spring_damping[6];
} B36DofJointDef;

// Joint Lifecycle API
uint32_t b3_bridge_create_revolute_joint(void* ctx, const B3RevoluteJointDef* def);
uint32_t b3_bridge_create_distance_joint(void* ctx, const B3DistanceSpringJointDef* def);
uint32_t b3_bridge_create_6dof_joint(void* ctx, const B36DofJointDef* def);
void     b3_bridge_destroy_joint(void* ctx, uint32_t joint_id);

#ifdef __cplusplus
}
#endif
#endif

```

---

### 3. Local Anchor & Axis Computation

Blender empties or dedicated joint objects define constraints in world space. These must be transformed into the local coordinate frames of both connected bodies before registering with Box3D.

```python
def compute_joint_anchors(joint_obj, body_obj_a, body_obj_b):
    world_joint_matrix = joint_obj.matrix_world
    
    # 1. Transform joint world position into local coordinates of both bodies
    inv_mat_a = body_obj_a.matrix_world.inverted()
    inv_mat_b = body_obj_b.matrix_world.inverted()
    
    anchor_a = inv_mat_a @ world_joint_matrix.translation
    anchor_b = inv_mat_b @ world_joint_matrix.translation
    
    # 2. Extract primary constraint axis (Blender local Z-axis vector)
    joint_z_axis = world_joint_matrix.to_3x3() @ mathutils.Vector((0.0, 0.0, 1.0))
    joint_z_axis.normalize()
    
    # 3. Transform axis direction into local spaces
    axis_a = (inv_mat_a.to_3x3() @ joint_z_axis).normalized()
    axis_b = (inv_mat_b.to_3x3() @ joint_z_axis).normalized()
    
    return (
        (anchor_a.x, anchor_a.y, anchor_a.z),
        (anchor_b.x, anchor_b.y, anchor_b.z),
        (axis_a.x, axis_a.y, axis_a.z),
        (axis_b.x, axis_b.y, axis_b.z)
    )

```

---

### 4. Blender Constraint UI Properties

```python
import bpy

class Box3DJointProperties(bpy.types.PropertyGroup):
    enabled: bpy.props.BoolProperty(name="Enable Joint", default=True)
    joint_type: bpy.props.EnumProperty(
        name="Joint Type",
        items=[
            ('FIXED', "Fixed", "Lock all DoF"),
            ('REVOLUTE', "Hinge / Revolute", "1-DoF rotational joint"),
            ('DISTANCE', "Distance / Spring", "Linear spring-damper"),
            ('6DOF', "Generic 6-DoF", "Custom limits and springs on all 6 axes"),
        ],
        default='REVOLUTE'
    )
    target_a: bpy.props.PointerProperty(name="Body A", type=bpy.types.Object)
    target_b: bpy.props.PointerProperty(name="Body B", type=bpy.types.Object)
    collide_connected: bpy.props.BoolProperty(name="Collide Connected", default=False)
    
    # Spring-Damper parameters
    frequency_hz: bpy.props.FloatProperty(name="Frequency (Hz)", default=4.0, min=0.0)
    damping_ratio: bpy.props.FloatProperty(name="Damping Ratio", default=0.7, min=0.0)
    
    # Angular Limits
    enable_limits: bpy.props.BoolProperty(name="Enable Limits", default=False)
    lower_limit: bpy.props.FloatProperty(name="Lower Limit", default=-1.57, subtype='ANGLE')
    upper_limit: bpy.props.FloatProperty(name="Upper Limit", default=1.57, subtype='ANGLE')
    
    # Motor controls
    enable_motor: bpy.props.BoolProperty(name="Enable Motor", default=False)
    motor_speed: bpy.props.FloatProperty(name="Target Speed", default=0.0)
    max_torque: bpy.props.FloatProperty(name="Max Torque", default=100.0, min=0.0)

```

---

### 5. C17 Bridge Implementation (`box3d_bridge_joints.c`)

```c
#include "box3d_bridge_joints.h"
#include "box3d/box3d.h"

uint32_t b3_bridge_create_revolute_joint(void* ctx, const B3RevoluteJointDef* def) {
    b3WorldId world_id = ((B3BridgeContext*)ctx)->world_id;
    
    b3RevoluteJointDef joint_def = b3DefaultRevoluteJointDef();
    joint_def.bodyIdA = (b3BodyId){ .index = def->base.body_id_a };
    joint_def.bodyIdB = (b3BodyId){ .index = def->base.body_id_b };
    
    joint_def.localAnchorA = (b3Vec3){ def->base.local_anchor_a[0], def->base.local_anchor_a[1], def->base.local_anchor_a[2] };
    joint_def.localAnchorB = (b3Vec3){ def->base.local_anchor_b[0], def->base.local_anchor_b[1], def->base.local_anchor_b[2] };
    
    joint_def.localAxisA = (b3Vec3){ def->base.local_axis_a[0], def->base.local_axis_a[1], def->base.local_axis_a[2] };
    joint_def.localAxisB = (b3Vec3){ def->base.local_axis_b[0], def->base.local_axis_b[1], def->base.local_axis_b[2] };
    
    joint_def.enableLimit = def->enable_limit;
    joint_def.lowerAngle = def->lower_angle;
    joint_def.upperAngle = def->upper_angle;
    
    joint_def.enableMotor = def->enable_motor;
    joint_def.motorSpeed = def->motor_speed;
    joint_def.maxMotorTorque = def->max_motor_torque;
    joint_def.collideConnected = def->base.collide_connected;

    b3JointId joint_id = b3CreateRevoluteJoint(world_id, &joint_def);
    return joint_id.index;
}

uint32_t b3_bridge_create_distance_joint(void* ctx, const B3DistanceSpringJointDef* def) {
    b3WorldId world_id = ((B3BridgeContext*)ctx)->world_id;
    
    b3DistanceJointDef joint_def = b3DefaultDistanceJointDef();
    joint_def.bodyIdA = (b3BodyId){ .index = def->base.body_id_a };
    joint_def.bodyIdB = (b3BodyId){ .index = def->base.body_id_b };
    
    joint_def.localAnchorA = (b3Vec3){ def->base.local_anchor_a[0], def->base.local_anchor_a[1], def->base.local_anchor_a[2] };
    joint_def.localAnchorB = (b3Vec3){ def->base.local_anchor_b[0], def->base.local_anchor_b[1], def->base.local_anchor_b[2] };
    
    joint_def.length = def->target_distance;
    joint_def.hertz = def->frequency_hz;
    joint_def.dampingRatio = def->damping_ratio;
    joint_def.minLength = def->min_distance;
    joint_def.maxLength = def->max_distance;
    joint_def.collideConnected = def->base.collide_connected;

    b3JointId joint_id = b3CreateDistanceJoint(world_id, &joint_def);
    return joint_id.index;
}

```

---
Committing high-frequency deterministic physics back to Blender's animation curves requires decoupling the simulation loop from the viewport refresh rate, capturing trajectory data in native memory buffers, and bulk-writing keyframe points to Blender's `AnimData` / `FCurve` structures.

---

### 1. Native Memory Recording Architecture

Stepping the physics simulation headless and accumulating raw transform data into a contiguous memory block in C avoids Python per-frame overhead and guarantees deterministic frame stepping:

```c
// Struct matching single-frame transform sample
typedef struct {
    uint32_t frame;
    float position[3];
    float rotation[4]; // Quaternion (x, y, z, w)
} B3FrameSample;

// Per-body trajectory container
typedef struct {
    uint32_t body_id;
    uint32_t sample_count;
    B3FrameSample* samples;
} B3BodyTrajectory;

// Bridge context recorder extension
typedef struct {
    uint32_t total_frames;
    uint32_t body_count;
    B3BodyTrajectory* trajectories;
} B3BakeRecorder;

```

#### Native Bridge Recording Functions (`box3d_bridge_baker.c`)

```c
#include "box3d_bridge.h"
#include <stdlib.h>
#include <string.h>

B3BakeRecorder* b3_bridge_init_recorder(uint32_t body_count, uint32_t total_frames) {
    B3BakeRecorder* rec = (B3BakeRecorder*)malloc(sizeof(B3BakeRecorder));
    rec->total_frames = total_frames;
    rec->body_count = body_count;
    rec->trajectories = (B3BodyTrajectory*)malloc(sizeof(B3BodyTrajectory) * body_count);

    for (uint32_t i = 0; i < body_count; i++) {
        rec->trajectories[i].sample_count = 0;
        rec->trajectories[i].samples = (B3FrameSample*)malloc(sizeof(B3FrameSample) * total_frames);
    }
    return rec;
}

void b3_bridge_record_frame(B3BakeRecorder* rec, void* world_ctx, uint32_t frame_index) {
    uint32_t count = 0;
    const B3TransformData* transforms = b3_bridge_get_transform_buffer(world_ctx, &count);

    for (uint32_t i = 0; i < count && i < rec->body_count; i++) {
        B3BodyTrajectory* traj = &rec->trajectories[i];
        traj->body_id = transforms[i].body_id;
        
        B3FrameSample* sample = &traj->samples[frame_index];
        sample->frame = frame_index;
        memcpy(sample->position, transforms[i].position, sizeof(float) * 3);
        memcpy(sample->rotation, transforms[i].rotation, sizeof(float) * 4);
        traj->sample_count++;
    }
}

void b3_bridge_free_recorder(B3BakeRecorder* rec) {
    if (!rec) return;
    for (uint32_t i = 0; i < rec->body_count; i++) {
        free(rec->trajectories[i].samples);
    }
    free(rec->trajectories);
    free(rec);
}

```

---

### 2. High-Performance F-Curve Writer (Python)

Standard `obj.keyframe_insert()` forces full scene-graph evaluation on every call, making it too slow for large scenes. Pre-allocating `FCurve.keyframe_points` and using `foreach_set` inserts keyframes across all frames in a single vectorized memory pass.

```python
import bpy
import ctypes

def write_trajectory_to_action(obj, samples, start_frame):
    # Ensure animation data and active action exist
    if not obj.animation_data:
        obj.animation_data_create()
    
    action_name = f"{obj.name}_Box3D_Baked"
    action = bpy.data.actions.get(action_name) or bpy.data.actions.new(name=action_name)
    obj.animation_data.action = action
    
    # Ensure rotation mode is Quaternion
    obj.rotation_mode = 'QUATERNION'

    total_samples = len(samples)
    num_points = total_samples

    # Paths: location (3 channels: 0, 1, 2) and rotation_quaternion (4 channels: 0, 1, 2, 3)
    fcurves_loc = []
    for axis in range(3):
        fc = action.fcurves.find('location', index=axis) or action.fcurves.new('location', index=axis)
        fc.keyframe_points.add(num_points)
        fcurves_loc.append(fc)

    fcurves_rot = []
    for axis in range(4):
        fc = action.fcurves.find('rotation_quaternion', index=axis) or action.fcurves.new('rotation_quaternion', index=axis)
        fc.keyframe_points.add(num_points)
        fcurves_rot.append(fc)

    # Flatten coordinates: [frame_0, val_0, frame_1, val_1, ...]
    loc_coords = [[] for _ in range(3)]
    rot_coords = [[] for _ in range(4)]

    for idx, s in enumerate(samples):
        frame_num = start_frame + s.frame
        
        # Location channels
        loc_coords[0].extend([frame_num, s.position[0]])
        loc_coords[1].extend([frame_num, s.position[1]])
        loc_coords[2].extend([frame_num, s.position[2]])

        # Quaternion channels: Blender order is (W, X, Y, Z); Box3D is (X, Y, Z, W)
        rot_coords[0].extend([frame_num, s.rotation[3]]) # W
        rot_coords[1].extend([frame_num, s.rotation[0]]) # X
        rot_coords[2].extend([frame_num, s.rotation[1]]) # Y
        rot_coords[3].extend([frame_num, s.rotation[2]]) # Z

    # Bulk assign keyframe points
    for axis in range(3):
        fcurves_loc[axis].keyframe_points.foreach_set('co', loc_coords[axis])
        fcurves_loc[axis].update()

    for axis in range(4):
        fcurves_rot[axis].keyframe_points.foreach_set('co', rot_coords[axis])
        fcurves_rot[axis].update()

```

---

### 3. Keyframe Decimation & Curve Optimization

Raw physics trajectories produce a keyframe on every single frame. Applying an optional tolerance-based decimation pass cleans resting bodies and linear motion without visible deviation:

```python
def decimate_fcurves(action, error_threshold=0.0005):
    """
    Applies Blender's native F-Curve simplification algorithm 
    to remove redundant collinear points on baked actions.
    """
    for fc in action.fcurves:
        points = fc.keyframe_points
        if len(points) <= 2:
            continue

        # Set interpolation mode to LINEAR or BEZIER
        for pt in points:
            pt.interpolation = 'BEZIER'
            pt.handle_left_type = 'AUTO'
            pt.handle_right_type = 'AUTO'

    # Run Blender's native curve clean operator
    # Threshold determines max deviation before a keyframe is preserved
    for fc in action.fcurves:
        fc.convert_to_samples(0, 0) # Clear handles
        fc.keyframe_points.simplify(error_threshold)
        fc.update()

```

---

### 4. Headless Bake Operator (`OT_Box3DBakePhysics`)

```python
class OT_Box3DBakePhysics(bpy.types.Operator):
    bl_idname = "box3d.bake_physics"
    bl_label = "Bake Box3D Physics to Action"
    bl_description = "Run deterministic physics headlessly and bake keys to animation curves"
    bl_options = {'REGISTER', 'UNDO'}

    frame_start: bpy.props.IntProperty(name="Start Frame", default=1)
    frame_end: bpy.props.IntProperty(name="End Frame", default=250)
    substeps: bpy.props.IntProperty(name="Substeps per Frame", default=4, min=1, max=32)
    simplify_curves: bpy.props.BoolProperty(name="Simplify F-Curves", default=True)

    def execute(self, context):
        scene = context.scene
        fps = scene.render.fps / scene.render.fps_base
        dt = 1.0 / fps

        # 1. Initialize Box3D Native World
        bridge = ctypes.CDLL("./libbox3d_bridge.so")
        world_ctx = bridge.b3_bridge_create_world(ctypes.c_float(0.0), ctypes.c_float(0.0), ctypes.c_float(-9.81))

        # 2. Extract scene rigid bodies & setup mappings
        body_map = {} # {body_id: bpy.types.Object}
        physics_objects = [obj for obj in scene.objects if getattr(obj, "box3d_body", None) and obj.box3d_body.enabled]
        
        if not physics_objects:
            self.report({'WARNING'}, "No Box3D dynamic objects found in scene.")
            bridge.b3_bridge_destroy_world(world_ctx)
            return {'CANCELLED'}

        for obj in physics_objects:
            # Register body with native bridge (extracting initial transform/shapes)
            body_id = register_object_in_world(bridge, world_ctx, obj)
            body_map[body_id] = obj

        total_frames = (self.frame_end - self.frame_start) + 1
        recorder = bridge.b3_bridge_init_recorder(len(physics_objects), total_frames)

        # 3. Headless Fast Simulation Loop (No Viewport Lag)
        wm = context.window_manager
        wm.progress_begin(0, total_frames)

        for frame_idx in range(total_frames):
            # Step Box3D simulation
            bridge.b3_bridge_step(world_ctx, ctypes.c_float(dt), ctypes.c_int32(self.substeps))
            bridge.b3_bridge_record_frame(recorder, world_ctx, ctypes.c_uint32(frame_idx))
            wm.progress_update(frame_idx)

        wm.progress_end()

        # 4. Write trajectories back to Blender F-Curves
        for body_id, obj in body_map.items():
            traj_samples = bridge.b3_bridge_get_trajectory_samples(recorder, body_id)
            write_trajectory_to_action(obj, traj_samples, self.frame_start)
            
            if self.simplify_curves and obj.animation_data and obj.animation_data.action:
                decimate_fcurves(obj.animation_data.action)

        # 5. Cleanup
        bridge.b3_bridge_free_recorder(recorder)
        bridge.b3_bridge_destroy_world(world_ctx)

        self.report({'INFO'}, f"Baked {len(physics_objects)} objects over {total_frames} frames.")
        return {'FINISHED'}

```

---

### Workflow Features

* **Instant Scrubbing:** Baked curves sit directly inside Blender's Dope Sheet / Graph Editor, allowing timeline scrubbing without re-simulating.
* **Kinematic-to-Dynamic Transitions:** Animated kinematic objects follow their Blender F-Curves up to a trigger frame, after which dynamic state takes over and writes back to new keyframes.
* **Non-Destructive Layering:** Baked animations are isolated to individual `Action` datablocks, allowing animators to mix physics passes using the NLA (Non-Linear Animation) editor.