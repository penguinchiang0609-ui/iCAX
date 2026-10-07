"""Manufacturing inputs. Product scripts declare; assembly execution resolves."""
from __future__ import annotations

from contextlib import contextmanager
from contextvars import ContextVar
from copy import deepcopy
import json
import math
from pathlib import Path
import re
from typing import Any

from .display import _hierarchy
from .resources import _nodes, _placement, _validate_graph, _compose, to_resource_model

SCHEMA = "icax.manufacturing-model"
DECLARATIONS = "tubeDesigner.manufacturingProcessDeclarations"
_FIELDS = {"schema", "schemaVersion", "coordinateSystem", "lengthUnit", "resources", "items", "roots", "sourceMappings", "processes"}
_DECLARATION_FIELDS = {"schema", "schemaVersion", "connections", "processes"}
_EXECUTION_CONTEXT = ContextVar("icax_manufacturing_environment", default={})
_DECLARING = ContextVar("icax_manufacturing_declaration", default=False)
_RESULT_FIELDS = {"applicable", "functionDigest", "executionSignature", "resolvedPlan", "resultGeometry", "targetGeometry", "planDigest",
    "manufacturing.sourceMembers", "tubeDesigner.frameManufacturing", "tubeDesigner.cornerProcess",
    "tubeDesigner.jointProcess", "tubeDesigner.endProcess", "tubeDesigner.sourceSpans"}


@contextmanager
def _execution_scope(context):
    token = _EXECUTION_CONTEXT.set(deepcopy(context))
    try:
        yield
    finally:
        _EXECUTION_CONTEXT.reset(token)


@contextmanager
def manufacturing_declaration():
    """Collect pending calls without importing or executing machining providers."""
    token = _DECLARING.set(True)
    try:
        yield
    finally:
        _DECLARING.reset(token)


def is_manufacturing_declaration():
    return _DECLARING.get()


def manufacturing_context(template_file: str) -> dict[str, Any]:
    context = deepcopy(_EXECUTION_CONTEXT.get())
    descriptor = json.loads(Path(template_file).with_name("template.json").read_text(encoding="utf-8"))
    context["geometryPurpose"] = "manufacturing"
    context.setdefault("template", {"id": descriptor["id"], "version": descriptor["version"]})
    return context


def _key(value, name):
    if not isinstance(value, str) or re.fullmatch(r"[A-Za-z0-9_][A-Za-z0-9_.:-]*", value) is None:
        raise ValueError(f"invalid manufacturing {name}")
    return value


def validate_assembly_resource_ref(reference):
    """Validate a scoped resource identity and its optional current package pins."""
    if not isinstance(reference, dict) or reference.get('scope') not in ('system', 'user', 'template'):
        raise ValueError('assembly resource requires a complete scoped reference')
    required = {'scope', 'id'}
    if reference['scope'] == 'template':
        required.add('templateId')
    if (not required <= set(reference) or set(reference) - required - {'version', 'digest'}
            or any(not isinstance(reference[key], str) or not reference[key] for key in required)
            or any(not isinstance(reference[key], str) or not reference[key]
                   for key in ('version', 'digest') if key in reference)):
        raise ValueError('assembly resource requires a complete scoped reference')
    return reference


def _validate_assembly_resources(process_input):
    resources = process_input.get('resources', {})
    if not isinstance(resources, dict):
        raise ValueError('assembly resource slots must be an object')
    for resource in resources.values():
        if not isinstance(resource, dict) or 'ref' not in resource:
            raise ValueError('assembly process resource requires a ref')
        if process_input['schemaVersion'] == 3:
            if set(resource) != {'ref'}:
                raise ValueError('assembly resource requires a complete scoped reference')
            validate_assembly_resource_ref(resource['ref'])


def _pure(value):
    if isinstance(value, dict):
        for key, child in value.items():
            if key in _RESULT_FIELDS or key.startswith("tubeDesigner.assemblyProcess"):
                raise ValueError(f"manufacturing input contains executed process field: {key}")
            _pure(child)
    elif isinstance(value, (list, tuple)):
        for child in value:
            _pure(child)


def _vector(value, path):
    if (not isinstance(value, (list, tuple)) or len(value) != 3 or any(isinstance(v, bool)
            or not isinstance(v, (int, float)) or not math.isfinite(v) for v in value)):
        raise ValueError(f"{path} requires three finite numbers")
    return value


