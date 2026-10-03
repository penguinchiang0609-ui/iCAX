"""Validate the existing product profile-library presentation contract."""
from __future__ import annotations

from contextlib import contextmanager
from contextvars import ContextVar
from typing import Any

_CURRENT_TEMPLATE = ContextVar("icax_product_profile_descriptor", default=None)
_CONSUMED_ROLES = ContextVar("icax_product_profile_consumed_roles", default=None)


@contextmanager
def _profile_validation_scope(template: dict[str, Any]):
    """Bind the host descriptor while a product actually consumes profiles."""
    token = _CURRENT_TEMPLATE.set(template)
    roles: set[str] = set()
    roles_token = _CONSUMED_ROLES.set(roles)
    try:
        yield roles
    finally:
        _CONSUMED_ROLES.reset(roles_token)
        _CURRENT_TEMPLATE.reset(token)


def _constraints(field: dict[str, Any]) -> tuple[dict[str, Any], set[str]]:
    presentation = field.get("presentation", {})
    if not isinstance(presentation, dict):
        raise ValueError("parameter presentation must be an object")
    constraints = presentation.get("profileConstraints", {})
    if not isinstance(constraints, dict):
        raise ValueError("profileConstraints must be an object")
    kinds = constraints.get("sectionKinds")
    if "sectionKinds" in constraints and (not isinstance(kinds, list) or not kinds
            or any(not isinstance(kind, str) or not kind for kind in kinds)
            or len(set(kinds)) != len(kinds)):
        raise ValueError("profileConstraints.sectionKinds requires distinct non-empty strings")
    whitelist = set(kinds or [])
    if "hollow" in constraints and type(constraints["hollow"]) is not bool:
        raise ValueError("profileConstraints.hollow must be a boolean")
    for name in ("minimumContourCount", "maximumContourCount"):
        value = constraints.get(name)
        if name in constraints and (type(value) is not int or not 1 <= value <= 1000):
            raise ValueError(f"profileConstraints.{name} must be an integer between one and 1000")
    minimum, maximum = constraints.get("minimumContourCount"), constraints.get("maximumContourCount")
    if minimum is not None and maximum is not None and minimum > maximum:
        raise ValueError("profileConstraints has an inverted contour count range")
    if whitelist:
        for choice in field.get("choices", []):
            if not isinstance(choice, dict) or choice.get("value") not in whitelist:
                raise ValueError("profileConstraints.sectionKinds excludes a declared profile choice")
    return constraints, whitelist


def _profile_fields(template: dict[str, Any]):
    """Validate declarations without interpreting parameter conditions."""

    fields = template.get("parameters", [])
    if not isinstance(fields, list):
        raise ValueError("template parameters must be an array")
    by_key = {}
    for field in fields:
        if not isinstance(field, dict) or not isinstance(field.get("key"), str):
            raise ValueError("template parameter requires a key")
        _constraints(field)
        if field["key"] in by_key:
            raise ValueError("duplicate template parameter")
        by_key[field["key"]] = field
    extensions = template.get("extensions", {})
    if not isinstance(extensions, dict):
        raise ValueError("template extensions must be an object")
    roles = extensions.get("resourceRoles", {})
    if not isinstance(roles, dict):
        raise ValueError("resourceRoles must be an object")
    profiles = roles.get("profiles", {})
    if not isinstance(profiles, dict):
        raise ValueError("resourceRoles.profiles must be an object")
    for role, declaration in profiles.items():
        if (not isinstance(role, str) or not role or len(role) > 80
                or not isinstance(declaration, dict) or declaration.get("parameter") not in by_key):
            raise ValueError("profile resource role references a missing parameter")
        field = by_key[declaration["parameter"]]
        if field.get("valueType") != "enum":
            raise ValueError("profile resource role must reference an enum parameter")
    return by_key, profiles


def validate_product_profile_parameters(template: dict[str, Any], parameters: dict[str, Any]) -> None:
    """Validate the envelope; retain inactive profile drafts without mutation.

    Section and contour restrictions are checked by the actual catalog lookup,
    so closed parent options do not make their saved child drafts applicable.
    The host separately normalizes and validates all raw enum parameter values.
    """
    _, profiles = _profile_fields(template)
    overrides = parameters.get("tubeDesignerProfileOverrides", {})
    if not isinstance(overrides, dict) or len(overrides) > 64:
        raise ValueError("tubeDesignerProfileOverrides must be an object with at most 64 roles")
    for role, snapshot in overrides.items():
        if role not in profiles:
            raise ValueError("product uses an undeclared profile resource role")
        if not isinstance(snapshot, dict):
            raise ValueError("profile override must be an object")


def validate_selected_product_profile(role: str, snapshot: dict[str, Any]) -> None:
    """Enforce the one declaration when load_profile consumes an override."""
    template = _CURRENT_TEMPLATE.get()
    if template is None:
        raise ValueError("product profile lookup requires the host template descriptor")
    by_key, profiles = _profile_fields(template)
    if role not in profiles:
        raise ValueError("product uses an undeclared profile resource role")
    constraints, whitelist = _constraints(by_key[profiles[role]["parameter"]])
    if not whitelist:
        raise ValueError("profile resource role requires a declared sectionKinds whitelist")
    if (not isinstance(snapshot, dict) or snapshot.get("schema") != "icax.imported-tube-profile"
            or type(snapshot.get("schemaVersion")) is not int or snapshot["schemaVersion"] != 1
            or snapshot.get("kind") not in ("fixed-section", "profile-package")):
        raise ValueError("unsupported product profile snapshot schema")
    section_kind = snapshot.get("sectionKind")
    if not isinstance(section_kind, str) or section_kind not in whitelist:
        raise ValueError(f"profile section kind is not applicable to role {role}")
    contours = snapshot.get("contours")
    if not isinstance(contours, list) or not 1 <= len(contours) <= 1000:
        raise ValueError("profile override requires non-empty contours")
    count = len(contours)
    if "contourCount" in snapshot and (type(snapshot["contourCount"]) is not int
            or snapshot["contourCount"] != count):
        raise ValueError("profile contourCount does not match its contours")
    hollow = count > 1
    if "hollow" in snapshot and (type(snapshot["hollow"]) is not bool or snapshot["hollow"] != hollow):
        raise ValueError("profile hollow flag does not match its contours")
    if "hollow" in constraints and constraints["hollow"] != hollow:
        raise ValueError(f"profile hollow section is not applicable to role {role}")
    if count < constraints.get("minimumContourCount", 1):
        raise ValueError(f"profile has too few contours for role {role}")
    if "maximumContourCount" in constraints and count > constraints["maximumContourCount"]:
        raise ValueError(f"profile has too many contours for role {role}")
    _CONSUMED_ROLES.get().add(role)
