MOSAIC_MENU_ITEMS = {
    "Spawn Debug Cube": "on_spawn_cube",
    "Despawn Debug Cube": "on_despawn_cube",
}

_cube_entity = None
_status_entity = None


def _set_status(text):
    global _status_entity
    if _status_entity is None or not is_valid(_status_entity):
        _status_entity = create_entity()
        add_component(_status_entity, "UiText")
    set_component(_status_entity, "UiText", {
        "font": "f0000000-0000-0000-0000-000000000000",
        "text": text,
        "anchor": 0,
        "pos": {"x": 0.05, "y": 0.62},
        "height": 0.05,
        "color": {"x": 0.5, "y": 1.0, "z": 0.5},
        "alpha": 1.0,
        "order": 10,
    })


def on_spawn_cube():
    global _cube_entity
    if _cube_entity is not None and is_valid(_cube_entity):
        _set_status("Cube already spawned (#" + str(_cube_entity) + ")")
        return
    _cube_entity = create_entity()
    add_component(_cube_entity, "Transform")
    add_component(_cube_entity, "MeshRenderer")
    add_component(_cube_entity, "Material")
    set_component(_cube_entity, "Transform", {
        "position": {"x": 4.0, "y": 1.0, "z": -3.0},
    })
    set_component(_cube_entity, "MeshRenderer", {
        "mesh": "cbe00000-0000-0000-0000-000000000000",
    })
    set_component(_cube_entity, "Material", {
        "base_color": {"x": 1.0, "y": 0.85, "z": 0.1},
    })
    _set_status("Spawned debug cube #" + str(_cube_entity))


def on_despawn_cube():
    global _cube_entity
    if _cube_entity is None or not is_valid(_cube_entity):
        _set_status("No debug cube to despawn")
        return
    destroy_entity(_cube_entity)
    _set_status("Despawned debug cube #" + str(_cube_entity))
    _cube_entity = None
