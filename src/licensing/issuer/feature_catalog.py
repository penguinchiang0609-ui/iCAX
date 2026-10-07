"""Issuer permissions come from the same descriptor as the native client."""
from dataclasses import dataclass
import json
from pathlib import Path
import sys
from types import MappingProxyType


@dataclass(frozen=True)
class Feature:
    id: str
    bit: int
    label: str
    parent: str | None


def _unique_object(pairs):
    result = {}
    for key, value in pairs:
        if key in result:
            raise ValueError("Duplicate permission catalogue field")
        result[key] = value
    return result


def load_catalogue(path=None):
    if path is None:
        path = (Path(sys._MEIPASS) / "features.json" if getattr(sys, "frozen", False)
                else Path(__file__).resolve().parent.parent / "features.json")
    raw = Path(path).read_bytes()
    if len(raw) > 65536:
        raise ValueError("Permission catalogue exceeds size limit")
    catalogue = json.loads(raw, object_pairs_hook=_unique_object)
    if (not isinstance(catalogue, dict)
            or set(catalogue) != {"schemaVersion", "certificateFormat", "features"}
            or type(catalogue["schemaVersion"]) is not int or catalogue["schemaVersion"] != 1
            or catalogue["certificateFormat"] != "TDLIC003"
            or not isinstance(catalogue["features"], list) or not catalogue["features"]):
        raise ValueError("Unsupported permission catalogue")
    features, ids, bits = [], set(), set()
    for item in catalogue["features"]:
        if not isinstance(item, dict) or set(item) != {"id", "bit", "label", "parent"}:
            raise ValueError("Invalid permission descriptor")
        identifier, bit, label, parent = (item[key] for key in ("id", "bit", "label", "parent"))
        if (not isinstance(identifier, str) or not identifier or identifier in ids
                or type(bit) is not int or not 0 < bit <= 0x80000000 or bit & (bit - 1) or bit in bits
                or not isinstance(label, str) or not label.strip()
                or (parent is not None and not isinstance(parent, str))):
            raise ValueError("Invalid or duplicate permission descriptor")
        ids.add(identifier)
        bits.add(bit)
        features.append(Feature(identifier, bit, label, parent))
    by_id = {feature.id: feature for feature in features}
    for feature in features:
        if feature.parent is None:
            if not feature.id.startswith("page."):
                raise ValueError("Top-level permission must be a page")
        elif (feature.parent not in by_id or by_id[feature.parent].parent is not None
              or not feature.id.startswith(feature.parent.removeprefix("page.") + ".")):
            raise ValueError("Operation permission must belong to its page")
    return tuple(features)


FEATURES = load_catalogue()
BY_ID = MappingProxyType({feature.id: feature for feature in FEATURES})
ALL_FEATURES = sum(feature.bit for feature in FEATURES)
CERTIFICATE_FORMAT = "TDLIC003"


def validate_mask(mask):
    if type(mask) is not int or not 0 < mask <= 0xffffffff or mask & ~ALL_FEATURES:
        raise ValueError("请选择有效的页面和操作权限")
    for feature in FEATURES:
        if mask & feature.bit and feature.parent and not mask & BY_ID[feature.parent].bit:
            raise ValueError(f"“{feature.label}”还需授权“{BY_ID[feature.parent].label}”")
    return mask


def labels_for_mask(mask):
    validate_mask(mask)
    return tuple(feature.label for feature in FEATURES if mask & feature.bit)
