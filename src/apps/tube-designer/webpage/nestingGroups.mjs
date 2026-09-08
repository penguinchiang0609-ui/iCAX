export function groupNestingPlans(plans = []) {
  const groups = new Map();
  plans.forEach((plan, index) => {
    const label = String(plan.profile ?? plan.profileName ?? plan.stockTypeId ?? "截面未指定");
    const key = String(plan.profileKey || label);
    if (!groups.has(key)) groups.set(key, { key, label, entries: [] });
    groups.get(key).entries.push({ plan, index });
  });
  return [...groups.values()];
}
