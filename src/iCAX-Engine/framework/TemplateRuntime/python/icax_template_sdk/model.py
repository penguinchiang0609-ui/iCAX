from __future__ import annotations

from copy import deepcopy
from typing import Any, Iterable, Mapping


class NeutralModel:
    """Pure-data builder for the iCAX neutral-model protocol.

    The builder never calls native code and never owns native handles.  Its only
    result is a JSON-serializable document consumed later by the C++ evaluator.
    """

    def __init__(
        self,
        *,
        template_id: str,
        template_version: str,
        package_digest: str,
        parameters: Mapping[str, Any],
        coordinate_system: str = "right-handed-x-width-y-depth-z-height",
        length_unit: str = "mm",
    ) -> None:
        self._document: dict[str, Any] = {
            "schema": "icax.neutral-model",
            "schemaVersion": 1,
            "template": {
                "id": template_id,
                "version": template_version,
                "packageDigest": package_digest,
            },
            "coordinateSystem": coordinate_system,
            "lengthUnit": length_unit,
            "parameters": deepcopy(dict(parameters)),
            "geometry": [],
            "items": [],
            "outputs": [],
            "relationships": [],
            "tables": [],
            "diagnostics": [],
        }
        self._geometry_keys: set[str] = set()
        self._item_keys: set[str] = set()
        self._relationship_keys: set[str] = set()

    def geometry(
        self,
        key: str,
        operator: str,
        *,
        inputs: Iterable[str] = (),
        arguments: Mapping[str, Any] | None = None,
    ) -> str:
        self._require_new_key(key, self._geometry_keys, "geometry")
        self._document["geometry"].append({
            "key": key,
            "operator": operator,
            "inputs": list(inputs),
            "arguments": deepcopy(dict(arguments or {})),
        })
        return key

    def item(
        self,
        key: str,
        display_name: str | Mapping[str, str],
        *,
        representations: Mapping[str, str] | None = None,
        children: Iterable[str] = (),
        properties: Mapping[str, Any] | None = None,
    ) -> str:
        self._require_new_key(key, self._item_keys, "item")
        self._document["items"].append({
            "key": key,
            "displayName": deepcopy(display_name),
            "representations": dict(representations or {}),
            "children": list(children),
            "properties": deepcopy(dict(properties or {})),
        })
        return key

    def output(
        self,
        key: str,
        purpose: str,
        items: Iterable[str],
        *,
        properties: Mapping[str, Any] | None = None,
    ) -> None:
        self._document["outputs"].append({
            "key": key,
            "purpose": purpose,
            "items": list(items),
            "properties": deepcopy(dict(properties or {})),
        })

    def relationship(
        self,
        key: str,
        kind: str,
        items: Iterable[str],
        *,
        properties: Mapping[str, Any] | None = None,
    ) -> None:
        self._require_new_key(key, self._relationship_keys, "relationship")
        self._document["relationships"].append({
            "key": key,
            "kind": kind,
            "items": list(items),
            "properties": deepcopy(dict(properties or {})),
        })

    def table(
        self,
        key: str,
        display_name: str | Mapping[str, str],
        *,
        columns: Iterable[Mapping[str, Any]],
        rows: Iterable[Mapping[str, Any]],
    ) -> None:
        self._document["tables"].append({
            "key": key,
            "displayName": deepcopy(display_name),
            "columns": deepcopy(list(columns)),
            "rows": deepcopy(list(rows)),
        })

    def diagnostic(
        self,
        severity: str,
        code: str,
        message: str,
        *,
        parameter_keys: Iterable[str] = (),
        model_key: str = "",
    ) -> None:
        self._document["diagnostics"].append({
            "severity": severity,
            "code": code,
            "message": message,
            "parameterKeys": list(parameter_keys),
            "modelKey": model_key,
        })

    def build(self) -> dict[str, Any]:
        return deepcopy(self._document)

    @staticmethod
    def _require_new_key(key: str, keys: set[str], kind: str) -> None:
        if not isinstance(key, str) or not key:
            raise ValueError(f"{kind} key must be a non-empty string")
        if key in keys:
            raise ValueError(f"duplicate {kind} key: {key}")
        keys.add(key)
