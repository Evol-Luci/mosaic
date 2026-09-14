import csv
import json

MOSAIC_IMPORTERS = {".csv": "import_csv"}


def import_csv(source_path):
    with open(source_path, newline="") as f:
        rows = list(csv.reader(f))
    return json.dumps(rows)
