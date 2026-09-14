MOSAIC_MENU_ITEMS = {"Show World Info": "on_show_world_info"}

# Screen-space UiText has no built-in scrolling/clipping (not yet an engine
# feature), so instead of one long line we cap the visible list to a fixed
# number of rows that reliably fits the viewport, one UiText entity per row,
# stacked top-to-bottom. If the world has more entities than that, the last
# row says how many were left out.
_MAX_ROWS = 10
_ROW_HEIGHT = 0.04
_FIRST_ROW_Y = 0.20
_ROW_SPACING = 0.045
_MAX_COMPONENTS_SHOWN = 3

_header_entity = None
_row_entities = []
_visible = False


def _ensure_text_entity(existing):
    if existing is not None and is_valid(existing):
        return existing
    e = create_entity()
    add_component(e, "UiText")
    return e


def _destroy_display_entities():
    global _header_entity, _row_entities
    if _header_entity is not None and is_valid(_header_entity):
        destroy_entity(_header_entity)
    for e in _row_entities:
        if is_valid(e):
            destroy_entity(e)
    _header_entity = None
    _row_entities = []


def _set_row_text(e, y, text, color):
    set_component(e, "UiText", {
        "font": "f0000000-0000-0000-0000-000000000000",
        "text": text,
        "anchor": 2,  # TopRight -- its own column, clear of the other plugins' left-side status lines
        "pos": {"x": 0.05, "y": y},  # offset inward from the right edge
        "height": _ROW_HEIGHT,
        "color": color,
        "alpha": 1.0 if text else 0.0,
        "order": 10,
    })


def _describe(e):
    if has_component(e, "Name"):
        label = get_component(e, "Name")["value"]
    else:
        label = "#" + str(e)
    names = component_names(e)
    shown = names[:_MAX_COMPONENTS_SHOWN]
    suffix = "" if len(names) <= _MAX_COMPONENTS_SHOWN else "+" + str(len(names) - _MAX_COMPONENTS_SHOWN)
    return label + ": [" + ",".join(shown) + suffix + "]"


def on_show_world_info():
    global _header_entity, _row_entities, _visible

    # Second click while the on-screen info is already showing: tear it back down
    # instead of just redrawing the same text. This leaves the (unrelated) World
    # Inspector panel and its own info untouched -- only this menu item's own
    # UiText entities are destroyed.
    if _visible:
        _destroy_display_entities()
        _visible = False
        return
    _visible = True

    # Ensure our own display entities already exist BEFORE snapshotting the
    # world, so entity_count()/entities() below reflect a stable state and
    # never undercount by however many new entities this call itself would
    # otherwise still be about to create.
    _header_entity = _ensure_text_entity(_header_entity)
    while len(_row_entities) < _MAX_ROWS:
        _row_entities.append(_ensure_text_entity(None))

    count = entity_count()
    all_entities = entities()

    _set_row_text(_header_entity, _FIRST_ROW_Y - _ROW_SPACING,
                  "Entities: " + str(count), {"x": 1.0, "y": 1.0, "z": 1.0})

    max_listed = _MAX_ROWS - 1 if count > _MAX_ROWS else _MAX_ROWS
    listed = all_entities[:max_listed]

    for i in range(_MAX_ROWS):
        row_e = _row_entities[i]
        y = _FIRST_ROW_Y + i * _ROW_SPACING
        if i < len(listed):
            _set_row_text(row_e, y, _describe(listed[i]), {"x": 0.6, "y": 1.0, "z": 1.0})
        elif i == len(listed) and count > max_listed:
            remaining = count - max_listed
            _set_row_text(row_e, y, "... and " + str(remaining) + " more", {"x": 0.7, "y": 0.7, "z": 0.7})
        else:
            _set_row_text(row_e, y, "", {"x": 0.0, "y": 0.0, "z": 0.0})
