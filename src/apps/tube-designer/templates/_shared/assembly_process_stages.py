"""Schedule assembly planning capabilities without changing material snapshots.

Stages describe downstream planning, not the machine operator's bending order.
A material-boundary facet and its later cutter replay are distinct planning
tasks; callers may declare their stages without adding fields to process input.
"""
from copy import deepcopy


STAGES = ("material-requirements", "bend-unfolding", "connection-machining", "validation")
SCHEMA = "icax.assembly-stage-plan"

# Explicit current provider capabilities; external providers declare a stage.
TEMPLATE_STAGES = {
    "security-window-assembly": "material-requirements",
    "profile-stock-preparation": "material-requirements",
    "bend": "bend-unfolding",
    "segmented-bend": "bend-unfolding",
    "node-v-notch-integrated": "bend-unfolding",
    "node-embedded-arc-integrated": "bend-unfolding",
    "node-edge-arc-integrated": "bend-unfolding",
    "flexible-slit-bend-integrated": "bend-unfolding",
    "cross-through": "connection-machining",
    "end-side-tab-slot": "connection-machining",
    "four-end-end-end-end": "connection-machining",
    "insert-sleeve": "connection-machining",
    "mechanical-fastener": "connection-machining",
    "orthogonal-corner": "connection-machining",
    "saddle-weld": "connection-machining",
    "slot-bolt-adjustable": "connection-machining",
    "t-contact-fit": "connection-machining",
    "t-profile-insert": "connection-machining",
    "tab-slot-lock": "connection-machining",
    "through-bolt": "connection-machining",
    "two-end-end-angle": "connection-machining",
    "two-end-middle": "connection-machining",
    "weld-interface": "connection-machining",
    "wrap-a-over-b": "connection-machining",
    "tube-end-joint": "connection-machining",
    "tube-profile-aperture": "connection-machining",
    "tube-post-end": "connection-machining",
    "tube-post-aperture": "connection-machining",
    "structural-stock-fit": "connection-machining",
    "structural-profile-pocket": "connection-machining",
    "structural-apertures": "connection-machining",
    "structural-male-head": "connection-machining",
    "structural-plate-apertures": "connection-machining",
    "structural-planar-trim": "connection-machining",
    "extrusion-end-clip": "connection-machining",
    "through-depth-aperture": "connection-machining",
    "surface-feature-cut": "connection-machining",
    "tube-insertion-check": "validation",
}


def _stage(value, label):
    if value not in STAGES:
        raise ValueError(f"unknown assembly planning stage for {label}: {value!r}")
    return value


def stage_for_process(process, template_stages=None):
    """Read provider capabilities and an explicitly declared boundary facet."""
    definition = process["definition"]
    geometry = definition.get("processInput", {}).get("geometry", {})
    if geometry.get("allocation") == "initial-material-boundary":
        return "material-requirements"
    template = definition.get("templateId")
    capabilities = TEMPLATE_STAGES if template_stages is None else {**TEMPLATE_STAGES, **template_stages}
    if template not in capabilities:
        raise ValueError("assembly provider must declare its planning stage: " + str(template))
    return _stage(capabilities[template], template)


