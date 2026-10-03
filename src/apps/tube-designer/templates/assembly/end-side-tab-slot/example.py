"""Template-owned example finished product; manufacturing is requested separately."""
import importlib.util
from pathlib import Path

_path = Path(__file__).resolve().parents[2] / "_shared" / "assembly_example_product.py"
_spec = importlib.util.spec_from_file_location("icax_assembly_example_product", _path)
examples = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(examples)


def get_example_product(parameters):
    product = examples.create("t")
    width, insertion, clearance = (parameters[key] for key in
                                  ("tabWidth", "tabLength", "sideClearance"))
    wall = min(2, insertion / 4)
    branch_width = max(40, width + 8)
    branch_depth = max(40, width + 8) if parameters["pairCount"] == "four" else 40
    host_width = max(80, branch_width + 2 * clearance + 8, width + 2 * clearance + 10)
    host_depth = max(40, insertion + wall + 5)
    examples.rectangular(product, "main", host_width, host_depth, wall)
    examples.rectangular(product, "branch", branch_width, branch_depth, wall)
    return product
