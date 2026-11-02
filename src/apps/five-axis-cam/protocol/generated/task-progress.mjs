import * as flatbuffers from '../runtime/flatbuffers.mjs';
export class TaskProgress {
    bb = null;
    bb_pos = 0;
    __init(i, bb) {
        this.bb_pos = i;
        this.bb = bb;
        return this;
    }
    static getRootAsTaskProgress(bb, obj) {
        return (obj || new TaskProgress()).__init(bb.readInt32(bb.position()) + bb.position(), bb);
    }
    static getSizePrefixedRootAsTaskProgress(bb, obj) {
        bb.setPosition(bb.position() + flatbuffers.SIZE_PREFIX_LENGTH);
        return (obj || new TaskProgress()).__init(bb.readInt32(bb.position()) + bb.position(), bb);
    }
    static bufferHasIdentifier(bb) {
        return bb.__has_identifier('F5TP');
    }
    schemaVersion() {
        const offset = this.bb.__offset(this.bb_pos, 4);
        return offset ? this.bb.readUint32(this.bb_pos + offset) : 1;
    }
    taskId(optionalEncoding) {
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
    status(optionalEncoding) {
        const offset = this.bb.__offset(this.bb_pos, 12);
        return offset ? this.bb.__string(this.bb_pos + offset, optionalEncoding) : null;
    }
    kind(optionalEncoding) {
        const offset = this.bb.__offset(this.bb_pos, 14);
        return offset ? this.bb.__string(this.bb_pos + offset, optionalEncoding) : null;
    }
    progress() {
        const offset = this.bb.__offset(this.bb_pos, 16);
        return offset ? this.bb.readUint32(this.bb_pos + offset) : 0;
    }
    error(optionalEncoding) {
        const offset = this.bb.__offset(this.bb_pos, 18);
        return offset ? this.bb.__string(this.bb_pos + offset, optionalEncoding) : null;
    }
    terminationReason(optionalEncoding) {
        const offset = this.bb.__offset(this.bb_pos, 20);
        return offset ? this.bb.__string(this.bb_pos + offset, optionalEncoding) : null;
    }
    detailsJson(optionalEncoding) {
        const offset = this.bb.__offset(this.bb_pos, 22);
        return offset ? this.bb.__string(this.bb_pos + offset, optionalEncoding) : null;
    }
    static startTaskProgress(builder) {
        builder.startObject(10);
    }
    static addSchemaVersion(builder, schemaVersion) {
        builder.addFieldInt32(0, schemaVersion, 1);
    }
    static addTaskId(builder, taskIdOffset) {
        builder.addFieldOffset(1, taskIdOffset, 0);
    }
    static addEpoch(builder, epochOffset) {
        builder.addFieldOffset(2, epochOffset, 0);
    }
    static addSequence(builder, sequence) {
        builder.addFieldInt64(3, sequence, BigInt('0'));
    }
    static addStatus(builder, statusOffset) {
        builder.addFieldOffset(4, statusOffset, 0);
    }
    static addKind(builder, kindOffset) {
        builder.addFieldOffset(5, kindOffset, 0);
    }
    static addProgress(builder, progress) {
        builder.addFieldInt32(6, progress, 0);
    }
    static addError(builder, errorOffset) {
        builder.addFieldOffset(7, errorOffset, 0);
    }
    static addTerminationReason(builder, terminationReasonOffset) {
        builder.addFieldOffset(8, terminationReasonOffset, 0);
    }
    static addDetailsJson(builder, detailsJsonOffset) {
        builder.addFieldOffset(9, detailsJsonOffset, 0);
    }
    static endTaskProgress(builder) {
        const offset = builder.endObject();
        builder.requiredField(offset, 6);
        builder.requiredField(offset, 8);
        builder.requiredField(offset, 12);
        return offset;
    }
    static finishTaskProgressBuffer(builder, offset) {
        builder.finish(offset, 'F5TP');
    }
    static finishSizePrefixedTaskProgressBuffer(builder, offset) {
        builder.finish(offset, 'F5TP', true);
    }
    static createTaskProgress(builder, schemaVersion, taskIdOffset, epochOffset, sequence, statusOffset, kindOffset, progress, errorOffset, terminationReasonOffset, detailsJsonOffset) {
        TaskProgress.startTaskProgress(builder);
        TaskProgress.addSchemaVersion(builder, schemaVersion);
        TaskProgress.addTaskId(builder, taskIdOffset);
        TaskProgress.addEpoch(builder, epochOffset);
        TaskProgress.addSequence(builder, sequence);
        TaskProgress.addStatus(builder, statusOffset);
        TaskProgress.addKind(builder, kindOffset);
        TaskProgress.addProgress(builder, progress);
        TaskProgress.addError(builder, errorOffset);
        TaskProgress.addTerminationReason(builder, terminationReasonOffset);
        TaskProgress.addDetailsJson(builder, detailsJsonOffset);
        return TaskProgress.endTaskProgress(builder);
    }
}
