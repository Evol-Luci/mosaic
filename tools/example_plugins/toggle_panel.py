MOSAIC_MENU_ITEMS = {"Toggle Debug Panel": "on_toggle_debug_panel"}

_panel_entity = None


def on_toggle_debug_panel():
    global _panel_entity
    if _panel_entity is None or not is_valid(_panel_entity):
        _panel_entity = create_entity()

    if has_component(_panel_entity, "UiPanel"):
        remove_component(_panel_entity, "UiPanel")
    else:
        add_component(_panel_entity, "UiPanel")
        set_component(_panel_entity, "UiPanel", {
            "anchor": 6,  # BottomLeft -- stays clear of the stacked status text lines
            "pos": {"x": 0.05, "y": 0.05},
            "size": {"x": 0.3, "y": 0.15},
            "color": {"x": 0.9, "y": 0.2, "z": 0.2},
            "alpha": 0.7,
            "order": 5,
        })