def _tube_path(value, resources):
    if not isinstance(value, dict) or value.get("kind") != "tube-path":
        raise ValueError("unsupported manufacturing input kind")
    if set(value) - {"kind", "profileResource", "path", "formingOrder"}:
        raise ValueError("manufacturing tube-path contains solved allocation fields")
    if resources.get(value.get("profileResource"), {}).get("operator") != "profile2d":
        raise ValueError("tube-path requires a true profile resource")
    path = value.get("path")
    if not isinstance(path, dict) or set(path) - {"vertices", "segments", "closed", "startVertex", "closure"}:
        raise ValueError("invalid manufacturing design path")
    vertices = _nodes(path.get("vertices"), "path vertices")
    for vertex in vertices.values():
        if set(vertex) != {"key", "point"}:
            raise ValueError("path vertex requires key and design point")
        _vector(vertex["point"], "path point")
    segments = _nodes(path.get("segments"), "path segments")
    if not segments or not isinstance(path.get("closed"), bool) or path.get("startVertex") not in vertices:
        raise ValueError("path requires segments, closed and startVertex")
    current = path["startVertex"]
    used = {current}
    for segment in segments.values():
        if set(segment) != {"key", "from", "to", "sectionFrame"}:
            raise ValueError("design segment requires endpoints and sectionFrame")
        if segment["from"] != current or segment["from"] not in vertices or segment["to"] not in vertices:
            raise ValueError("design segments must form an ordered continuous path")
        frame = _placement(segment["sectionFrame"], "path sectionFrame")
        start, end = vertices[segment["from"]]["point"], vertices[segment["to"]]["point"]
        delta = [b-a for a, b in zip(start, end)]
        length = math.sqrt(sum(v*v for v in delta))
        if length <= 1.e-9 or any(abs(delta[i]/length-frame["zAxis"][i]) > 1.e-7 for i in range(3)):
            raise ValueError("sectionFrame zAxis must follow the design segment")
        if any(abs(start[i]-frame["origin"][i]) > 1.e-7 for i in range(3)):
            raise ValueError("sectionFrame origin must equal the design segment start")
        current = segment["to"]
        used.add(current)
    if path["closed"] and current != path["startVertex"]:
        raise ValueError("closed path must return to startVertex")
    if not path['closed'] and current == path['startVertex'] or used != set(vertices):
        raise ValueError('design path closure or vertex coverage is inconsistent')
    if path['closed'] and not isinstance(path.get('closure'), str):
        raise ValueError('closed design path requires an explicit seam constraint')
    order = value.get("formingOrder", [])
    if not isinstance(order, list) or len(order) != len(set(order)) or any(key not in vertices for key in order):
        raise ValueError("formingOrder requires distinct design vertex keys")
    return segments


def _design_member(value, resources):
    """A stable product member, without any route grouping or stock allocation."""
    if (not isinstance(value, dict) or set(value) != {"kind", "profileResource", "start", "end", "sectionFrame"}
            or value.get("kind") != "tube-member"):
        raise ValueError("designInput requires a complete tube-member")
    if not isinstance(value['profileResource'], str) or resources.get(value["profileResource"], {}).get("operator") != "profile2d":
        raise ValueError("design member requires a true profile resource")
    start = _vector(value["start"], "design member start")
    end = _vector(value["end"], "design member end")
    frame = _placement(value["sectionFrame"], "design member sectionFrame")
    delta = [b-a for a, b in zip(start, end)]
    length = math.sqrt(sum(v*v for v in delta))
    if length <= 1.e-9 or any(abs(delta[i]/length-frame["zAxis"][i]) > 1.e-7 for i in range(3)):
        raise ValueError("design member sectionFrame must follow its endpoints")
    if any(abs(start[i]-frame["origin"][i]) > 1.e-7 for i in range(3)):
        raise ValueError("design member sectionFrame origin must equal start")


