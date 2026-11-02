import * as flatbuffers from '../runtime/flatbuffers.mjs';
import { MachineCollisionPair } from './machine-collision-pair.mjs';
export class MachineIKResult {
    bb = null;
    bb_pos = 0;
    __init(i, bb) {
        this.bb_pos = i;
        this.bb = bb;
        return this;
    }
    static getRootAsMachineIKResult(bb, obj) {
        return (obj || new MachineIKResult()).__init(bb.readInt32(bb.position()) + bb.position(), bb);
    }
    static getSizePrefixedRootAsMachineIKResult(bb, obj) {
        bb.setPosition(bb.position() + flatbuffers.SIZE_PREFIX_LENGTH);
        return (obj || new MachineIKResult()).__init(bb.readInt32(bb.position()) + bb.position(), bb);
    }
    static bufferHasIdentifier(bb) {
        return bb.__has_identifier('F5IR');
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
    resultSequence() {
        const offset = this.bb.__offset(this.bb_pos, 10);
        return offset ? this.bb.readUint64(this.bb_pos + offset) : BigInt('0');
    }
    processedSequence() {
        const offset = this.bb.__offset(this.bb_pos, 12);
        return offset ? this.bb.readUint64(this.bb_pos + offset) : BigInt('0');
    }
    appliedSequence() {
        const offset = this.bb.__offset(this.bb_pos, 14);
        return offset ? this.bb.readUint64(this.bb_pos + offset) : BigInt('0');
    }
    mutationSequence() {
        const offset = this.bb.__offset(this.bb_pos, 16);
        return offset ? this.bb.readUint64(this.bb_pos + offset) : BigInt('0');
    }
    status(optionalEncoding) {
        const offset = this.bb.__offset(this.bb_pos, 18);
        return offset ? this.bb.__string(this.bb_pos + offset, optionalEncoding) : null;
    }
    collisionStatus(optionalEncoding) {
        const offset = this.bb.__offset(this.bb_pos, 20);
        return offset ? this.bb.__string(this.bb_pos + offset, optionalEncoding) : null;
    }
    reason(optionalEncoding) {
        const offset = this.bb.__offset(this.bb_pos, 22);
        return offset ? this.bb.__string(this.bb_pos + offset, optionalEncoding) : null;
    }
    positions(index) {
        const offset = this.bb.__offset(this.bb_pos, 24);
        return offset ? this.bb.readFloat64(this.bb.__vector(this.bb_pos + offset) + index * 8) : 0;
    }
    positionsLength() {
        const offset = this.bb.__offset(this.bb_pos, 24);
        return offset ? this.bb.__vector_len(this.bb_pos + offset) : 0;
    }
    positionsArray() {
        const offset = this.bb.__offset(this.bb_pos, 24);
        return offset ? new Float64Array(this.bb.bytes().buffer, this.bb.bytes().byteOffset + this.bb.__vector(this.bb_pos + offset), this.bb.__vector_len(this.bb_pos + offset)) : null;
    }
    tipWorld(index) {
        const offset = this.bb.__offset(this.bb_pos, 26);
        return offset ? this.bb.readFloat64(this.bb.__vector(this.bb_pos + offset) + index * 8) : 0;
    }
    tipWorldLength() {
        const offset = this.bb.__offset(this.bb_pos, 26);
        return offset ? this.bb.__vector_len(this.bb_pos + offset) : 0;
    }
    tipWorldArray() {
        const offset = this.bb.__offset(this.bb_pos, 26);
        return offset ? new Float64Array(this.bb.bytes().buffer, this.bb.bytes().byteOffset + this.bb.__vector(this.bb_pos + offset), this.bb.__vector_len(this.bb_pos + offset)) : null;
    }
    cacheReused() {
        const offset = this.bb.__offset(this.bb_pos, 28);
        return offset ? !!this.bb.readInt8(this.bb_pos + offset) : false;
    }
    captureMs() {
        const offset = this.bb.__offset(this.bb_pos, 30);
        return offset ? this.bb.readFloat64(this.bb_pos + offset) : 0.0;
    }
    prepareMs() {
        const offset = this.bb.__offset(this.bb_pos, 32);
        return offset ? this.bb.readFloat64(this.bb_pos + offset) : 0.0;
    }
    solveMs() {
        const offset = this.bb.__offset(this.bb_pos, 34);
        return offset ? this.bb.readFloat64(this.bb_pos + offset) : 0.0;
    }
    collisionMs() {
        const offset = this.bb.__offset(this.bb_pos, 36);
        return offset ? this.bb.readFloat64(this.bb_pos + offset) : 0.0;
    }
    commitMs() {
        const offset = this.bb.__offset(this.bb_pos, 38);
        return offset ? this.bb.readFloat64(this.bb_pos + offset) : 0.0;
    }
    totalMs() {
        const offset = this.bb.__offset(this.bb_pos, 40);
        return offset ? this.bb.readFloat64(this.bb_pos + offset) : 0.0;
    }
    positionErrorMm() {
        const offset = this.bb.__offset(this.bb_pos, 42);
        return offset ? this.bb.readFloat64(this.bb_pos + offset) : 0.0;
    }
    rotationErrorRadians() {
        const offset = this.bb.__offset(this.bb_pos, 44);
        return offset ? this.bb.readFloat64(this.bb_pos + offset) : 0.0;
    }
    worldMatrices(index) {
        const offset = this.bb.__offset(this.bb_pos, 46);
        return offset ? this.bb.readFloat64(this.bb.__vector(this.bb_pos + offset) + index * 8) : 0;
    }
    worldMatricesLength() {
        const offset = this.bb.__offset(this.bb_pos, 46);
        return offset ? this.bb.__vector_len(this.bb_pos + offset) : 0;
    }
    worldMatricesArray() {
        const offset = this.bb.__offset(this.bb_pos, 46);
        return offset ? new Float64Array(this.bb.bytes().buffer, this.bb.bytes().byteOffset + this.bb.__vector(this.bb_pos + offset), this.bb.__vector_len(this.bb_pos + offset)) : null;
    }
    collisionPairs(index, obj) {
        const offset = this.bb.__offset(this.bb_pos, 48);
        return offset ? (obj || new MachineCollisionPair()).__init(this.bb.__indirect(this.bb.__vector(this.bb_pos + offset) + index * 4), this.bb) : null;
    }
    collisionPairsLength() {
        const offset = this.bb.__offset(this.bb_pos, 48);
        return offset ? this.bb.__vector_len(this.bb_pos + offset) : 0;
    }
    collisionDiagnosticsComplete() {
        const offset = this.bb.__offset(this.bb_pos, 50);
        return offset ? !!this.bb.readInt8(this.bb_pos + offset) : false;
    }
    static startMachineIKResult(builder) {
        builder.startObject(24);
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
    static addResultSequence(builder, resultSequence) {
        builder.addFieldInt64(3, resultSequence, BigInt('0'));
    }
    static addProcessedSequence(builder, processedSequence) {
        builder.addFieldInt64(4, processedSequence, BigInt('0'));
    }
    static addAppliedSequence(builder, appliedSequence) {
        builder.addFieldInt64(5, appliedSequence, BigInt('0'));
    }
    static addMutationSequence(builder, mutationSequence) {
        builder.addFieldInt64(6, mutationSequence, BigInt('0'));
    }
    static addStatus(builder, statusOffset) {
        builder.addFieldOffset(7, statusOffset, 0);
    }
    static addCollisionStatus(builder, collisionStatusOffset) {
        builder.addFieldOffset(8, collisionStatusOffset, 0);
    }
    static addReason(builder, reasonOffset) {
        builder.addFieldOffset(9, reasonOffset, 0);
    }
    static addPositions(builder, positionsOffset) {
        builder.addFieldOffset(10, positionsOffset, 0);
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
    static addTipWorld(builder, tipWorldOffset) {
        builder.addFieldOffset(11, tipWorldOffset, 0);
    }
    static createTipWorldVector(builder, data) {
        builder.startVector(8, data.length, 8);
        for(let i = data.length - 1; i >= 0; i--){
            builder.addFloat64(data[i]);
        }
        return builder.endVector();
    }
    static startTipWorldVector(builder, numElems) {
        builder.startVector(8, numElems, 8);
    }
    static addCacheReused(builder, cacheReused) {
        builder.addFieldInt8(12, +cacheReused, +false);
    }
    static addCaptureMs(builder, captureMs) {
        builder.addFieldFloat64(13, captureMs, 0.0);
    }
    static addPrepareMs(builder, prepareMs) {
        builder.addFieldFloat64(14, prepareMs, 0.0);
    }
    static addSolveMs(builder, solveMs) {
        builder.addFieldFloat64(15, solveMs, 0.0);
    }
    static addCollisionMs(builder, collisionMs) {
        builder.addFieldFloat64(16, collisionMs, 0.0);
    }
    static addCommitMs(builder, commitMs) {
        builder.addFieldFloat64(17, commitMs, 0.0);
    }
    static addTotalMs(builder, totalMs) {
        builder.addFieldFloat64(18, totalMs, 0.0);
    }
    static addPositionErrorMm(builder, positionErrorMm) {
        builder.addFieldFloat64(19, positionErrorMm, 0.0);
    }
    static addRotationErrorRadians(builder, rotationErrorRadians) {
        builder.addFieldFloat64(20, rotationErrorRadians, 0.0);
    }
    static addWorldMatrices(builder, worldMatricesOffset) {
        builder.addFieldOffset(21, worldMatricesOffset, 0);
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
    static addCollisionPairs(builder, collisionPairsOffset) {
        builder.addFieldOffset(22, collisionPairsOffset, 0);
    }
    static createCollisionPairsVector(builder, data) {
        builder.startVector(4, data.length, 4);
        for(let i = data.length - 1; i >= 0; i--){
            builder.addOffset(data[i]);
        }
        return builder.endVector();
    }
    static startCollisionPairsVector(builder, numElems) {
        builder.startVector(4, numElems, 4);
    }
    static addCollisionDiagnosticsComplete(builder, collisionDiagnosticsComplete) {
        builder.addFieldInt8(23, +collisionDiagnosticsComplete, +false);
    }
    static endMachineIKResult(builder) {
        const offset = builder.endObject();
        builder.requiredField(offset, 6);
        builder.requiredField(offset, 8);
        builder.requiredField(offset, 18);
        return offset;
    }
    static finishMachineIKResultBuffer(builder, offset) {
        builder.finish(offset, 'F5IR');
    }
    static finishSizePrefixedMachineIKResultBuffer(builder, offset) {
        builder.finish(offset, 'F5IR', true);
    }
    static createMachineIKResult(builder, schemaVersion, editTokenOffset, epochOffset, resultSequence, processedSequence, appliedSequence, mutationSequence, statusOffset, collisionStatusOffset, reasonOffset, positionsOffset, tipWorldOffset, cacheReused, captureMs, prepareMs, solveMs, collisionMs, commitMs, totalMs, positionErrorMm, rotationErrorRadians, worldMatricesOffset, collisionPairsOffset, collisionDiagnosticsComplete) {
        MachineIKResult.startMachineIKResult(builder);
        MachineIKResult.addSchemaVersion(builder, schemaVersion);
        MachineIKResult.addEditToken(builder, editTokenOffset);
        MachineIKResult.addEpoch(builder, epochOffset);
        MachineIKResult.addResultSequence(builder, resultSequence);
        MachineIKResult.addProcessedSequence(builder, processedSequence);
        MachineIKResult.addAppliedSequence(builder, appliedSequence);
        MachineIKResult.addMutationSequence(builder, mutationSequence);
        MachineIKResult.addStatus(builder, statusOffset);
        MachineIKResult.addCollisionStatus(builder, collisionStatusOffset);
        MachineIKResult.addReason(builder, reasonOffset);
        MachineIKResult.addPositions(builder, positionsOffset);
        MachineIKResult.addTipWorld(builder, tipWorldOffset);
        MachineIKResult.addCacheReused(builder, cacheReused);
        MachineIKResult.addCaptureMs(builder, captureMs);
        MachineIKResult.addPrepareMs(builder, prepareMs);
        MachineIKResult.addSolveMs(builder, solveMs);
        MachineIKResult.addCollisionMs(builder, collisionMs);
        MachineIKResult.addCommitMs(builder, commitMs);
        MachineIKResult.addTotalMs(builder, totalMs);
        MachineIKResult.addPositionErrorMm(builder, positionErrorMm);
        MachineIKResult.addRotationErrorRadians(builder, rotationErrorRadians);
        MachineIKResult.addWorldMatrices(builder, worldMatricesOffset);
        MachineIKResult.addCollisionPairs(builder, collisionPairsOffset);
        MachineIKResult.addCollisionDiagnosticsComplete(builder, collisionDiagnosticsComplete);
        return MachineIKResult.endMachineIKResult(builder);
    }
}
