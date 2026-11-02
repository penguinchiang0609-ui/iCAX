import * as flatbuffers from '../runtime/flatbuffers.mjs';
export class MachineIKTarget {
    bb = null;
    bb_pos = 0;
    __init(i, bb) {
        this.bb_pos = i;
        this.bb = bb;
        return this;
    }
    static getRootAsMachineIKTarget(bb, obj) {
        return (obj || new MachineIKTarget()).__init(bb.readInt32(bb.position()) + bb.position(), bb);
    }
    static getSizePrefixedRootAsMachineIKTarget(bb, obj) {
        bb.setPosition(bb.position() + flatbuffers.SIZE_PREFIX_LENGTH);
        return (obj || new MachineIKTarget()).__init(bb.readInt32(bb.position()) + bb.position(), bb);
    }
    static bufferHasIdentifier(bb) {
        return bb.__has_identifier('F5IT');
    }
    schemaVersion() {
        const offset = this.bb.__offset(this.bb_pos, 4);
        return offset ? this.bb.readUint32(this.bb_pos + offset) : 1;
    }
    editToken(optionalEncoding) {
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
    target(index) {
        const offset = this.bb.__offset(this.bb_pos, 12);
        return offset ? this.bb.readFloat64(this.bb.__vector(this.bb_pos + offset) + index * 8) : 0;
    }
    targetLength() {
        const offset = this.bb.__offset(this.bb_pos, 12);
        return offset ? this.bb.__vector_len(this.bb_pos + offset) : 0;
    }
    targetArray() {
        const offset = this.bb.__offset(this.bb_pos, 12);
        return offset ? new Float64Array(this.bb.bytes().buffer, this.bb.bytes().byteOffset + this.bb.__vector(this.bb_pos + offset), this.bb.__vector_len(this.bb_pos + offset)) : null;
    }
    jointPosition() {
        const offset = this.bb.__offset(this.bb_pos, 14);
        return offset ? this.bb.readFloat64(this.bb_pos + offset) : null;
    }
    static startMachineIKTarget(builder) {
        builder.startObject(6);
    }
    static addSchemaVersion(builder, schemaVersion) {
        builder.addFieldInt32(0, schemaVersion, 1);
    }
    static addEditToken(builder, editTokenOffset) {
        builder.addFieldOffset(1, editTokenOffset, 0);
    }
    static addEpoch(builder, epochOffset) {
        builder.addFieldOffset(2, epochOffset, 0);
    }
    static addSequence(builder, sequence) {
        builder.addFieldInt64(3, sequence, BigInt('0'));
    }
    static addTarget(builder, targetOffset) {
        builder.addFieldOffset(4, targetOffset, 0);
    }
    static createTargetVector(builder, data) {
        builder.startVector(8, data.length, 8);
        for(let i = data.length - 1; i >= 0; i--){
            builder.addFloat64(data[i]);
        }
        return builder.endVector();
    }
    static startTargetVector(builder, numElems) {
        builder.startVector(8, numElems, 8);
    }
    static addJointPosition(builder, jointPosition) {
        builder.addFieldFloat64(5, jointPosition, null);
    }
    static endMachineIKTarget(builder) {
        const offset = builder.endObject();
        builder.requiredField(offset, 6);
        builder.requiredField(offset, 8);
        builder.requiredField(offset, 12);
        return offset;
    }
    static finishMachineIKTargetBuffer(builder, offset) {
        builder.finish(offset, 'F5IT');
    }
    static finishSizePrefixedMachineIKTargetBuffer(builder, offset) {
        builder.finish(offset, 'F5IT', true);
    }
    static createMachineIKTarget(builder, schemaVersion, editTokenOffset, epochOffset, sequence, targetOffset, jointPosition) {
        MachineIKTarget.startMachineIKTarget(builder);
        MachineIKTarget.addSchemaVersion(builder, schemaVersion);
        MachineIKTarget.addEditToken(builder, editTokenOffset);
        MachineIKTarget.addEpoch(builder, epochOffset);
        MachineIKTarget.addSequence(builder, sequence);
        MachineIKTarget.addTarget(builder, targetOffset);
        if (jointPosition !== null) MachineIKTarget.addJointPosition(builder, jointPosition);
        return MachineIKTarget.endMachineIKTarget(builder);
    }
}