def validate_manufacturing_model(document: dict[str, Any]) -> dict[str, Any]:
    """Validate pending declarations without allocating or evaluating geometry."""
    if isinstance(document, dict) and document.get('schemaVersion') == 4:
        return _validate_declarations(document)
    if (not isinstance(document, dict) or document.get("schema") != SCHEMA
            or type(document.get("schemaVersion")) is not int or document["schemaVersion"] not in (2, 3)
            or set(document) != _FIELDS):
        raise ValueError("unsupported or impure manufacturing input model")
    if document["lengthUnit"] != "mm" or not isinstance(document["coordinateSystem"], str):
        raise ValueError("invalid manufacturing coordinate system or unit")
    _pure(document)
    json.dumps(document, allow_nan=False)
    resources = _nodes(document["resources"], "resources")
    _validate_graph(resources)
    items = _nodes(document["items"], "items")
    version = document["schemaVersion"]
    hierarchy, segments_by_item, design_items = [], {}, set()
    for item in items.values():
        _key(item["key"], "item key")
        if set(item) - {"key", "displayName", "children", "properties", "geometry", "manufacturingInput", "componentReference", "designInput"}:
            raise ValueError("invalid manufacturing item fields")
        forms = [field for field in ("geometry", "manufacturingInput", "componentReference", "designInput") if field in item]
        if len(forms) > 1 or forms and item.get("children"):
            raise ValueError("manufacturing item has conflicting initial forms")
        if "designInput" in item:
            if version != 3:
                raise ValueError("designInput requires manufacturing input version three")
            _design_member(item["designInput"], resources)
            profile = item.get('properties', {}).get('tubeDesigner.profile', {})
            if profile.get('sectionResource') != item['designInput']['profileResource']:
                raise ValueError('design member section must match its own tube profile')
            if set(item.get('properties', {})) & {'length', 'stockLength', 'station', 'stockStart', 'startReserve', 'endReserve', 'bendAllowance', 'stockState', 'formingOrder'}:
                raise ValueError('design input contains manufacturing route or stock dimensions')
            design_items.add(item['key'])
        if "manufacturingInput" in item:
            if version == 3:
                raise ValueError("version three product input cannot pre-plan manufacturing paths")
            segments_by_item[item["key"]] = _tube_path(item["manufacturingInput"], resources)
            profile = item.get('properties', {}).get('tubeDesigner.profile', {})
            if profile.get('sectionResource') != item['manufacturingInput']['profileResource']:
                raise ValueError('manufacturing path section must match its own tube profile')
            if set(item.get('properties', {})) & {'length', 'station', 'stockStart', 'startReserve', 'endReserve', 'bendAllowance'}:
                raise ValueError('unallocated manufacturing input contains solved stock dimensions')
        if "geometry" in item:
            ref = item["geometry"]
            if (not isinstance(ref, dict) or set(ref) - {"resource", "placement", "instanceKey"}
                    or ref.get("resource") not in resources):
                raise ValueError("manufacturing item requires a known initial geometry resource")
            if "placement" in ref:
                _placement(ref["placement"], "manufacturing geometry placement")
            elif "instanceKey" in ref:
                raise ValueError("instanceKey requires placement")
        if "componentReference" in item and (not isinstance(item["componentReference"], dict) or not item['componentReference']):
            raise ValueError("componentReference must be an object")
        shadow = {k: v for k, v in item.items() if k not in {"manufacturingInput", "componentReference", "designInput"}}
        if forms:
            shadow.setdefault("geometry", {})
        hierarchy.append(shadow)
    _hierarchy(hierarchy, document["roots"])
    mappings = document["sourceMappings"]
    if not isinstance(mappings, list):
        raise ValueError("sourceMappings must be an array")
    mapped = set()
    for mapping in mappings:
        if (not isinstance(mapping, dict) or set(mapping) != {"itemKey", "sources"}
                or mapping["itemKey"] not in items or mapping["itemKey"] in mapped):
            raise ValueError("invalid manufacturing source mapping")
        mapped.add(mapping["itemKey"])
        if not isinstance(mapping["sources"], list) or not mapping["sources"]:
            raise ValueError("manufacturing sources must be nonempty")
        if mapping['itemKey'] in design_items and (len(mapping['sources']) != 1
                or not isinstance(mapping['sources'][0], dict) or set(mapping['sources'][0]) != {'itemKey'}):
            raise ValueError('design input requires one direct display source, without manufacturing segments')
        seen = set()
        for source in mapping["sources"]:
            if not isinstance(source, dict) or set(source) - {"itemKey", "segments"}:
                raise ValueError("invalid display source mapping")
            _key(source.get("itemKey"), "display source key")
            if source["itemKey"] in seen:
                raise ValueError("duplicate display source mapping")
            seen.add(source["itemKey"])
            for span in source.get("segments", []):
                if (not isinstance(span, dict) or set(span) != {"segmentKey", "sourceRange"}
                        or span["segmentKey"] not in segments_by_item.get(mapping["itemKey"], {})):
                    raise ValueError("source mapping references an unknown design segment")
                interval = span["sourceRange"]
                if (not isinstance(interval, list) or len(interval) != 2 or any(isinstance(v, bool)
                        or not isinstance(v, (int, float)) or not 0 <= v <= 1 for v in interval) or interval[0] == interval[1]):
                    raise ValueError("sourceRange requires distinct fractions in [0,1]")
    processes = _nodes(document["processes"], "processes")
    deps, output_namespaces, consumed_design = {}, set(), set()
    for key, process in processes.items():
        _key(key, "process key")
        if set(process) != {"key", "kind", "definition"} or process["kind"] != "assembly-process":
            raise ValueError("manufacturing requires pending assembly-process calls")
        definition = process["definition"]
        common = {"templateId", "processInput", "parameters", "processDrafts", "dependencies"}
        if (not isinstance(definition, dict) or set(definition) not in (common | {"targets"}, common | {"outputs"})
                or version == 2 and "outputs" in definition):
            raise ValueError("invalid pending assembly process definition")
        _key(definition["templateId"], "assembly template id")
        if any(not isinstance(definition[field], dict) for field in ("parameters", "processDrafts")):
            raise ValueError("assembly process parameters, drafts and targets require objects")
        targets = definition.get("targets", {})
        outputs = definition.get("outputs", {})
        if not isinstance(targets, dict) or not isinstance(outputs, dict) or not (targets or outputs):
            raise ValueError("assembly process requires nonempty fixed targets or generated outputs")
        for role, output in outputs.items():
            _key(role, "assembly output role")
            if (not isinstance(output, dict) or set(output) != {"kind", "key"}
                    or output.get("kind") != "manufacturing-set"):
                raise ValueError("assembly generated output requires a manufacturing-set namespace")
            namespace = _key(output["key"], "assembly output namespace")
            if namespace in output_namespaces or namespace in items:
                raise ValueError("assembly generated output namespace must be unique")
            output_namespaces.add(namespace)
        deps[key] = definition["dependencies"]
        if (not isinstance(deps[key], list) or len(deps[key]) != len(set(deps[key]))
                or any(dep not in processes or dep == key for dep in deps[key])):
            raise ValueError("invalid assembly process dependencies")
        for target in targets.values():
            if target not in items or not any(field in items[target] for field in ("geometry", "manufacturingInput", "componentReference")):
                raise ValueError("assembly output target must be a manufacturing item")
        inp = definition["processInput"]
        if (not isinstance(inp, dict) or set(inp) - {"schema", "schemaVersion", "parts", "geometry", "resources"}
                or inp.get("schema") != "icax.assembly-process-input" or type(inp.get("schemaVersion")) is not int
                or inp["schemaVersion"] not in ((2,) if version == 2 else (2, 3))
                or not isinstance(inp.get("parts"), dict) or not isinstance(inp.get("geometry"), dict)):
            raise ValueError("invalid assembly process input")
        if not inp["parts"]:
            raise ValueError("assembly process parts and targets must not be empty")
        if outputs and inp['schemaVersion'] != 3:
            raise ValueError("generated outputs require assembly input version three")
        for role, target in targets.items():
            binding = inp["parts"].get(role)
            if (not isinstance(binding, dict) or binding.get("scope") != "manufacturing"
                    or binding.get("itemKey") != target):
                raise ValueError("manufacturing process cannot target an observer or a different item")
        for role, binding in inp["parts"].items():
            _key(role, "assembly input role")
            if not isinstance(binding, dict):
                raise ValueError("assembly part binding must be an object")
            scope = binding.get("scope")
            if scope == "design":
                if (version != 3 or inp['schemaVersion'] != 3 or not outputs
                        or set(binding) != {'scope', 'itemKeys'} or not isinstance(binding['itemKeys'], list)
                        or not binding['itemKeys'] or any(not isinstance(key, str) for key in binding['itemKeys'])
                        or len(set(binding['itemKeys'])) != len(binding['itemKeys'])
                        or any(key not in items or not any(field in items[key] for field in ('designInput', 'geometry', 'componentReference')) for key in binding['itemKeys'])):
                    raise ValueError('assembly design binding requires explicit distinct design items')
                consumed_design.update(binding['itemKeys'])
            elif scope == 'process-output':
                if (version != 3 or inp['schemaVersion'] != 3 or not outputs
                        or set(binding) != {'scope', 'processKey', 'outputKey'}
                        or not isinstance(binding['processKey'], str) or binding['processKey'] not in deps[key]
                        or not isinstance(binding['outputKey'], str)
                        or binding['outputKey'] not in processes[binding['processKey']]['definition'].get('outputs', {})):
                    raise ValueError('assembly output input requires an explicit dependency and producer output role')
            elif scope == "manufacturing":
                if (set(binding) - {"scope", "itemKey", "state", "data"} or binding.get("itemKey") not in items
                        or not any(field in items[binding['itemKey']] for field in ('geometry', 'manufacturingInput', 'componentReference'))):
                    raise ValueError("unknown manufacturing part binding")
                state = binding.get("state")
                if state != "initial":
                    if (not isinstance(state, dict) or set(state) != {"after"} or state["after"] not in deps[key]
                            or binding["itemKey"] not in processes[state["after"]]["definition"].get("targets", {}).values()):
                        raise ValueError("after state must depend on a writer of the same item")
            elif scope == "input":
                if set(binding) != {"scope", "data"} or not isinstance(binding["data"], dict) or not binding['data']:
                    raise ValueError("inline input requires explicit design facts")
            elif scope == "display":
                if not isinstance(binding.get("itemKey"), str):
                    raise ValueError("display observation requires an item key")
            elif scope == "resource":
                if binding.get("resourceKey") not in resources or 'placement' not in binding:
                    raise ValueError("resource observation requires a known resource")
                if "placement" in binding:
                    _placement(binding["placement"], "assembly resource placement")
            else:
                raise ValueError("unsupported assembly part scope")
        _validate_assembly_resources(inp)
    visited, active = set(), set()
    def visit(key):
        if key in active:
            raise ValueError("cyclic assembly process dependency")
        if key in visited:
            return
        active.add(key)
        for dep in deps[key]:
            visit(dep)
        active.remove(key)
        visited.add(key)
    for key in processes:
        visit(key)
    if design_items - consumed_design:
        raise ValueError('design input must be consumed by a generated-output assembly process')
    if design_items - mapped:
        raise ValueError('design input requires an explicit display source mapping')
    return document


