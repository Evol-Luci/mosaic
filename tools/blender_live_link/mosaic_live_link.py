"""Mosaic Live-Link — streams live transform edits from Blender to Mosaic Studio.

One-way (Blender -> Studio), transform-only. See
docs/superpowers/specs/2026-09-13-blender-live-link-design.md for the wire protocol
and axis-conversion rationale.
"""

bl_info = {
    "name": "Mosaic Live-Link",
    "author": "Mosaic",
    "version": (1, 0, 0),
    "blender": (4, 0, 0),
    "location": "View3D > Sidebar > Mosaic",
    "description": "Streams live transform edits to Mosaic Studio",
    "category": "Object",
}

import json
import socket

import bpy


def _to_mosaic_position(v):
    # Blender is right-handed Z-up; Mosaic is right-handed Y-up.
    return [v[0], v[2], -v[1]]


def _to_mosaic_rotation(q):
    # Same basis change applied to the quaternion (w unchanged).
    return [q[1], q[3], -q[2], q[0]]  # Blender Quaternion is (w, x, y, z)


class MosaicLiveLinkState:
    sock = None
    connected = False


_state = MosaicLiveLinkState()


def _send_line(obj):
    if _state.sock is None:
        return
    loc = obj.location
    rot = obj.rotation_quaternion if obj.rotation_mode == 'QUATERNION' else obj.rotation_euler.to_quaternion()
    scale = obj.scale
    message = {
        "name": obj.name,
        "position": _to_mosaic_position([loc.x, loc.y, loc.z]),
        "rotation": _to_mosaic_rotation([rot.w, rot.x, rot.y, rot.z]),
        "scale": [scale.x, scale.z, scale.y],
    }
    try:
        _state.sock.sendall((json.dumps(message) + "\n").encode("utf-8"))
    except OSError:
        _state.connected = False
        _state.sock = None


def _on_depsgraph_update(scene, depsgraph):
    for update in depsgraph.updates:
        obj = update.id
        if isinstance(obj, bpy.types.Object) and update.is_updated_transform:
            _send_line(obj)


class MOSAIC_OT_live_link_connect(bpy.types.Operator):
    bl_idname = "mosaic.live_link_connect"
    bl_label = "Connect"

    def execute(self, context):
        props = context.scene.mosaic_live_link
        try:
            s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            s.connect((props.host, props.port))
            _state.sock = s
            _state.connected = True
            if _on_depsgraph_update not in bpy.app.handlers.depsgraph_update_post:
                bpy.app.handlers.depsgraph_update_post.append(_on_depsgraph_update)
        except OSError as e:
            self.report({'ERROR'}, f"Mosaic Live-Link: {e}")
            _state.connected = False
        return {'FINISHED'}


class MOSAIC_OT_live_link_disconnect(bpy.types.Operator):
    bl_idname = "mosaic.live_link_disconnect"
    bl_label = "Disconnect"

    def execute(self, context):
        if _on_depsgraph_update in bpy.app.handlers.depsgraph_update_post:
            bpy.app.handlers.depsgraph_update_post.remove(_on_depsgraph_update)
        if _state.sock is not None:
            _state.sock.close()
        _state.sock = None
        _state.connected = False
        return {'FINISHED'}


class MOSAIC_PT_live_link_panel(bpy.types.Panel):
    bl_label = "Mosaic Live-Link"
    bl_space_type = 'VIEW_3D'
    bl_region_type = 'UI'
    bl_category = "Mosaic"

    def draw(self, context):
        layout = self.layout
        props = context.scene.mosaic_live_link
        layout.prop(props, "host")
        layout.prop(props, "port")
        if _state.connected:
            layout.label(text="Connected", icon='CHECKMARK')
            layout.operator(MOSAIC_OT_live_link_disconnect.bl_idname)
        else:
            layout.operator(MOSAIC_OT_live_link_connect.bl_idname)


class MosaicLiveLinkProperties(bpy.types.PropertyGroup):
    host: bpy.props.StringProperty(name="Host", default="127.0.0.1")
    port: bpy.props.IntProperty(name="Port", default=6828, min=1, max=65535)


_classes = (
    MosaicLiveLinkProperties,
    MOSAIC_OT_live_link_connect,
    MOSAIC_OT_live_link_disconnect,
    MOSAIC_PT_live_link_panel,
)


def register():
    for cls in _classes:
        bpy.utils.register_class(cls)
    bpy.types.Scene.mosaic_live_link = bpy.props.PointerProperty(type=MosaicLiveLinkProperties)


def unregister():
    if _on_depsgraph_update in bpy.app.handlers.depsgraph_update_post:
        bpy.app.handlers.depsgraph_update_post.remove(_on_depsgraph_update)
    if _state.sock is not None:
        _state.sock.close()
    del bpy.types.Scene.mosaic_live_link
    for cls in reversed(_classes):
        bpy.utils.unregister_class(cls)