def plan_stages(processes, stage_by_key=None, template_stages=None):
    """Return stage barriers plus a stable topological order as pure data.

    Dependencies and after-state bindings are hard requirements. A dependency
    on a later stage is an error: split a sizing facet from cutter execution
    rather than silently violating either the stage or the snapshot contract.
    Independent calls in one stage retain their supplied order.
    """
    if not isinstance(processes, list):
        raise ValueError("assembly stage planning requires a process list")
    stage_by_key = {} if stage_by_key is None else stage_by_key
    template_stages = {} if template_stages is None else template_stages
    if not isinstance(stage_by_key, dict) or not isinstance(template_stages, dict):
        raise ValueError("assembly stage capabilities must be mappings")
    for capabilities in (template_stages, stage_by_key):
        for key, stage in capabilities.items():
            _stage(stage, key)
    lookup, stages, dependencies = {}, {}, {}
    for process in processes:
        if (not isinstance(process, dict) or not isinstance(process.get("key"), str)
                or not process["key"] or not isinstance(process.get("definition"), dict)):
            raise ValueError("assembly stage planning requires keyed process definitions")
        key = process["key"]
        if key in lookup:
            raise ValueError("duplicate assembly stage process: " + key)
        lookup[key] = process
        stages[key] = stage_by_key[key] if key in stage_by_key else stage_for_process(process, template_stages)
        declared = process["definition"].get("dependencies", [])
        if (not isinstance(declared, list) or any(not isinstance(dep, str) for dep in declared)
                or len(set(declared)) != len(declared)):
            raise ValueError("invalid assembly stage dependencies for " + key)
        dependencies[key] = list(declared)
    unknown_overrides = set(stage_by_key) - set(lookup)
    if unknown_overrides:
        raise ValueError("assembly stage capability references unknown processes: " + ", ".join(sorted(unknown_overrides)))
    for key, deps in dependencies.items():
        for dep in deps:
            if dep not in lookup:
                raise ValueError(f"assembly stage process {key} depends on unknown process {dep}")
        definition = lookup[key]["definition"]
        for role, binding in definition.get("processInput", {}).get("parts", {}).items():
            if not isinstance(binding, dict) or binding.get("scope") != "manufacturing":
                continue
            state = binding.get("state", "initial")
            if state == "initial":
                continue
            producer = state.get("after") if isinstance(state, dict) and set(state) == {"after"} else None
            if (producer not in deps or binding.get("itemKey") not in
                    lookup[producer]["definition"].get("targets", {}).values()):
                raise ValueError(f"assembly stage process {key} role {role}: after state must depend on a writer of the same item")
    # Validate cycles before stage conflicts so a circular graph reports the
    # actual graph error even when its edges also cross stage barriers.
    # Preserve the previous predecessor-first traversal within each stage.
    # A different ready-queue policy could move independent cuts ahead of a
    # writer and change the accumulated material snapshot named by after.
    topological, visited, active = [], set(), set()
    for root in lookup:
        if root in visited:
            continue
        active.add(root)
        pending = [(root, iter(dependencies[root]))]
        while pending:
            key, incoming = pending[-1]
            dep = next(incoming, None)
            if dep is None:
                pending.pop()
                active.remove(key)
                visited.add(key)
                topological.append(key)
            elif dep in active:
                cycle = [entry[0] for entry in pending] + [dep]
                raise ValueError("cyclic assembly process dependency in stage planning: " + " -> ".join(cycle))
            elif dep not in visited:
                active.add(dep)
                pending.append((dep, iter(dependencies[dep])))
    ranks = {stage: index for index, stage in enumerate(STAGES)}
    for key, deps in dependencies.items():
        for dep in deps:
            if ranks[stages[dep]] > ranks[stages[key]]:
                raise ValueError(f"assembly stage dependency conflict: {key} ({stages[key]}) depends on "
                                 f"{dep} ({stages[dep]}); split material requirements from machining")
    batches = [{"stage": stage, "processKeys": [key for key in topological if stages[key] == stage]}
               for stage in STAGES]
    return {"schema": SCHEMA, "schemaVersion": 1, "stages": batches,
            "orderedKeys": [key for batch in batches for key in batch["processKeys"]],
            "dependencies": dependencies}


def ordered_processes(processes, stage_by_key=None, template_stages=None):
    """Return the original process objects in their validated execution order."""
    plan = plan_stages(processes, stage_by_key, template_stages)
    lookup = {process["key"]: process for process in processes}
    return [lookup[key] for key in plan["orderedKeys"]]


def _target_materials(process):
    """A provider supplies only the physical stocks it actually writes."""
    materials = set()
    for target in process["definition"].get("targets", {}).values():
        material = target.get("stockId") if isinstance(target, dict) else target
        if not isinstance(material, str) or not material.strip():
            raise ValueError("invalid assembly material target for " + process["key"])
        materials.add(material)
    return materials


