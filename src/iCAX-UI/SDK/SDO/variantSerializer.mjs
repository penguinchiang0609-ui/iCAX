const typedMarker = Symbol("icax.variant");

export function typedVariant(type, value) {
  if (!type) {
    throw new TypeError("variant type is required");
  }
  return { [typedMarker]: true, type, value };
}

export function serializeVariantText(value) {
  return JSON.stringify(toVariantNode(value));
}

export function deserializeVariantText(text) {
  if (!text) {
    return {};
  }
  return fromVariantNode(JSON.parse(text));
}

function toVariantNode(value) {
  if (value && value[typedMarker]) {
    return {
      __variant_type: value.type,
      value: encodeTypedValue(value.type, value.value),
    };
  }

  if (value === null || value === undefined) {
    return { __variant_type: "null", value: null };
  }
  if (typeof value === "boolean") {
    return { __variant_type: "bool", value };
  }
  if (typeof value === "bigint") {
    if (value > BigInt(Number.MAX_SAFE_INTEGER)) {
      throw new TypeError("BigInt Variant values above Number.MAX_SAFE_INTEGER must be passed as strings");
    }
    return { __variant_type: "unsigned_long_long", value: Number(value) };
  }
  if (typeof value === "number") {
    if (!Number.isFinite(value)) {
      throw new TypeError("Variant number must be finite");
    }
    if (Number.isInteger(value) && value >= -2147483648 && value <= 2147483647) {
      return { __variant_type: "int", value };
    }
    return { __variant_type: "double", value };
  }
  if (typeof value === "string") {
    return { __variant_type: "string", value };
  }
  if (Array.isArray(value)) {
    return { __variant_type: "Array", value: value.map((item) => toVariantNode(item)) };
  }
  if (typeof value === "object") {
    const object = {};
    for (const [key, item] of Object.entries(value)) {
      if (item !== undefined) {
        object[key] = toVariantNode(item);
      }
    }
    return { __variant_type: "Object", value: object };
  }

  throw new TypeError(`Unsupported Variant value type: ${typeof value}`);
}

function encodeTypedValue(type, value) {
  if (type === "Object") {
    const object = {};
    for (const [key, item] of Object.entries(value ?? {})) {
      object[key] = toVariantNode(item);
    }
    return object;
  }
  if (type === "Array") {
    return (value ?? []).map((item) => toVariantNode(item));
  }
  if (type === "uuid" || type === "string") {
    return String(value ?? "");
  }
  if (type === "unsigned_long_long" || type === "long_long") {
    const numericValue = typeof value === "bigint" ? value : BigInt(value ?? 0);
    if (numericValue > BigInt(Number.MAX_SAFE_INTEGER) || numericValue < BigInt(Number.MIN_SAFE_INTEGER)) {
      throw new TypeError(`${type} Variant values above JS safe integer range must be passed as strings`);
    }
    return Number(numericValue);
  }
  return value;
}

function fromVariantNode(node) {
  if (!node || typeof node !== "object" || !("__variant_type" in node)) {
    throw new TypeError("Invalid Variant node");
  }

  const type = node.__variant_type;
  const value = node.value;

  switch (type) {
    case "null":
      return null;
    case "bool":
      return Boolean(value);
    case "char":
    case "uint8_t":
    case "short":
    case "unsigned_short":
    case "int":
    case "unsigned_int":
    case "float":
    case "double":
      return Number(value);
    case "long_long":
    case "unsigned_long_long":
      return parseLargeInteger(value);
    case "string":
    case "uuid":
      return String(value ?? "");
    case "Array":
      return (value ?? []).map((item) => fromVariantNode(item));
    case "Object": {
      const object = {};
      for (const [key, item] of Object.entries(value ?? {})) {
        object[key] = fromVariantNode(item);
      }
      return object;
    }
    default:
      if (Array.isArray(value)) {
        return value.map((item) => Number(item));
      }
      return value;
  }
}

function parseLargeInteger(value) {
  const text = String(value ?? "0");
  const parsed = BigInt(text);
  if (parsed <= BigInt(Number.MAX_SAFE_INTEGER) && parsed >= BigInt(Number.MIN_SAFE_INTEGER)) {
    return Number(parsed);
  }
  return text;
}
