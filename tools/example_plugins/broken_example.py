# INTENTIONALLY BROKEN -- demonstrates PluginHost's error-isolation guarantee.
#
# This file has a real Python syntax error on purpose. When Studio loads
# plugins (on launch, or via "Reload Plugins"), this file will fail to load
# and its error will be logged to the Console panel under the "plugin"
# category -- but every other plugin in this directory keeps working
# normally. A broken plugin never blocks its siblings and never crashes
# Studio.

MOSAIC_MENU_ITEMS = {"This Never Loads": "on_never_runs"}

def on_never_runs(
    print("unreachable")
