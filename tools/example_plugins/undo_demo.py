MOSAIC_MENU_ITEMS = {
    "Nudge Marker Right": "on_nudge_marker",
    "Undo Last Change": "on_undo_last_change",
    "Redo Last Undo": "on_redo_last_undo",
}

_marker_entity = None
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
        "pos": {"x": 0.05, "y": 0.46},
        "height": 0.05,
        "color": {"x": 1.0, "y": 0.5, "z": 1.0},
        "alpha": 1.0,
        "order": 10,
    })


def _ensure_marker():
    global _marker_entity
    if _marker_entity is None or not is_valid(_marker_entity):
        _marker_entity = create_entity()
        add_component(_marker_entity, "Transform")
        add_component(_marker_entity, "MeshRenderer")
        add_component(_marker_entity, "Material")
        set_component(_marker_entity, "Transform", {
            "position": {"x": -3.0, "y": 1.0, "z": -3.0},
            "scale": {"x": 0.4, "y": 0.4, "z": 0.4},
        })
        set_component(_marker_entity, "MeshRenderer", {
            "mesh": "cbe00000-0000-0000-0000-000000000000",
        })
        set_component(_marker_entity, "Material", {
            "base_color": {"x": 1.0, "y": 0.2, "z": 0.8},
        })
    return _marker_entity


def on_nudge_marker():
    e = _ensure_marker()
    t = get_component(e, "Transform")
    pos = t["position"]
    set_component(e, "Transform", {
        "position": {"x": pos["x"] + 1.0, "y": pos["y"], "z": pos["z"]},
    })
    _set_status("Nudged marker. undo_depth=" + str(undo_depth()))


def on_undo_last_change():
    ok = undo()
    _set_status("undo() -> " + str(ok) + ". undo_depth=" + str(undo_depth()))


def on_redo_last_undo():
    ok = redo()
    _set_status("redo() -> " + str(ok) + ". undo_depth=" + str(undo_depth()))
