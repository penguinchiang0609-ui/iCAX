import * as flatbuffers from '../runtime/flatbuffers.mjs';
export class MachineJogResult {
    bb = null;
    bb_pos = 0;
    __init(i, bb) {
        this.bb_pos = i;
        this.bb = bb;
        return this;
    }
    static getRootAsMachineJogResult(bb, obj) {
        return (obj || new MachineJogResult()).__init(bb.readInt32(bb.position()) + bb.position(), bb);
    }
    static getSizePrefixedRootAsMachineJogResult(bb, obj) {
        bb.setPosition(bb.position() + flatbuffers.SIZE_PREFIX_LENGTH);
        return (obj || new MachineJogResult()).__init(bb.readInt32(bb.position()) + bb.position(), bb);
    }
    static bufferHasIdentifier(bb) {
        return bb.__has_identifier('F5JR');
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
    resultSequence() {
        const offset = this.bb.__offset(this.bb_pos, 10);
        return offset ? this.bb.readUint64(this.bb_pos + offset) : BigInt('0');
    }
    processedSequence() {
        const offset = this.bb.__offset(this.bb_pos, 12);
        return offset ? this.bb.readUint64(this.bb_pos + offset) : BigInt('0');
    }
    mutationSequence() {
        const offset = this.bb.__offset(this.bb_pos, 14);
        return offset ? this.bb.readUint64(this.bb_pos + offset) : BigInt('0');
    }
    status(optionalEncoding) {
        const offset = this.bb.__offset(this.bb_pos, 16);
        return offset ? this.bb.__string(this.bb_pos + offset, optionalEncoding) : null;
    }
    reason(optionalEncoding) {
        const offset = this.bb.__offset(this.bb_pos, 18);
        return offset ? this.bb.__string(this.bb_pos + offset, optionalEncoding) : null;
    }
    positions(index) {
        const offset = this.bb.__offset(this.bb_pos, 20);
        return offset ? this.bb.readFloat64(this.bb.__vector(this.bb_pos + offset) + index * 8) : 0;
    }
    positionsLength() {
        const offset = this.bb.__offset(this.bb_pos, 20);
        return offset ? this.bb.__vector_len(this.bb_pos + offset) : 0;
    }
    positionsArray() {
        const offset = this.bb.__offset(this.bb_pos, 20);
        return offset ? new Float64Array(this.bb.bytes().buffer, this.bb.bytes().byteOffset + this.bb.__vector(this.bb_pos + offset), this.bb.__vector_len(this.bb_pos + offset)) : null;
    }
    worldMatrices(index) {
        const offset = this.bb.__offset(this.bb_pos, 22);
        return offset ? this.bb.readFloat64(this.bb.__vector(this.bb_pos + offset) + index * 8) : 0;
    }
    worldMatricesLength() {
        const offset = this.bb.__offset(this.bb_pos, 22);
        return offset ? this.bb.__vector_len(this.bb_pos + offset) : 0;
    }
    worldMatricesArray() {
        const offset = this.bb.__offset(this.bb_pos, 22);
        return offset ? new Float64Array(this.bb.bytes().buffer, this.bb.bytes().byteOffset + this.bb.__vector(this.bb_pos + offset), this.bb.__vector_len(this.bb_pos + offset)) : null;
    }
    ticks() {
        const offset = this.bb.__offset(this.bb_pos, 24);
        return offset ? this.bb.readUint64(this.bb_pos + offset) : BigInt('0');
    }
    collisionMs() {
        const offset = this.bb.__offset(this.bb_pos, 26);
        return offset ? this.bb.readFloat64(this.bb_pos + offset) : 0.0;
    }
    tickMs() {
        const offset = this.bb.__offset(this.bb_pos, 28);
        return offset ? this.bb.readFloat64(this.bb_pos + offset) : 0.0;
    }
    intervalMs() {
        const offset = this.bb.__offset(this.bb_pos, 30);
        return offset ? this.bb.readFloat64(this.bb_pos + offset) : 0.0;
    }
    static startMachineJogResult(builder) {
        builder.startObject(14);
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
    static addResultSequence(builder, resultSequence) {
        builder.addFieldInt64(3, resultSequence, BigInt('0'));
    }
    static addProcessedSequence(builder, processedSequence) {
        builder.addFieldInt64(4, processedSequence, BigInt('0'));
    }
    static addMutationSequence(builder, mutationSequence) {
        builder.addFieldInt64(5, mutationSequence, BigInt('0'));
    }
    static addStatus(builder, statusOffset) {
        builder.addFieldOffset(6, statusOffset, 0);
    }
    static addReason(builder, reasonOffset) {
        builder.addFieldOffset(7, reasonOffset, 0);
    }
    static addPositions(builder, positionsOffset) {
        builder.addFieldOffset(8, positionsOffset, 0);
    }
    static createPositionsVector(builder, data) {
        builder.startVector(8, data.length, 8);
        for(let i = data.length - 1; i >= 0; i--){
            builder.addFloat64(data[i]);
        }
        return builder.endVector();
    }
    static startPositionsVector(builder, numElems) {
        builder.startVector(8, numElems, 8);
    }
    static addWorldMatrices(builder, worldMatricesOffset) {
        builder.addFieldOffset(9, worldMatricesOffset, 0);
    }
    static createWorldMatricesVector(builder, data) {
        builder.startVector(8, data.length, 8);
        for(let i = data.length - 1; i >= 0; i--){
            builder.addFloat64(data[i]);
        }
        return builder.endVector();
    }
    static startWorldMatricesVector(builder, numElems) {
        builder.startVector(8, numElems, 8);
    }
    static addTicks(builder, ticks) {
        builder.addFieldInt64(10, ticks, BigInt('0'));
    }
    static addCollisionMs(builder, collisionMs) {
        builder.addFieldFloat64(11, collisionMs, 0.0);
    }
    static addTickMs(builder, tickMs) {
        builder.addFieldFloat64(12, tickMs, 0.0);
    }
    static addIntervalMs(builder, intervalMs) {
        builder.addFieldFloat64(13, intervalMs, 0.0);
    }
    static endMachineJogResult(builder) {
        const offset = builder.endObject();
        builder.requiredField(offset, 6);
        builder.requiredField(offset, 8);
        builder.requiredField(offset, 16);
        return offset;
    }
    static finishMachineJogResultBuffer(builder, offset) {
        builder.finish(offset, 'F5JR');
    }
    static finishSizePrefixedMachineJogResultBuffer(builder, offset) {
        builder.finish(offset, 'F5JR', true);
    }
    static createMachineJogResult(builder, schemaVersion, tokenOffset, epochOffset, resultSequence, processedSequence, mutationSequence, statusOffset, reasonOffset, positionsOffset, worldMatricesOffset, ticks, collisionMs, tickMs, intervalMs) {
        MachineJogResult.startMachineJogResult(builder);
        MachineJogResult.addSchemaVersion(builder, schemaVersion);
        MachineJogResult.addToken(builder, tokenOffset);
        MachineJogResult.addEpoch(builder, epochOffset);
        MachineJogResult.addResultSequence(builder, resultSequence);
        MachineJogResult.addProcessedSequence(builder, processedSequence);
        MachineJogResult.addMutationSequence(builder, mutationSequence);
        MachineJogResult.addStatus(builder, statusOffset);
        MachineJogResult.addReason(builder, reasonOffset);
        MachineJogResult.addPositions(builder, positionsOffset);
        MachineJogResult.addWorldMatrices(builder, worldMatricesOffset);
        MachineJogResult.addTicks(builder, ticks);
        MachineJogResult.addCollisionMs(builder, collisionMs);
        MachineJogResult.addTickMs(builder, tickMs);
        MachineJogResult.addIntervalMs(builder, intervalMs);
        return MachineJogResult.endMachineJogResult(builder);
    }
}
