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
    host = geometry.part(process_input, 'memberA')
    branch = geometry.part(process_input, 'memberB')
    if not parameters['maleFemale']:
        return
    h, b = geometry.standard_section(host), geometry.standard_section(branch)
    width = geometry.positive(parameters['tabWidth'], '插舌宽度')
    insertion = geometry.positive(parameters['tabLength'], '插舌长度')
    geometry.number(parameters['sideClearance'], '母槽单边间隙', 0)
    geometry.number(parameters.get('tabAngle',0), '插舌周向角')
    geometry.require(parameters['pairCount'] in ('two','four'), '插舌方位数量无效')
    geometry.require(insertion > width/2+.01, '圆弧插舌长度须大于半宽')
    geometry.require(branch['length']+insertion<=100000, '支管插舌所需下料补长超出允许长度')
    if h is not None:
        geometry.require(h['wall']+.01 < insertion < h['depth']-h['wall']-.01,
                         '插舌长度必须穿过主管近侧壁，且不能碰到对侧内壁')
