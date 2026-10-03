"""Template-owned example finished product; manufacturing is requested separately."""
import importlib.util
from pathlib import Path

_path = Path(__file__).resolve().parents[2] / "_shared" / "assembly_example_product.py"
_spec = importlib.util.spec_from_file_location("icax_assembly_example_product", _path)
examples = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(examples)


def get_example_product(parameters):
    product = examples.create("straight")
    width = max(60, parameters["nominalWidth"] + 2 * parameters["fitClearance"] + 10)
    length = max(280, parameters["straightDepth"] + parameters["nominalWidth"] / 2 + parameters["fitClearance"] + 20)
    for span_id in ("first", "second"):
        examples.rectangular(product, span_id, width, 40, length=length)
    return product
