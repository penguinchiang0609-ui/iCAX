"""Construct a feasible product directly from process inequalities."""
import importlib.util
from pathlib import Path

spec = importlib.util.spec_from_file_location("icax_corner_examples", Path(__file__).resolve().parents[2] / "_shared" / "assembly_example_product.py")
examples = importlib.util.module_from_spec(spec)
spec.loader.exec_module(examples)


def get_example_product(parameters):
    product = examples.create("orthogonal-corner")
    joint = parameters["cJoint"]
    insertion = parameters["insertDepth"] if joint == "insert" else parameters["tabLength"] if joint == "tabs" else 12
    gap = parameters["fitGap"] if joint == "insert" else parameters["sideClearance"] if joint == "tabs" else 0
    wall = min(3, insertion / 4)
    c_wall = min(2, insertion / 4) if joint == "tabs" else 2
    c_width = max(20, parameters["tabWidth"] + 3*c_wall + 4*gap + 2) if joint == "tabs" else 20
    c_depth = max(20, parameters["tabWidth"] + 3*c_wall + 4*gap + 2) if joint == "tabs" else 20
    depth = max(40, c_width + 2*gap + 12, c_depth + 2*gap + 12)
    if joint == "tabs":
        depth = max(depth, insertion + wall + 5)
    width = max(80, 2 * (insertion + 5))
    for letter in "AB":
        examples.rectangular(product, "arm" + letter, width, depth, wall, max(260, 2*depth+20))
    examples.rectangular(product, "armC", c_width, c_depth, c_wall, max(220, width+20))
    return product
