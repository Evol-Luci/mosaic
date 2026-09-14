MOSAIC_PANELS = {"World Inspector": "draw_world_inspector"}


def draw_world_inspector():
    imgui_text("Entities: " + str(entity_count()))
    imgui_separator()
    imgui_begin_child("world_inspector_scroll", 300.0)
    for e in entities():
        if has_component(e, "Name"):
            label = get_component(e, "Name")["value"]
        else:
            label = "#" + str(e)
        names = component_names(e)
        imgui_text(label + ": [" + ",".join(names) + "]")
    imgui_end_child()
