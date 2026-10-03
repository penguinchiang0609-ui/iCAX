"""Template-owned example finished product; manufacturing is requested separately."""
import importlib.util
from pathlib import Path

_path = Path(__file__).resolve().parents[2] / "_shared" / "assembly_example_product.py"
_spec = importlib.util.spec_from_file_location("icax_assembly_example_product", _path)
examples = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(examples)


def get_example_product(parameters):
    product = examples.create("l")
    radius = parameters["bendRadius"]
    if radius < 1.5:
        raise ValueError("分段折弯装配半径不得小于 1.5 mm，当前默认槽组需要足够的剩余间隔")
    product["parameters"]["angle"] = max(1, min(90, 50000 / radius))
    if radius < 22:
        # Six slots at 170 degrees leave >1 mm lands at Ri=1.5 with this
        # valid 1.11 mm section; larger stock would consume those lands.
        for span_id in ("armA", "armB"):
            examples.rectangular(product, span_id, 1.11, 1.11, 0.1)
        product["parameters"]["angle"] = 170
    return product