def initial_manufacturing_model(document):
    """Private adapter for known initial geometry; never allocate design paths."""
    validate_manufacturing_model(document)
    if document.get('schemaVersion') == 4:
        raise ValueError('manufacturing declarations requires host display designModel and assembly execution')
    if any("manufacturingInput" in item or "componentReference" in item or "designInput" in item for item in document["items"]):
        raise ValueError("unresolved manufacturing inputs require assembly execution")
    items, visible = [], []
    for value in document["items"]:
        item = deepcopy(value)
        geometry = item.pop("geometry", None)
        item["representations"] = {} if geometry is None else {"result": geometry}
        if geometry:
            visible.append(item["key"])
        items.append(item)
    if document['sourceMappings']:
        by_key = {item['key']: item for item in items}
        for mapping in document['sourceMappings']:
            by_key[mapping['itemKey']].setdefault('properties', {})['manufacturing.sourceMembers'] = deepcopy(mapping['sources'])
    return {"schema": "icax.neutral-model", "schemaVersion": 2,
        "template": {"id": "assembly-manufacturing", "version": "2", "packageDigest": ""},
        "coordinateSystem": document["coordinateSystem"], "lengthUnit": document["lengthUnit"],
        "parameters": {}, "resources": deepcopy(document["resources"]), "items": items,
        "outputs": [{"key": "result", "purpose": "result", "items": visible, "properties": {}}],
        "relationships": [], "tables": [], "diagnostics": [],
        "extensions": {"tubeDesigner.geometryPurpose": "manufacturing", "tubeDesigner.manufacturingPartCount": len(visible),
                       **({'tubeDesigner.productStockMappingVersion': 1} if document['sourceMappings'] else {})}}


