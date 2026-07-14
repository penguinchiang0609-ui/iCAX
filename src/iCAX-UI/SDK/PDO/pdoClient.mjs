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

  findDeclarationById(id) {
    const textId = String(id ?? "");
    if (!textId) {
      return null;
    }
    return (this.descriptor.declarations ?? []).find((decl) => String(decl.id) === textId) ?? null;
  }

  async withReadDescriptor(descriptor, reader) {
    if (!this.enabled) {
      return null;
    }
    if (!descriptor?.id) {
      throw new TypeError("PDO descriptor.id is required");
    }
    if (typeof reader !== "function") {
      throw new TypeError("PDO reader must be a function");
    }
    if (!this.bridge?.pdo?.withRead) {
      throw new Error("Host bridge does not provide PDO read leases");
    }

    const declaration = this.findDeclarationById(descriptor.id);
    const version = descriptor.version ?? declaration?.version;
    const payloadSize = descriptor.payloadSize ?? descriptor.payloadCapacity ?? declaration?.payloadSize;
    if (!version) {
      throw new Error(`PDO descriptor.version is required: ${descriptor.id}`);
    }
    if (!payloadSize) {
      throw new Error(`PDO descriptor.payloadSize is required: ${descriptor.id}`);
    }

    return this.bridge.pdo.withRead({
      arenaName: descriptor.arenaName ?? this.descriptor.sharedArenaName,
      id: String(descriptor.id),
      version,
      payloadSize,
    }, reader);
  }

  async getMetaDescriptor(descriptor) {
    if (!this.enabled) {
      return null;
    }
    if (!descriptor?.id) {
      throw new TypeError("PDO descriptor.id is required");
    }
    if (!this.bridge?.pdo?.getMeta) {
      throw new Error("Host bridge does not provide PDO slot metadata");
    }

    const declaration = this.findDeclarationById(descriptor.id);
    const version = descriptor.version ?? declaration?.version;
    const payloadSize = descriptor.payloadSize ?? descriptor.payloadCapacity ?? declaration?.payloadSize;
    if (!version) {
      throw new Error(`PDO descriptor.version is required: ${descriptor.id}`);
    }
    if (!payloadSize) {
      throw new Error(`PDO descriptor.payloadSize is required: ${descriptor.id}`);
    }

    return this.bridge.pdo.getMeta({
      arenaName: descriptor.arenaName ?? this.descriptor.sharedArenaName,
      id: String(descriptor.id),
      version,
      payloadSize,
    });
  }

  async withWriteDescriptor(descriptor, writer) {
    if (!this.enabled) {
      return false;
    }
    if (!descriptor?.id) {
      throw new TypeError("PDO descriptor.id is required");
    }
    if (typeof writer !== "function") {
      throw new TypeError("PDO writer must be a function");
    }
    if (!this.bridge?.pdo?.withWrite) {
      throw new Error("Host bridge does not provide PDO write leases");
    }

    const declaration = this.findDeclarationById(descriptor.id);
    const version = descriptor.version ?? declaration?.version;
    const payloadSize = descriptor.payloadSize ?? descriptor.payloadCapacity ?? declaration?.payloadSize;
    const dataVersion = descriptor.dataVersion;
    if (!version) {
      throw new Error(`PDO descriptor.version is required: ${descriptor.id}`);
    }
    if (!payloadSize) {
      throw new Error(`PDO descriptor.payloadSize is required: ${descriptor.id}`);
    }
    if (!dataVersion) {
      throw new Error(`PDO descriptor.dataVersion is required: ${descriptor.id}`);
    }

    return this.bridge.pdo.withWrite({
      arenaName: descriptor.arenaName ?? this.descriptor.sharedArenaName,
      id: String(descriptor.id),
      version,
      payloadSize,
      dataVersion: String(dataVersion),
    }, writer);
  }
}
