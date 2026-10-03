"""Template-owned example finished product; manufacturing is requested separately."""
import importlib.util
from pathlib import Path

_path = Path(__file__).resolve().parents[2] / "_shared" / "assembly_example_product.py"
_spec = importlib.util.spec_from_file_location("icax_assembly_example_product", _path)
examples = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(examples)


def get_example_product(parameters):
    product = examples.create("t")
    if True:
        insertion = parameters["insertDepth"]
        wall = min(3, insertion / 4)
        examples.rectangular(product, "main", 80, max(40, insertion + wall + 5), wall)
    return product