def expand_manufacturing_model(document):
    return initial_manufacturing_model(document)


def _input_properties(properties):
    result = deepcopy(properties)
    for key in list(result):
        if (key.startswith("tubeDesigner.assembly") or key in {"manufacturing.sourceMembers", "manufacturing.operations",
                "tubeDesigner.endProcess", "tubeDesigner.frameManufacturing", "tubeDesigner.sourceSpans", "tubeDesigner.stockPlan"}):
            result.pop(key)
    return result


IDENTITY_FRAME = {"origin": [0, 0, 0], "xAxis": [1, 0, 0], "yAxis": [0, 1, 0], "zAxis": [0, 0, 1]}


def inverse_frame(frame):
    axes = [frame[key] for key in ("xAxis", "yAxis", "zAxis")]
    return {"origin": [-sum(axis[i]*frame["origin"][i] for i in range(3)) for axis in axes],
            **{name: [axes[i][j] for i in range(3)] for j, name in enumerate(("xAxis", "yAxis", "zAxis"))}}


def _reference_frame(reference, nodes):
    key = reference["resource"]
    frame = deepcopy(reference.get("placement", IDENTITY_FRAME))
    while nodes.get(key, {}).get("operator") == "transform":
        node = nodes[key]
        if len(node.get("inputs", [])) != 1 or set(node.get("arguments", {})) != {"placement"}:
            break
        frame = _compose(frame, node["arguments"]["placement"])
        key = node["inputs"][0]
    return key, frame


def _to_manufacturing_input_model(document):
    if document.get("schema") == SCHEMA:
        validate_manufacturing_model(document)
        return deepcopy(document)
    model = to_resource_model(document)
    extensions = model.get("extensions", {})
    if extensions.get("tubeDesigner.assemblyGeometryProcesses") or extensions.get("tubeDesigner.assemblyProcessSource"):
        raise ValueError("cannot publish executed machining as manufacturing input")
    outputs = [value for value in model.get("outputs", []) if value.get("purpose") == "result"]
    if len(outputs) != 1:
        raise ValueError("manufacturing requires one selected result hierarchy")
    source_items = {item["key"]: item for item in model["items"]}
    selected = set()
    def select(key):
        if key in selected:
            return
        if key not in source_items:
            raise ValueError("unknown manufacturing item")
        selected.add(key)
        for child in source_items[key].get("children", []):
            select(child)
    for key in outputs[0]["items"]:
        select(key)
    items = []
    for original in model["items"]:
        if original["key"] not in selected:
            continue
        item = {"key": original["key"], "displayName": original["displayName"], "children": deepcopy(original.get("children", [])),
                "properties": _input_properties(original.get("properties", {}))}
        if "result" in original.get("representations", {}):
            item["geometry"] = deepcopy(original["representations"]["result"])
        items.append(item)
    by_key = {item["key"]: item for item in items}
    nodes = {node["key"]: node for node in model["resources"]}
    for item in items:
        ref = item.get("geometry", {})
        if "instanceKey" in ref:
            nodes.setdefault(ref["instanceKey"], {"key": ref["instanceKey"], "operator": "transform",
                "inputs": [ref["resource"]], "arguments": {"placement": ref["placement"]}})
    processes, previous = [], {}
    for record in extensions.get(DECLARATIONS, []):
        owner = record["stockId"]
        if owner not in by_key:
            candidates = [key for key in record.get('ownerCandidates', []) if key in by_key]
            if len(candidates) > 1:
                raise ValueError('ambiguous manufacturing owner for a shared initial resource')
            if candidates:
                owner = candidates[0]
        if owner not in by_key:
            matches = [item["key"] for item in items if item.get("geometry", {}).get("instanceKey", item.get("geometry", {}).get("resource")) == record["targetKey"]]
            if len(matches) != 1:
                raise ValueError(f"pending process has no unique manufacturing owner: {owner}")
            owner = matches[0]
        prior = previous.get(owner)
        target_base, target_frame = _reference_frame({"resource": record["targetKey"]}, nodes)
        item_base, item_frame = _reference_frame(by_key[owner]["geometry"], nodes)
        if target_base != item_base:
            raise ValueError("pending process target does not belong to the manufacturing item's initial shape")
        item_placement = _compose(item_frame, inverse_frame(target_frame))
        parts = {}
        for role, facts in record["processInput"].get("parts", {}).items():
            data = deepcopy(facts)
            if role == "stock":
                data["initialGeometry"] = {"resource": record["targetKey"], "itemPlacement": item_placement}
            parts[role] = ({"scope": "manufacturing", "itemKey": owner, "state": "initial" if prior is None else {"after": prior}, "data": data}
                           if role == "stock" else {"scope": "input", "data": deepcopy(facts)})
        process_input = {"schema": "icax.assembly-process-input", "schemaVersion": 2,
                         "parts": parts, "geometry": deepcopy(record["processInput"].get("geometry", {}))}
        if "resources" in record["processInput"]:
            process_input["resources"] = deepcopy(record["processInput"]["resources"])
        processes.append({"key": record["instanceId"], "kind": "assembly-process", "definition": {
            "templateId": record["functionId"], "processInput": process_input, "parameters": deepcopy(record["parameters"]),
            "processDrafts": {}, "targets": {"stock": owner}, "dependencies": [] if prior is None else [prior]}})
        previous[owner] = record["instanceId"]
    children = {child for item in items for child in item["children"]}
    result = {"schema": SCHEMA, "schemaVersion": 3, "coordinateSystem": model["coordinateSystem"],
        "lengthUnit": model["lengthUnit"], "resources": deepcopy(model["resources"]), "items": items,
        "roots": [item["key"] for item in items if item["key"] not in children], "sourceMappings": [], "processes": processes}
    validate_manufacturing_model(result)
    return result


