"""Read-only declaration audit. Never imports or executes build_model.py."""
import ast
import hashlib
import json
from pathlib import Path
import struct
import xml.etree.ElementTree as ET

root = Path(__file__).resolve().parents[2] / "apps/five-axis-cam/resources/machines/industrial-ac"
script = ast.parse((root / "build_model.py").read_text(encoding="utf-8"))
assignments = {target.id: node.value for node in script.body if isinstance(node, ast.Assign) for target in node.targets if isinstance(target, ast.Name)}
colliders = assignments["COLLIDERS"]
head_declaration = next(ast.literal_eval(value) for key, value in zip(colliders.keys, colliders.values) if ast.literal_eval(key) == "laser_head")
names = ast.literal_eval(assignments["COLLIDER_NAMES"])["laser_head"]
urdf_path = root / "LC-3015.urdf"
before = urdf_path.read_bytes()
robot = ET.fromstring(before)
head = robot.find("link[@name='A 激光切割头']")
actual = head.findall("collision")
assert len(actual) == len(head_declaration) == len(names) == 6
for element, name, (position, size) in zip(actual, names, head_declaration):
    assert element.get("name") == name
    for text, expected in ((element.find("origin").get("xyz"), position), (element.find("geometry/box").get("size"), size)):
        assert all(abs(float(value) * 1000 - target) < 1e-8 for value, target in zip(text.split(), expected))
subelements = [node for node in ast.walk(script) if isinstance(node, ast.Call) and isinstance(node.func, ast.Attribute) and node.func.attr == "SubElement"]
tool = next(node for node in subelements if len(node.args) > 1 and isinstance(node.args[1], ast.Constant) and node.args[1].value == "icax_tool")
tool_attributes = {item.arg: ast.literal_eval(item.value) for item in tool.keywords}
assert tool_attributes["tcp_mm"] == head.find("icax_tool").get("tcp_mm") == "0 0 -220"
assert tool_attributes["beam"] == head.find("icax_tool").get("beam") == "0 0 -1"
assert any(len(node.args) > 1 and isinstance(node.args[1], ast.Constant) and node.args[1].value == "icax_work_support" for node in subelements)
assert robot.find("link[@name='工作台']/icax_work_support") is not None
z_call = next(node for node in ast.walk(script) if isinstance(node, ast.Call) and isinstance(node.func, ast.Name) and node.func.id == "joint" and node.args and isinstance(node.args[0], ast.Constant) and node.args[0].value == "Z")
home = next(ast.literal_eval(item.value) for item in z_call.keywords if item.arg == "home")
assert home == float(robot.find("joint[@name='Z']/icax_home").get("position")) == .15
triangles = sum(struct.unpack_from("<I", (root / visual.find("geometry/mesh").get("filename")).read_bytes(), 80)[0] for visual in head.findall("visual"))
assert triangles == 6278
assert urdf_path.read_bytes() == before
print(json.dumps({"passed": True, "generatorExecuted": False, "urdfWritten": False, "headBoxDeclarationsMatch": 6, "physicalTCPMm": [0, 0, -220], "zHomeMm": 150, "headVisualTriangles": triangles, "urdfSha256": hashlib.sha256(before).hexdigest()}, ensure_ascii=False))
