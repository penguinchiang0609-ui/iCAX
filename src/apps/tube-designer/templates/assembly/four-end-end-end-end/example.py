"""Template-owned example finished product; manufacturing is requested separately."""
import importlib.util
import math
from pathlib import Path

_path = Path(__file__).resolve().parents[2] / "_shared" / "assembly_example_product.py"
_spec = importlib.util.spec_from_file_location("icax_assembly_example_product", _path)
examples = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(examples)


def get_example_product(parameters):
    product = examples.create("four-way")
    slope = abs(math.tan(math.radians(parameters["miterAngle"])))
    for span in product["spans"].values():
        # Both cut orientations fit when stock exceeds the larger support.
        reach = slope * max(span["parameters"]["width"], span["parameters"]["depth"])
        span["length"] = max(span["length"], reach + 20)
    return product
