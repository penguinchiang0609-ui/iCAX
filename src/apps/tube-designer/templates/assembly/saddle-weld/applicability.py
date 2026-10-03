"""Template-owned, read-only local process applicability rules."""
import importlib.util
from pathlib import Path
import math

_path = Path(__file__).resolve().parents[2] / "_shared" / "assembly_applicability_geometry.py"
_spec = importlib.util.spec_from_file_location("icax_assembly_applicability_geometry", _path)
geometry = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(geometry)


@geometry.checked
def check_applicability(process_input, parameters):
    # end-profile accepts arbitrary closed outer contours. Their exact
    # intersection and retained end material are checked by the native host.
    main = geometry.part(process_input, "main")
    branch = geometry.part(process_input, "branch")
    geometry.standard_section(main)
    geometry.standard_section(branch)
    projection = abs(math.sin(math.radians(process_input["geometry"]["intersectionAngle"])))
    geometry.require(projection > 1e-6, "相贯端切的支管轴线不能与主管平行")
