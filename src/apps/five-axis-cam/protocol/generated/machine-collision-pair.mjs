import * as flatbuffers from '../runtime/flatbuffers.mjs';
import { MachineCollisionCollider } from './machine-collision-collider.mjs';
export class MachineCollisionPair {
    bb = null;
    bb_pos = 0;
    __init(i, bb) {
        this.bb_pos = i;
        this.bb = bb;
        return this;
    }
    static getRootAsMachineCollisionPair(bb, obj) {
        return (obj || new MachineCollisionPair()).__init(bb.readInt32(bb.position()) + bb.position(), bb);
    }
    static getSizePrefixedRootAsMachineCollisionPair(bb, obj) {
        bb.setPosition(bb.position() + flatbuffers.SIZE_PREFIX_LENGTH);
        return (obj || new MachineCollisionPair()).__init(bb.readInt32(bb.position()) + bb.position(), bb);
    }
    first(obj) {
        const offset = this.bb.__offset(this.bb_pos, 4);
        return offset ? (obj || new MachineCollisionCollider()).__init(this.bb.__indirect(this.bb_pos + offset), this.bb) : null;
    }
    second(obj) {
        const offset = this.bb.__offset(this.bb_pos, 6);
        return offset ? (obj || new MachineCollisionCollider()).__init(this.bb.__indirect(this.bb_pos + offset), this.bb) : null;
    }
    static startMachineCollisionPair(builder) {
        builder.startObject(2);
    }
    static addFirst(builder, firstOffset) {
        builder.addFieldOffset(0, firstOffset, 0);
    }
    static addSecond(builder, secondOffset) {
        builder.addFieldOffset(1, secondOffset, 0);
    }
    static endMachineCollisionPair(builder) {
        const offset = builder.endObject();
        builder.requiredField(offset, 4);
        builder.requiredField(offset, 6);
        return offset;
    }
}
