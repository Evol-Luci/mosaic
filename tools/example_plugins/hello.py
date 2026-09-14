MOSAIC_MENU_ITEMS = {"Say Hello": "on_say_hello"}

_hello_entity = None

def on_say_hello():
    global _hello_entity
    if _hello_entity is None or not is_valid(_hello_entity):
        _hello_entity = create_entity()
        add_component(_hello_entity, "UiText")
    ok = set_component(_hello_entity, "UiText", {
        "font": "f0000000-0000-0000-0000-000000000000",
        "text": "Hello World!",
        "anchor": 0,
        "pos": {"x": 0.05, "y": 0.5},
        "height": 0.08,
        "color": {"x": 1.0, "y": 1.0, "z": 0.0},
        "alpha": 1.0,
        "order": 10,
    })
    print("set_component ok =", ok)
