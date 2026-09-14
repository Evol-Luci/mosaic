MOSAIC_MENU_ITEMS = {"Emit Test Event": "on_emit_test_event"}

_status_entity = None
_count = 0


def _set_status(text):
    global _status_entity
    if _status_entity is None or not is_valid(_status_entity):
        _status_entity = create_entity()
        add_component(_status_entity, "UiText")
    set_component(_status_entity, "UiText", {
        "font": "f0000000-0000-0000-0000-000000000000",
        "text": text,
        "anchor": 0,
        "pos": {"x": 0.05, "y": 0.70},
        "height": 0.05,
        "color": {"x": 0.6, "y": 0.6, "z": 1.0},
        "alpha": 1.0,
        "order": 10,
    })


def on_emit_test_event():
    global _count
    _count += 1
    emit("plugin_test_event", {"source": "event_demo", "count": _count})
    _set_status("Emitted plugin_test_event x" + str(_count))
