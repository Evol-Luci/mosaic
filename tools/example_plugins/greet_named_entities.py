MOSAIC_MENU_ITEMS = {"Greet Named Entities": "on_greet_named_entities"}

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
        "pos": {"x": 0.05, "y": 0.38},
        "height": 0.05,
        "color": {"x": 1.0, "y": 0.8, "z": 0.4},
        "alpha": 1.0,
        "order": 10,
    })


def on_greet_named_entities():
    greeted = 0
    for e in entities_with("Name"):
        current = get_component(e, "Name")
        if current is None:
            continue
        value = current["value"]
        if not value.endswith(" (hi!)"):
            set_component(e, "Name", {"value": value + " (hi!)"})
            greeted += 1
    _set_status("Greeted " + str(greeted) + " named entities")
