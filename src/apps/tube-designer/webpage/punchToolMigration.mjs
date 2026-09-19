// Shared current-schema cloning and placement defaults for punch recipes.

const clone = value => structuredClone(value);

export const BRANCH_PLACEMENT_KEYS = Object.freeze([
  "angle", "azimuth", "roll", "offsetY", "offsetZ", "direction", "length",
]);

export const BRANCH_PLACEMENT_DEFAULTS = Object.freeze({
  angle: 90, azimuth: 0, roll: 0, offsetY: 0, offsetZ: 0, direction: "through", length: 120,
});

export const END_PROFILE_PLACEMENT_KEYS = Object.freeze([
  "angle", "azimuth", "roll", "axialOffset", "offsetY", "offsetZ",
]);

export function migratePunchRecord(value = {}) {
  return clone(value ?? {});
}

export function migratePunchRecipe(value = {}) {
  const recipe = clone(value ?? {});
  if (Array.isArray(recipe.features)) recipe.features = recipe.features.map(migratePunchRecord);
  if (recipe.ends && typeof recipe.ends === "object") {
    recipe.ends = Object.fromEntries(
      Object.entries(recipe.ends).map(([key, item]) => [key, migratePunchRecord(item)]),
    );
  }
  return recipe;
}
