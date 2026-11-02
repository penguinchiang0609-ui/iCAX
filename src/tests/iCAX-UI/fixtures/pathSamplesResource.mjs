import {Builder} from '../../../apps/five-axis-cam/protocol/runtime/flatbuffers.mjs';

// Build the production CamPathSamples.fbs format with the official runtime.
// Numeric samples stay float64 and use the same ICPS identifier as native data.
export function createPathSamplesResource(entries){
 const builder=new Builder(1024);
 const doubles=triples=>{const values=triples.flat();builder.startVector(8,values.length,8);for(let i=values.length-1;i>=0;i--)builder.addFloat64(values[i]);return builder.endVector();};
 const offsets=values=>{builder.startVector(4,values.length,4);for(let i=values.length-1;i>=0;i--)builder.addOffset(values[i]);return builder.endVector();};
 const entryOffsets=entries.map(entry=>{
  const id=builder.createString(entry.id),partOffsets=entry.parts.map(part=>{
   const role=builder.createString(part.role),points=doubles(part.points),directions=doubles(part.directions);
   builder.startObject(3);builder.addFieldOffset(0,role,0);builder.addFieldOffset(1,points,0);builder.addFieldOffset(2,directions,0);return builder.endObject();
  });
  const parts=offsets(partOffsets);builder.startObject(2);builder.addFieldOffset(0,id,0);builder.addFieldOffset(1,parts,0);return builder.endObject();
 });
 const rootEntries=offsets(entryOffsets);builder.startObject(2);builder.addFieldInt32(0,1,1);builder.addFieldOffset(1,rootEntries,0);const root=builder.endObject();
 builder.finish(root,'ICPS');return builder.asUint8Array().slice();
}