def _validate_declarations(document):
    """Validate a recipe whose design objects are supplied separately by the host."""
    if (document.get('schema') != SCHEMA or type(document.get('schemaVersion')) is not int
            or document['schemaVersion'] != 4 or set(document) != _DECLARATION_FIELDS):
        raise ValueError('manufacturing declarations contain only connections and processes')
    _pure(document)
    json.dumps(document, allow_nan=False)
    for key, relation in _nodes(document['connections'], 'connections').items():
        _key(key, 'connection key')
        if (set(relation) - {'key', 'kind', 'items', 'properties'}
                or not isinstance(relation.get('kind'), str) or not relation['kind']
                or not isinstance(relation.get('items'), list) or len(relation['items']) < 2
                or len(set(relation['items'])) != len(relation['items'])
                or not isinstance(relation.get('properties', {}), dict)):
            raise ValueError('invalid manufacturing connection')
        for item in relation['items']:
            _key(item, 'connection item reference')
        properties = relation.get('properties', {})
        if 'participantAnchors' in properties:
            anchors = properties['participantAnchors']
            if not isinstance(anchors, list):
                raise ValueError('manufacturing participant anchors must be an array')
            seen = set()
            for anchor in anchors:
                item_key = anchor.get('itemKey') if isinstance(anchor, dict) else None
                if (not isinstance(item_key, str) or item_key not in relation['items']
                        or item_key in seen):
                    raise ValueError('manufacturing anchor requires a distinct connection participant')
                seen.add(item_key)
    calls = _nodes(document['processes'], 'processes')
    namespaces, dependencies = set(), {}
    for key, process in calls.items():
        _key(key, 'process key')
        if set(process) != {'key', 'kind', 'definition'} or process['kind'] != 'assembly-process':
            raise ValueError('manufacturing requires pending assembly-process calls')
        call = process['definition']
        common = {'templateId', 'processInput', 'parameters', 'processDrafts', 'dependencies'}
        if not isinstance(call, dict) or set(call) not in (common | {'targets'}, common | {'outputs'}):
            raise ValueError('invalid pending assembly process definition')
        _key(call['templateId'], 'assembly template id')
        if any(not isinstance(call[name], dict) for name in ('parameters', 'processDrafts')):
            raise ValueError('assembly parameters and drafts require objects')
        dependencies[key] = call['dependencies']
        if (not isinstance(call['dependencies'], list) or len(set(call['dependencies'])) != len(call['dependencies'])
                or any(dep not in calls or dep == key for dep in call['dependencies'])):
            raise ValueError('invalid assembly process dependencies')
        targets, outputs = call.get('targets', {}), call.get('outputs', {})
        if not isinstance(targets, dict) or not isinstance(outputs, dict) or not (targets or outputs):
            raise ValueError('assembly process requires fixed targets or generated outputs')
        for role, target in targets.items():
            _key(role, 'assembly target role')
            _key(target, 'assembly target item reference')
        for role, output in outputs.items():
            _key(role, 'assembly output role')
            if not isinstance(output, dict) or set(output) != {'kind', 'key'} or output['kind'] != 'manufacturing-set':
                raise ValueError('assembly generated output requires a manufacturing-set namespace')
            namespace = _key(output['key'], 'assembly output namespace')
            if namespace in namespaces:
                raise ValueError('assembly generated output namespace must be unique')
            namespaces.add(namespace)
        inp = call['processInput']
        if (not isinstance(inp, dict) or set(inp) - {'schema', 'schemaVersion', 'parts', 'geometry', 'resources'}
                or inp.get('schema') != 'icax.assembly-process-input'
                or type(inp.get('schemaVersion')) is not int or inp['schemaVersion'] not in (2, 3)
                or not isinstance(inp.get('parts'), dict) or not inp['parts']
                or not isinstance(inp.get('geometry'), dict)
                or not isinstance(inp.get('resources', {}), dict)):
            raise ValueError('invalid assembly process input')
        if 'connections' in inp['geometry']:
            raise ValueError('connections belong to manufacturing declarations, not a duplicate process geometry')
        if outputs and inp['schemaVersion'] != 3:
            raise ValueError('generated outputs require assembly input version three')
        for role, binding in inp['parts'].items():
            _key(role, 'assembly input role')
            if not isinstance(binding, dict):
                raise ValueError('assembly part binding must be an object')
            scope = binding.get('scope')
            if scope == 'design':
                if (not outputs or set(binding) != {'scope', 'itemKeys'}
                        or not isinstance(binding['itemKeys'], list) or not binding['itemKeys']
                        or len(set(binding['itemKeys'])) != len(binding['itemKeys'])):
                    raise ValueError('design binding requires explicit distinct item references')
                for item in binding['itemKeys']:
                    _key(item, 'design item reference')
            elif scope == 'process-output':
                if (not outputs or set(binding) != {'scope', 'processKey', 'outputKey'}
                        or binding['processKey'] not in call['dependencies']
                        or binding['outputKey'] not in calls[binding['processKey']]['definition'].get('outputs', {})):
                    raise ValueError('assembly output input requires an explicit dependency and producer output role')
            elif scope == 'manufacturing':
                if set(binding) - {'scope', 'itemKey', 'state', 'data'}:
                    raise ValueError('invalid item processing binding')
                _key(binding.get('itemKey'), 'processing item reference')
                state = binding.get('state')
                if state != 'initial' and (not isinstance(state, dict) or set(state) != {'after'}
                        or state['after'] not in call['dependencies']
                        or binding['itemKey'] not in calls[state['after']]['definition'].get('targets', {}).values()):
                    raise ValueError('after state must depend on a writer of the same item')
                if not isinstance(binding.get('data', {}), dict):
                    raise ValueError('processing binding data must be an object')
                initial = binding.get('data', {}).get('initialGeometry')
                if initial is not None:
                    if not isinstance(initial, dict) or set(initial) != {'itemPlacement'}:
                        raise ValueError('initial geometry is supplied by the host design item')
                    _placement(initial['itemPlacement'], 'assembly item processing frame')
            elif scope == 'input':
                if set(binding) != {'scope', 'data'} or not isinstance(binding['data'], dict) or not binding['data']:
                    raise ValueError('inline input requires explicit design facts')
            elif scope == 'resource':
                _key(binding.get('resourceKey'), 'design resource reference')
                _placement(binding.get('placement'), 'assembly resource placement')
            elif scope == 'display':
                _key(binding.get('itemKey'), 'design observation reference')
            else:
                raise ValueError('unsupported assembly part scope')
        for role, target in targets.items():
            binding = inp['parts'].get(role, {})
            if binding.get('scope') != 'manufacturing' or binding.get('itemKey') != target:
                raise ValueError('assembly process cannot target an observer or a different item')
        _validate_assembly_resources(inp)
    visited, active = set(), set()
    def visit(key):
        if key in active:
            raise ValueError('cyclic assembly process dependency')
        if key in visited:
            return
        active.add(key)
        for dependency in dependencies[key]:
            visit(dependency)
        active.remove(key)
        visited.add(key)
    for key in calls:
        visit(key)
    return document


