import * as flatbuffers from '../runtime/flatbuffers.mjs';
export class MachineCollisionCollider {
    bb = null;
    bb_pos = 0;
    __init(i, bb) {
        this.bb_pos = i;
        this.bb = bb;
        return this;
    }
    static getRootAsMachineCollisionCollider(bb, obj) {
        return (obj || new MachineCollisionCollider()).__init(bb.readInt32(bb.position()) + bb.position(), bb);
    }
    static getSizePrefixedRootAsMachineCollisionCollider(bb, obj) {
        bb.setPosition(bb.position() + flatbuffers.SIZE_PREFIX_LENGTH);
        return (obj || new MachineCollisionCollider()).__init(bb.readInt32(bb.position()) + bb.position(), bb);
    }
    ownerElementId(optionalEncoding) {
        const offset = this.bb.__offset(this.bb_pos, 4);
        return offset ? this.bb.__string(this.bb_pos + offset, optionalEncoding) : null;
    }
    ownerElementName(optionalEncoding) {
        const offset = this.bb.__offset(this.bb_pos, 6);
        return offset ? this.bb.__string(this.bb_pos + offset, optionalEncoding) : null;
    }
    boxId(optionalEncoding) {
        const offset = this.bb.__offset(this.bb_pos, 8);
        return offset ? this.bb.__string(this.bb_pos + offset, optionalEncoding) : null;
    }
    boxName(optionalEncoding) {
        const offset = this.bb.__offset(this.bb_pos, 10);
        return offset ? this.bb.__string(this.bb_pos + offset, optionalEncoding) : null;
    }
    boxIndex() {
        const offset = this.bb.__offset(this.bb_pos, 12);
        return offset ? this.bb.readInt32(this.bb_pos + offset) : -1;
    }
    representation(optionalEncoding) {
        const offset = this.bb.__offset(this.bb_pos, 14);
        return offset ? this.bb.__string(this.bb_pos + offset, optionalEncoding) : null;
    }
    editable() {
        const offset = this.bb.__offset(this.bb_pos, 16);
        return offset ? !!this.bb.readInt8(this.bb_pos + offset) : false;
    }
    static startMachineCollisionCollider(builder) {
        builder.startObject(7);
    }
    static addOwnerElementId(builder, ownerElementIdOffset) {
        builder.addFieldOffset(0, ownerElementIdOffset, 0);
    }
    static addOwnerElementName(builder, ownerElementNameOffset) {
        builder.addFieldOffset(1, ownerElementNameOffset, 0);
    }
    static addBoxId(builder, boxIdOffset) {
        builder.addFieldOffset(2, boxIdOffset, 0);
    }
    static addBoxName(builder, boxNameOffset) {
        builder.addFieldOffset(3, boxNameOffset, 0);
    }
    static addBoxIndex(builder, boxIndex) {
        builder.addFieldInt32(4, boxIndex, -1);
    }
    static addRepresentation(builder, representationOffset) {
        builder.addFieldOffset(5, representationOffset, 0);
    }
    static addEditable(builder, editable) {
        builder.addFieldInt8(6, +editable, +false);
    }
    static endMachineCollisionCollider(builder) {
        const offset = builder.endObject();
        builder.requiredField(offset, 4);
        builder.requiredField(offset, 6);
        builder.requiredField(offset, 10);
        builder.requiredField(offset, 14);
        return offset;
    }
    static createMachineCollisionCollider(builder, ownerElementIdOffset, ownerElementNameOffset, boxIdOffset, boxNameOffset, boxIndex, representationOffset, editable) {
        MachineCollisionCollider.startMachineCollisionCollider(builder);
        MachineCollisionCollider.addOwnerElementId(builder, ownerElementIdOffset);
        MachineCollisionCollider.addOwnerElementName(builder, ownerElementNameOffset);
        MachineCollisionCollider.addBoxId(builder, boxIdOffset);
        MachineCollisionCollider.addBoxName(builder, boxNameOffset);
        MachineCollisionCollider.addBoxIndex(builder, boxIndex);
        MachineCollisionCollider.addRepresentation(builder, representationOffset);
        MachineCollisionCollider.addEditable(builder, editable);
        return MachineCollisionCollider.endMachineCollisionCollider(builder);
    }
}