def _process_materials(process):
    """Read physical material references; display identities need an owner map."""
    definition = process["definition"]
    materials = _target_materials(process)
    for binding in definition.get("processInput", {}).get("parts", {}).values():
        if isinstance(binding, dict) and binding.get("scope") == "manufacturing":
            material = binding.get("itemKey")
            if not isinstance(material, str) or not material.strip():
                raise ValueError("invalid manufacturing material binding for " + process["key"])
            materials.add(material)
    return materials


def with_material_dependencies(processes, material_bindings=None, stage_by_key=None):
    """Copy downstream calls and add prerequisites for their actual materials.

    An optional process-key to material-ID map resolves display/source owners.
    Each supplied entry names consumer reads/writes; an omitted key uses targets
    plus manufacturing-part references. Providers are indexed by written targets
    so an observed mate never makes a process a writer of the mate's material.
    Independent bends never acquire edges to each other. Existing dependencies
    and explicit material snapshots remain authoritative, and same-stage
    machining dependencies are not inferred.
    """
    if not isinstance(processes, list):
        raise ValueError("assembly material dependencies require a process list")
    material_bindings = {} if material_bindings is None else material_bindings
    stage_by_key = {} if stage_by_key is None else stage_by_key
    if not isinstance(material_bindings, dict) or not isinstance(stage_by_key, dict):
        raise ValueError("assembly material bindings and stage capabilities must be mappings")
    for key, stage in stage_by_key.items():
        _stage(stage, key)
    copied = deepcopy(processes)
    stages, materials = {}, {}
    for process in copied:
        if (not isinstance(process, dict) or not isinstance(process.get("key"), str)
                or not process["key"] or not isinstance(process.get("definition"), dict)):
            raise ValueError("assembly material dependencies require keyed process definitions")
        key, definition = process["key"], process["definition"]
        if key in stages:
            raise ValueError("duplicate assembly stage process: " + key)
        stages[key] = stage_by_key[key] if key in stage_by_key else stage_for_process(process)
        declared = definition.get("dependencies", [])
        if not isinstance(declared, list) or any(not isinstance(dep, str) for dep in declared):
            raise ValueError("invalid assembly stage dependencies for " + key)
        definition["dependencies"] = list(dict.fromkeys(declared))
        if key not in material_bindings:
            materials[key] = _process_materials(process)
        else:
            supplied = material_bindings[key]
            if isinstance(supplied, str):
                supplied = [supplied]
            if (not isinstance(supplied, (list, tuple, set, frozenset))
                    or any(not isinstance(material, str) or not material.strip() for material in supplied)):
                raise ValueError("invalid assembly material owner bindings for " + key)
            materials[key] = set(supplied)
    unknown = set(material_bindings) - set(stages)
    if unknown:
        raise ValueError("assembly material bindings reference unknown processes: " + ", ".join(sorted(unknown)))
    providers = {stage: {} for stage in STAGES}
    for process in copied:
        key = process["key"]
        owned = _target_materials(process) if "targets" in process["definition"] else materials[key]
        for material in owned:
            providers[stages[key]].setdefault(material, set()).add(key)
    prerequisites = {"material-requirements": (),
                     "bend-unfolding": ("material-requirements",),
                     "connection-machining": ("material-requirements", "bend-unfolding"),
                     "validation": ("material-requirements", "bend-unfolding", "connection-machining")}
    for process in copied:
        key = process["key"]
        automatic = set()
        for stage in prerequisites[stages[key]]:
            for material in materials[key]:
                automatic.update(providers[stage].get(material, ()))
        existing = process["definition"]["dependencies"]
        existing.extend(sorted(automatic - set(existing)))
    # After-state validation runs against the completed graph, but the state
    # itself is never rewritten from initial to a guessed latest workpiece.
    plan_stages(copied, stage_by_key=stage_by_key)
    return copied


