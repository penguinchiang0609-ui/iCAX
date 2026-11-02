import * as flatbuffers from '../runtime/flatbuffers.mjs';
export class MachineJogControl {
    bb = null;
    bb_pos = 0;
    __init(i, bb) {
        this.bb_pos = i;
        this.bb = bb;
        return this;
    }
    static getRootAsMachineJogControl(bb, obj) {
        return (obj || new MachineJogControl()).__init(bb.readInt32(bb.position()) + bb.position(), bb);
    }
    static getSizePrefixedRootAsMachineJogControl(bb, obj) {
        bb.setPosition(bb.position() + flatbuffers.SIZE_PREFIX_LENGTH);
        return (obj || new MachineJogControl()).__init(bb.readInt32(bb.position()) + bb.position(), bb);
    }
    static bufferHasIdentifier(bb) {
        return bb.__has_identifier('F5JC');
    }
    schemaVersion() {
        const offset = this.bb.__offset(this.bb_pos, 4);
        return offset ? this.bb.readUint32(this.bb_pos + offset) : 1;
    }
    token(optionalEncoding) {
        const offset = this.bb.__offset(this.bb_pos, 6);
        return offset ? this.bb.__string(this.bb_pos + offset, optionalEncoding) : null;
    }
    epoch(optionalEncoding) {
        const offset = this.bb.__offset(this.bb_pos, 8);
        return offset ? this.bb.__string(this.bb_pos + offset, optionalEncoding) : null;
    }
    sequence() {
        const offset = this.bb.__offset(this.bb_pos, 10);
        return offset ? this.bb.readUint64(this.bb_pos + offset) : BigInt('0');
    }
    held() {
        const offset = this.bb.__offset(this.bb_pos, 12);
        return offset ? !!this.bb.readInt8(this.bb_pos + offset) : false;
    }
    static startMachineJogControl(builder) {
        builder.startObject(5);
    }
    static addSchemaVersion(builder, schemaVersion) {
        builder.addFieldInt32(0, schemaVersion, 1);
    }
    static addToken(builder, tokenOffset) {
        builder.addFieldOffset(1, tokenOffset, 0);
    }
    static addEpoch(builder, epochOffset) {
        builder.addFieldOffset(2, epochOffset, 0);
    }
    static addSequence(builder, sequence) {
        builder.addFieldInt64(3, sequence, BigInt('0'));
    }
    static addHeld(builder, held) {
        builder.addFieldInt8(4, +held, +false);
    }
    static endMachineJogControl(builder) {
        const offset = builder.endObject();
        builder.requiredField(offset, 6);
        builder.requiredField(offset, 8);
        return offset;
    }
    static finishMachineJogControlBuffer(builder, offset) {
        builder.finish(offset, 'F5JC');
    }
    static finishSizePrefixedMachineJogControlBuffer(builder, offset) {
        builder.finish(offset, 'F5JC', true);
    }
    static createMachineJogControl(builder, schemaVersion, tokenOffset, epochOffset, sequence, held) {
        MachineJogControl.startMachineJogControl(builder);
        MachineJogControl.addSchemaVersion(builder, schemaVersion);
        MachineJogControl.addToken(builder, tokenOffset);
        MachineJogControl.addEpoch(builder, epochOffset);
        MachineJogControl.addSequence(builder, sequence);
        MachineJogControl.addHeld(builder, held);
        return MachineJogControl.endMachineJogControl(builder);
    }
}
