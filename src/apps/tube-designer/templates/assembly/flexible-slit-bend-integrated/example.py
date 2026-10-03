"""Template-owned example finished product; manufacturing is requested separately."""
import importlib.util
import math
from pathlib import Path

_path = Path(__file__).resolve().parents[2] / "_shared" / "assembly_example_product.py"
_spec = importlib.util.spec_from_file_location("icax_assembly_example_product", _path)
examples = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(examples)


def get_example_product(parameters):
    product = examples.create("l")
    # The inherited six 1 mm slits need 1 mm lands: R*beta >= 14 mm.
    # Select beta directly; small radii need a larger angle, not an invalid
    # section below the system rectangular profile's minimum dimensions.
    radius = parameters["bendRadius"]
    if radius < 5:
        raise ValueError("柔性缝装配半径不得小于 5 mm，当前默认槽组需要足够的筋宽")
    size = min(40, max(2, radius))
    for span_id in ("armA", "armB"):
        examples.rectangular(product, span_id, size, size, min(2, size / 10))
    product["parameters"]["angle"] = max(1, min(170, max(math.degrees(14.01 / radius),
                                                     min(90, 50000 / radius))))
    return product
