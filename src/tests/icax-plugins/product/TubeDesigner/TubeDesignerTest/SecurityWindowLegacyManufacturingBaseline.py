"""Generate a legacy reference from the exact native lifecycle host input."""
from copy import deepcopy
import json
from pathlib import Path
import sys
from unittest.mock import patch

from WindowCatalogueTests import package


def generate_baseline(request):
    descriptor, _, template = package("single_face_security_window")
    if request["templateId"] != descriptor["id"]:
        raise ValueError("This reference generator covers the security window lifecycle")
    values = deepcopy(request["parameters"])
    before = deepcopy(values)
    context = {"template": deepcopy(request["template"]), "geometryPurpose": "manufacturing"}
    with patch.object(template, "manufacturing", side_effect=AssertionError("The reference must use legacy generate")):
        document = template.generate(values, context)
    assert values == before, "Legacy reference generation mutated the actual host input"
    assert document["parameters"] == before, "Legacy reference returned different host parameters"
    assert document["schema"] == "icax.neutral-model" and document["schemaVersion"] == 2
    assert any(resource["operator"] == "boolean" for resource in document["resources"]), \
        "The reference must contain actual legacy machining geometry"
    return document


if __name__ == "__main__":
    request_path, output_path = map(Path, sys.argv[1:])
    request = json.loads(request_path.read_text(encoding="utf-8"))
    document = generate_baseline(request)
    output_path.parent.mkdir(parents=True, exist_ok=True)
    output_path.write_text(json.dumps(document, ensure_ascii=False, separators=(",", ":")), encoding="utf-8")
    print(json.dumps({"legacyEntry": "generate", "templateId": request["templateId"],
                      "parts": len(document["items"]), "hostInputEqual": True}))