_FACT_ALIASES = {'partKind': 'manufacturing.partKind', 'sourcing': 'manufacturing.sourcing',
                 'material': 'manufacturing.material', 'materialGrade': 'manufacturing.materialGrade',
                 'materialCategory': 'manufacturing.materialCategory', 'categoryKey': 'manufacturing.categoryKey',
                 'categoryName': 'manufacturing.categoryName', 'modelReference': 'manufacturing.modelReference',
                 'plate': 'manufacturing.plate'}


def compose_manufacturing_model(declarations, design_model):
    """Bind immutable declarations to the host's existing display design objects.

    The returned v3 structure is private executor input, never a script result
    or another saved product object table. No product code or geometry runs.
    """
    validate_manufacturing_model(declarations)
    if declarations.get('schemaVersion') != 4:
        return deepcopy(declarations)
    if not isinstance(design_model, dict):
        raise ValueError('manufacturing declarations requires host display designModel')
    from .display import expand_display_model
    expand_display_model(design_model)
    resources = deepcopy(design_model['resources'])
    items = deepcopy(design_model['items'])
    nodes, by_key = {node['key']: node for node in resources}, {item['key']: item for item in items}
    calls = deepcopy(declarations['processes'])
    design_keys = {key for process in calls for binding in process['definition']['processInput']['parts'].values()
                   if binding['scope'] == 'design' for key in binding['itemKeys']}
    if design_keys - set(by_key):
        raise ValueError('unknown host design item reference')
    for item in items:
        properties = item.setdefault('properties', {})
        for source, target in _FACT_ALIASES.items():
            if source in properties:
                properties.setdefault(target, deepcopy(properties[source]))
        if item['key'] not in design_keys:
            continue
        member, profile = properties.get('assemblyFrame.member'), properties.get('tubeDesigner.profile')
        if not isinstance(member, dict) or not isinstance(profile, dict):
            raise ValueError('generated assembly requires the host item tube profile and member facts')
        start, end = deepcopy(member['start']), deepcopy(member['end'])
        section = profile.get('sectionResource')
        if section not in nodes or nodes[section]['operator'] != 'profile2d':
            raise ValueError('host design member requires its own actual sectionResource')
        frame = member['sectionFrame']
        data = {'kind': 'tube-member', 'profileResource': section, 'start': start, 'end': end,
                'sectionFrame': {'origin': deepcopy(frame['originAtStart']),
                                 **{axis: deepcopy(frame[axis]) for axis in ('xAxis', 'yAxis', 'zAxis')}}}
        _design_member(data, nodes)
        for name in ('length', 'stockLength', 'station', 'stockStart', 'startReserve', 'endReserve',
                     'bendAllowance', 'stockState', 'formingOrder', 'assemblyFrame.member'):
            properties.pop(name, None)
        properties.setdefault('manufacturing.partKind', 'tube')
        properties.setdefault('manufacturing.sourcing', 'made')
        item.pop('geometry', None)
        item['designInput'] = data
    for relation in declarations['connections']:
        if any(key not in by_key for key in relation['items']):
            raise ValueError('manufacturing connection references an unknown host design item')
        def anchors(value):
            if isinstance(value, dict):
                if 'itemKey' in value and value['itemKey'] not in by_key:
                    raise ValueError('manufacturing connection anchor references an unknown host design item')
                for child in value.values():
                    anchors(child)
            elif isinstance(value, list):
                for child in value:
                    anchors(child)
        anchors(relation.get('properties', {}))
    for process in calls:
        call, key = process['definition'], process['key']
        inp = call['processInput']
        if call.get('outputs'):
            inp['geometry']['connections'] = deepcopy(declarations['connections'])
        for role, binding in inp['parts'].items():
            if binding['scope'] in ('manufacturing', 'display') and binding['itemKey'] not in by_key:
                raise ValueError('assembly process references an unknown host design item')
            if binding['scope'] != 'manufacturing':
                continue
            data = binding.get('data', {})
            initial = data.get('initialGeometry')
            if initial is None:
                continue
            reference = by_key[binding['itemKey']].get('geometry')
            if not reference:
                raise ValueError('fixed processing target requires existing host geometry')
            relative = _compose(inverse_frame(initial['itemPlacement']), reference.get('placement', IDENTITY_FRAME))
            resource = reference['resource']
            if any(abs(relative[name][i]-IDENTITY_FRAME[name][i]) > 1.e-8 for name in IDENTITY_FRAME for i in range(3)):
                resource = 'manufacturing.input.' + key + '.' + role
                if resource in nodes:
                    raise ValueError('assembly input transform namespace conflicts with host resources')
                node = {'key': resource, 'operator': 'transform', 'inputs': [reference['resource']],
                        'arguments': {'placement': relative}}
                resources.append(node)
                nodes[resource] = node
            initial['resource'] = resource
    selections = [process for process in calls if process['definition']['templateId'] == 'product-manufacturing-members']
    if len(selections) > 1:
        raise ValueError('manufacturing must declare one explicit member selection')
    if selections:
        selection = selections[0]['definition']
        if (selection.get('parameters') or selection.get('processDrafts') or selection.get('dependencies')
                or 'outputs' in selection or selection['processInput']['geometry']
                or selection['processInput'].get('resources')):
            raise ValueError('manufacturing member selection contains no machining requirements')
        selected = list(selection['targets'].values())
        if len(selected) != len(set(selected)) or any(not by_key[key].get('geometry') for key in selected):
            raise ValueError('manufacturing member selection requires distinct existing geometry items')
        keep = set(selected)
        parents = {child: item['key'] for item in items for child in item.get('children', [])}
        for key in selected:
            while key in parents:
                key = parents[key]
                keep.add(key)
        items = [item for item in items if item['key'] in keep]
        for item in items:
            item['children'] = [child for child in item.get('children', []) if child in keep]
        calls = [process for process in calls if process is not selections[0]]
    else:
        keep = set(by_key)
    result = {'schema': SCHEMA, 'schemaVersion': 3,
              'coordinateSystem': design_model.get('coordinateSystem', 'right-handed-x-width-y-depth-z-height'),
              'lengthUnit': design_model.get('lengthUnit', 'mm'), 'resources': resources, 'items': items,
              'roots': [key for key in design_model['roots'] if key in keep],
              'sourceMappings': [{'itemKey': item['key'], 'sources': [{'itemKey': item['key']}]}
                                 for item in items if 'designInput' in item or 'geometry' in item],
              'processes': calls}
    validate_manufacturing_model(result)
    return result


