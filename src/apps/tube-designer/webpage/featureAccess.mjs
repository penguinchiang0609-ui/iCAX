export const PRODUCTION_WORKFLOW_ENTITLEMENT = "tube-designer.production-workflow";

export function hasProductionWorkflowAccess(context = {}) {
  const sources = [
    context.featureAccess,
    context.entitlements,
    context.product?.featureAccess,
    context.product?.entitlements,
    context.product?.license?.entitlements,
  ];
  return sources.some((source) => grantsEntitlement(source, PRODUCTION_WORKFLOW_ENTITLEMENT));
}

function grantsEntitlement(source, entitlement) {
  if (Array.isArray(source)) return source.includes(entitlement);
  if (!source || typeof source !== "object") return false;
  const value = source[entitlement];
  return value === true || value === "active" || value?.enabled === true || value?.status === "active";
}
