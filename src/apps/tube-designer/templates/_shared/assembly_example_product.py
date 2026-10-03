"""Product-data primitives for template-owned examples; no process selection."""
import importlib.util
from pathlib import Path

_spec = importlib.util.spec_from_file_location(
    "icax_example_finished_product_runtime", Path(__file__).with_name("finished_product_runtime.py"))
products = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(products)


def create(shape_id):
    return products.create(shape_id)


def rectangular(product, span_id, width, depth, wall=2, length=None):
    span = product["spans"][span_id]
    span["profileRef"] = {"scope": "system", "id": "rect"}
    span["parameters"] = {"width": width, "depth": depth, "wallThickness": wall,
                          "cornerRadius": min(3, min(width, depth) / 10),
                          "innerRadius": min(1, min(width, depth) / 20)}
    if length is not None:
        span["length"] = length


def round_tube(product, span_id, diameter, wall=2, length=None):
    span = product["spans"][span_id]
    span["profileRef"] = {"scope": "system", "id": "round"}
    span["parameters"] = {"width": diameter, "wallThickness": wall,
                          "innerOffsetX": 0, "innerOffsetY": 0}
    if length is not None:
        span["length"] = length