def to_manufacturing_model(document):
    if document.get('schema') == SCHEMA:
        validate_manufacturing_model(document)
        return deepcopy(document)
    inputs = _to_manufacturing_input_model(document)
    calls = deepcopy(inputs['processes'])
    for process in calls:
        for binding in process['definition']['processInput']['parts'].values():
            initial = binding.get('data', {}).get('initialGeometry')
            if initial is not None:
                initial.pop('resource', None)
    model = to_resource_model(document)
    source_items = {item['key']: item for item in model['items']}
    display_keys = set(model.get('extensions', {}).get('tubeDesigner.manufacturingDisplayItemKeys', []))
    def collect(key):
        if key in display_keys:
            return
        display_keys.add(key)
        for child in source_items[key].get('children', []):
            collect(child)
    for output in model.get('outputs', []):
        if output['purpose'] == 'display':
            for key in output['items']:
                collect(key)
    selected_keys = {item['key'] for item in inputs['items']}
    if display_keys and display_keys != selected_keys:
        targets = {'part.%04d' % (index + 1): item['key'] for index, item in enumerate(inputs['items'])
                   if item.get('geometry')}
        calls.insert(0, {'key': 'product.manufacturing.members', 'kind': 'assembly-process', 'definition': {
            'templateId': 'product-manufacturing-members',
            'processInput': {'schema': 'icax.assembly-process-input', 'schemaVersion': 2,
                             'parts': {role: {'scope': 'manufacturing', 'itemKey': key, 'state': 'initial'}
                                       for role, key in targets.items()}, 'geometry': {}},
            'parameters': {}, 'processDrafts': {}, 'targets': targets, 'dependencies': []}})
    result = {'schema': SCHEMA, 'schemaVersion': 4,
              'connections': deepcopy(model.get('relationships', [])), 'processes': calls}
    validate_manufacturing_model(result)
    return result
