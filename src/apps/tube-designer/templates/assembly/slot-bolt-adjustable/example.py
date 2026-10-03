"""Template-owned example finished product; manufacturing is requested separately."""
import importlib.util
from pathlib import Path

_path = Path(__file__).resolve().parents[2] / "_shared" / "assembly_example_product.py"
_spec = importlib.util.spec_from_file_location("icax_assembly_example_product", _path)
examples = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(examples)


def get_example_product(parameters):
    product = examples.create("parallel")
    diameter = parameters["boltDiameter"] + parameters["holeClearance"]
    slot_length = diameter + parameters["adjustment"]
    along, across = (slot_length, diameter) if parameters["adjustAxis"] == "x" else (diameter, slot_length)
    length = max(440, along + 20)
    for span_id in ("lower", "upper"):
        examples.rectangular(product, span_id, max(80, across + 10), 20, length=length)
    product["parameters"]["overlapLength"] = length
    return product
