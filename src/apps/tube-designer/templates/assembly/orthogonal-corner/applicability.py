"""Template-owned applicability; pure local process data, no generation."""
import importlib.util
from pathlib import Path

spec = importlib.util.spec_from_file_location("icax_corner_geometry", Path(__file__).with_name("geometry.py"))
geometry = importlib.util.module_from_spec(spec)
spec.loader.exec_module(geometry)


@geometry.queries.checked
def check_applicability(process_input, parameters):
    parts = [geometry.queries.part(process_input, "member" + letter) for letter in "ABC"]
    axes = [geometry.queries.axis(part) for part in parts]
    geometry.queries.require(all(abs(geometry.queries.dot(axes[a], axes[b])) < 1e-6
                                 for a, b in ((0, 1), (0, 2), (1, 2))),
                             "三向节点需要实际互相垂直的局部管段")
    geometry.process_sizes(process_input, parameters)
