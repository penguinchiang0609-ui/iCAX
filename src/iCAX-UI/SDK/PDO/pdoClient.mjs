import { makePDOID } from "../Mailbox/commandRoute.mjs";

export class PDOClient {
  constructor(descriptor = {}, bridge = globalThis.icax) {
    this.descriptor = descriptor ?? {};
    this.bridge = bridge ?? null;
  }

  get enabled() {
    return Boolean(this.descriptor.enabled);
  }

  makeId(typeName, instanceName) {
    return makePDOID(typeName, instanceName);
  }

  findDeclaration(typeName, instanceName) {
    const id = this.makeId(typeName, instanceName);
    return (this.descriptor.declarations ?? []).find((decl) => String(decl.id) === id) ?? null;
  }

  async withRead(typeName, instanceName, reader) {
    if (!this.enabled) {
      return null;
    }
    if (typeof reader !== "function") {
      throw new TypeError("PDO reader must be a function");
    }
    if (!this.bridge?.pdo?.withRead) {
      throw new Error("Host bridge does not provide PDO read leases");
    }

    const declaration = this.findDeclaration(typeName, instanceName);
    if (!declaration) {
      throw new Error(`PDO declaration is not found: ${typeName}/${instanceName}`);
    }

    return this.bridge.pdo.withRead({
      arenaName: this.descriptor.sharedArenaName,
      id: declaration.id,
      version: declaration.version,
      payloadSize: declaration.payloadSize,
    }, reader);
  }
}
