export const DEFAULT_LINEAR_JOG_STEP_MM = 10;
export const DEFAULT_ANGULAR_JOG_STEP_DEG = 1;

export function getLinearJogStepMm(machine = {}) {
  return getPositiveNumber(machine.linearJogStep, DEFAULT_LINEAR_JOG_STEP_MM);
}

export function getAngularJogStepDeg(machine = {}) {
  return getPositiveNumber(machine.angularJogStep, DEFAULT_ANGULAR_JOG_STEP_DEG);
}

export function isRotaryJointType(type = "") {
  return type === "revolute" || type === "continuous";
}

export function findMachineJoint(machine = {}, jointName = "") {
  const name = String(jointName ?? "").trim();
  const joints = Array.isArray(machine.joints) ? machine.joints : [];
  return joints.find((joint) => String(joint?.jointName ?? joint?.name ?? "").trim() === name) ?? null;
}

function getPositiveNumber(value, fallback) {
  const number = Number(value);
  return Number.isFinite(number) && number > 0 ? number : fallback;
}