def merge_stage_plans(*plans):
    """Aggregate independently executed output-set plans for model metadata."""
    by_stage = {stage: [] for stage in STAGES}
    owners, materialized, dependencies = {}, [], {}
    for plan in plans:
        if (not isinstance(plan, dict) or plan.get("schema") != SCHEMA
                or plan.get("schemaVersion") != 1 or not isinstance(plan.get("stages"), list)
                or [batch.get("stage") for batch in plan["stages"] if isinstance(batch, dict)] != list(STAGES)):
            raise ValueError("invalid assembly stage plan output")
        flattened = []
        for batch in plan["stages"]:
            stage = batch["stage"]
            keys = batch.get("processKeys")
            if not isinstance(keys, list) or any(not isinstance(key, str) or not key for key in keys):
                raise ValueError("invalid assembly stage plan process keys")
            flattened.extend(keys)
            for key in keys:
                if key in owners and owners[key] != stage:
                    raise ValueError("conflicting assembly output stage for process " + key)
                if key not in owners:
                    owners[key] = stage
                    by_stage[stage].append(key)
        if flattened != plan.get("orderedKeys") or len(flattened) != len(set(flattened)):
            raise ValueError("invalid assembly stage plan execution order")
        declared = plan.get("dependencies")
        if not isinstance(declared, dict) or set(declared) != set(flattened):
            raise ValueError("invalid assembly stage plan dependency graph")
        for key, deps in declared.items():
            if (not isinstance(deps, list) or any(not isinstance(dep, str) for dep in deps)
                    or len(set(deps)) != len(deps) or set(deps) - set(flattened)):
                raise ValueError("invalid assembly stage plan dependencies for " + key)
            if key in dependencies and dependencies[key] != deps:
                raise ValueError("conflicting assembly output dependencies for process " + key)
            dependencies[key] = list(deps)
        keys = plan.get("materializedProcessKeys", [])
        if not isinstance(keys, list) or any(not isinstance(key, str) or not key for key in keys):
            raise ValueError("invalid materialized assembly process keys")
        materialized.extend(key for key in keys if key not in materialized)
    batches = [{"stage": stage, "processKeys": by_stage[stage]} for stage in STAGES]
    merged = {"schema": SCHEMA, "schemaVersion": 1, "stages": batches,
              "orderedKeys": [key for batch in batches for key in batch["processKeys"]],
              "dependencies": dependencies}
    if any("materializedProcessKeys" in plan for plan in plans):
        merged["materializedProcessKeys"] = materialized
    return merged


def merge_material_plans(*plans):
    """Keep frozen allocation records for all independent output stocks."""
    stocks, boundaries = {}, {}
    for plan in plans:
        if (not isinstance(plan, dict) or plan.get("schema") != "icax.assembly-material-plan"
                or plan.get("schemaVersion") != 1 or not isinstance(plan.get("stocks"), list)
                or not isinstance(plan.get("initialMaterialAllocations"), list)):
            raise ValueError("invalid assembly material plan output")
        for stock in plan["stocks"]:
            if not isinstance(stock, dict) or not isinstance(stock.get("stockId"), str) or not stock["stockId"]:
                raise ValueError("invalid assembly material plan stock identity")
            key = stock["stockId"]
            if key in stocks and stocks[key] != stock:
                raise ValueError("conflicting assembly material plan for stock " + key)
            stocks[key] = deepcopy(stock)
        for allocation in plan["initialMaterialAllocations"]:
            if not isinstance(allocation, dict) or allocation.get("stockId") not in stocks:
                raise ValueError("assembly material boundary references an unknown stock")
            key = tuple(allocation.get(field) for field in ("stockId", "sourceItemKey", "end"))
            if any(not isinstance(value, str) or not value for value in key):
                raise ValueError("invalid assembly material boundary identity")
            if key in boundaries and boundaries[key] != allocation:
                raise ValueError("conflicting assembly material boundary for stock " + key[0])
            boundaries[key] = deepcopy(allocation)
    return {"schema": "icax.assembly-material-plan", "schemaVersion": 1,
            "stocks": list(stocks.values()), "initialMaterialAllocations": list(boundaries.values())}
