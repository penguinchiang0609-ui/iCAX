"""Build encrypted assembly import samples from a current built-in template."""
import importlib.util
import io
import json
from pathlib import Path
import sys
import zipfile

repo, output = (Path(value).resolve() for value in sys.argv[1:3])
output.mkdir(parents=True, exist_ok=True)
shared = repo / "src/apps/tube-designer/templates/_shared"
spec = importlib.util.spec_from_file_location("package_zip_writer", shared / "product_template_package_runtime.py")
writer = importlib.util.module_from_spec(spec)
spec.loader.exec_module(writer)
source = repo / "src/apps/tube-designer/templates/assembly/mechanical-fastener"
original = json.loads((source / "assembly.json").read_text(encoding="utf-8"))
descriptor = {**original, "id": "user-native-fastener", "displayName": "外部装配导入测试"}
entries = [("assembly.json", json.dumps(descriptor, ensure_ascii=False).encode("utf-8"))]
entries += [(path.name, path.read_bytes()) for path in sorted(source.glob("*.py"))]


def save(name, values):
    (output / name).write_bytes(writer._build_encrypted_zip(values))


save("valid.itat", entries)
save("builtin-duplicate.itat", [("assembly.json", (source / "assembly.json").read_bytes()), *entries[1:]])
save("missing-example.itat", [entry for entry in entries if entry[0] != "example.py"])
save("invalid-schema.itat", [("assembly.json", b'{"schema":"invalid","id":"bad"}'), *entries[1:]])
save("unsafe-path.itat", [*entries, ("../escape.py", b"pass")])
invalid_example_descriptor = {**descriptor, "id": "user-invalid-example"}
save("invalid-example.itat", [(name,
    json.dumps(invalid_example_descriptor, ensure_ascii=False).encode("utf-8") if name == "assembly.json"
    else b"def get_example_product(parameters):\n    return {}\n" if name == "example.py" else value)
    for name, value in entries])
unprotected = io.BytesIO()
with zipfile.ZipFile(unprotected, "w", zipfile.ZIP_DEFLATED) as archive:
    for name, value in entries:
        archive.writestr(name, value)
(output / "unprotected.itat").write_bytes(unprotected.getvalue())
(output / "corrupt.itat").write_bytes(b"invalid ZIP bytes")
print(json.dumps({"fixture": str(output / "valid.itat"), "templateId": descriptor["id"]}))
