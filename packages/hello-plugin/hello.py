"""A minimal example plugin: one menu item that logs a message."""

MOSAIC_MENU_ITEMS = {"Say Hello (from hello-plugin)": "on_say_hello"}


def on_say_hello():
    print("Hello from the hello-plugin package!")
