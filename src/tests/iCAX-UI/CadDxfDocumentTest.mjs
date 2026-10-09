import assert from "node:assert/strict";
import fs from "node:fs";
import { parseDxfDocument } from "../../iCAX-UI/SDK/Cad/dxfDocument.mjs";

const fixture = new URL("./fixtures/cad-import/", import.meta.url);
const ascii = fs.readFileSync(new URL("block-contours.dxf", fixture));
const binary = fs.readFileSync(new URL("block-contours-binary.dxf", fixture));
const asciiDocument = parseDxfDocument(ascii), binaryDocument = parseDxfDocument(binary);
// Values may be strings or numbers in the neutral schema. Compare the source
// tags themselves, without involving the sketch mapper or exporter.
const canonical = doc => JSON.parse(JSON.stringify({ header: doc.header, sections: doc.sections }, (_, value) => typeof value === "number" ? String(value) : value));
assert.deepEqual(canonical(binaryDocument), canonical(asciiDocument));
assert.equal(asciiDocument.sourceFormat, "ascii");
assert.equal(binaryDocument.sourceFormat, "binary");
assert.deepEqual(asciiDocument.sections.ENTITIES.map(record => record.type), ["INSERT", "TEXT"]);
assert.deepEqual(asciiDocument.sections.BLOCKS.map(record => record.type), ["BLOCK", "LWPOLYLINE", "CIRCLE", "ELLIPSE", "SPLINE", "ENDBLK"]);
assert.deepEqual(asciiDocument.header.$INSUNITS, [[70, "4"]]);
assert.deepEqual(canonical(parseDxfDocument(ascii.toString("utf8"))), canonical(asciiDocument));
const padded = Buffer.concat([Buffer.alloc(11), binary, Buffer.alloc(13)]);
assert.deepEqual(canonical(parseDxfDocument(new Uint8Array(padded.buffer, padded.byteOffset + 11, binary.length))), canonical(binaryDocument));
const buffer = binary.buffer.slice(binary.byteOffset, binary.byteOffset + binary.byteLength);
assert.deepEqual(canonical(parseDxfDocument(buffer)), canonical(binaryDocument));

const pairsText = pairs => pairs.map(([code, value]) => `${code}\n${value}\n`).join("");
const docPairs = records => [[0, "SECTION"], [2, "HEADER"], [9, "$ACADVER"], [1, "AC1027"], [9, "$INSUNITS"], [70, 4], [0, "ENDSEC"], [0, "SECTION"], [2, "ENTITIES"], ...records, [0, "ENDSEC"], [0, "EOF"]];
const records = [[0, "SPLINE"], [71, 2], [10, 1], [20, 2], [10, 3], [20, 4], [10, 5], [20, 6], [40, 0], [40, 0], [40, 0], [40, 1], [40, 1], [40, 1], [8, "\\U+4E2D\\U+6587"], [1001, "APP"], [1070, -4], [1071, 1234567]];
const ordered = parseDxfDocument(pairsText(docPairs(records)));
assert.deepEqual(ordered.sections.ENTITIES[0].values.filter(([code]) => code === 10), [[10, "1"], [10, "3"], [10, "5"]]);
assert.equal(ordered.sections.ENTITIES[0].values.find(([code]) => code === 8)[1], "中文");

// The test writer uses explicit DXF wire types, independently of the reader's
// group-code classifier. It checks every numeric width plus XDATA/chunks.
function writeBinary(pairs) {
  const chunks = [Buffer.from("AutoCAD Binary DXF\r\n\x1a\0", "ascii")];
  for (const [code, value, wire = "string"] of pairs) {
    const group = Buffer.alloc(2); group.writeUInt16LE(code); chunks.push(group);
    let bytes;
    if (wire === "string") bytes = Buffer.concat([Buffer.isBuffer(value) ? value : Buffer.from(String(value), "utf8"), Buffer.from([0])]);
    else if (wire === "short") { bytes = Buffer.alloc(2); bytes.writeInt16LE(value); }
    else if (wire === "int") { bytes = Buffer.alloc(4); bytes.writeInt32LE(value); }
    else if (wire === "long") { bytes = Buffer.alloc(8); bytes.writeBigInt64LE(BigInt(value)); }
    else if (wire === "double") { bytes = Buffer.alloc(8); bytes.writeDoubleLE(value); }
    else if (wire === "bool") bytes = Buffer.from([value]);
    else if (wire === "chunk") bytes = Buffer.from([value.length, ...value]);
    else throw new Error(wire);
    chunks.push(bytes);
  }
  return Buffer.concat(chunks);
}
const wireRecords = [[0, "LINE"], [10, 1.2345678901234567, "double"], [20, -2.5, "double"], [11, 7, "double"], [21, 8, "double"], [62, -3, "short"], [90, 1234567, "int"], [160, "9223372036854775807", "long"], [290, 1, "bool"], [310, [0, 1, 254, 255], "chunk"], [1001, "APP"], [1004, [128, 16], "chunk"], [1040, Math.PI, "double"], [1070, -9, "short"], [1071, -1234567, "int"]];
const wireDoc = parseDxfDocument(writeBinary([[0, "SECTION"], [2, "ENTITIES"], ...wireRecords, [0, "ENDSEC"], [0, "EOF"]]));
const tags = new Map(wireDoc.sections.ENTITIES[0].values);
assert.equal(tags.get(10), 1.2345678901234567);
assert.equal(tags.get(160), "9223372036854775807");
assert.equal(tags.get(310), "0001feff");
assert.equal(tags.get(1004), "8010");
assert.equal(tags.get(1040), Math.PI);
assert.equal(tags.get(1070), -9);
assert.equal(tags.get(1071), -1234567);

// Both encodings decode pre-2007 code-page text into the same neutral Unicode.
const gbkName = Buffer.from([0xd6, 0xd0, 0xce, 0xc4]);
const oldHeader = [[0, "SECTION"], [2, "HEADER"], [9, "$ACADVER"], [1, "AC1015"], [9, "$DWGCODEPAGE"], [3, "ANSI_936"], [0, "ENDSEC"]];
const gbkBinary = parseDxfDocument(writeBinary([...oldHeader, [0, "SECTION"], [2, "ENTITIES"], [0, "TEXT"], [1, gbkName], [0, "ENDSEC"], [0, "EOF"]]));
const gbkAscii = Buffer.concat([Buffer.from(pairsText([...oldHeader, [0, "SECTION"], [2, "ENTITIES"], [0, "TEXT"]]) + "1\n", "ascii"), gbkName, Buffer.from("\n0\nENDSEC\n0\nEOF\n", "ascii")]);
assert.equal(gbkBinary.sections.ENTITIES[0].values[0][1], "中文");
assert.deepEqual(canonical(parseDxfDocument(gbkAscii)), canonical(gbkBinary));
const utf8 = parseDxfDocument(new TextEncoder().encode(pairsText(docPairs([[0, "TEXT"], [1, "中文圆弧"]]))));
assert.equal(utf8.sections.ENTITIES[0].values[0][1], "中文圆弧");

// Neutral strings preserve significant spaces identically in both formats;
// whitespace padding on structural keywords does not change their meaning.
const spacedRecords = [[0, " TEXT "], [1, "  中文 text  "], [1001, "APP"], [1000, "  data  "]];
const spacedAscii = parseDxfDocument(pairsText(docPairs(spacedRecords)));
const spacedBinary = parseDxfDocument(writeBinary(docPairs(spacedRecords).map(pair => pair[0] === 70 ? [...pair, "short"] : pair)));
assert.deepEqual(canonical(spacedAscii), canonical(spacedBinary));
assert.equal(spacedAscii.sections.ENTITIES[0].type, "TEXT");
assert.equal(spacedAscii.sections.ENTITIES[0].values[0][1], "  中文 text  ");
assert.equal(spacedAscii.sections.ENTITIES[0].values.at(-1)[1], "  data  ");
const commentedPairs = [[999, "before the first section"], [0, "SECTION"], [2, "HEADER"], [999, "before a variable"], [9, "$INSUNITS"], [70, 4], [999, "after a variable"], [0, "ENDSEC"], [0, "SECTION"], [2, "ENTITIES"], [999, "before a record"], [0, "LINE"], [10, 0], [999, "between record groups"], [20, 1], [11, 2], [21, 3], [0, "ENDSEC"], [999, "between sections"], [0, "EOF"], [999, "after EOF"]];
const commented = parseDxfDocument(pairsText(commentedPairs));
assert.deepEqual(commented.header.$INSUNITS, [[70, "4"]]);
assert.deepEqual(commented.sections.ENTITIES, [{ type: "LINE", values: [[10, "0"], [20, "1"], [11, "2"], [21, "3"]] }]);
const commentedBinary = parseDxfDocument(writeBinary(commentedPairs.filter(([code]) => code !== 999).flatMap(pair => pair[0] === 0 && pair[1] === "LINE" ? [[999, "before a record"], pair] : [pair]).map(pair => pair[0] === 70 ? [...pair, "short"] : [10, 20, 11, 21].includes(pair[0]) ? [...pair, "double"] : pair)));
assert.deepEqual(canonical(commentedBinary), canonical(commented));

const thumbnail = parseDxfDocument(pairsText([[0, "SECTION"], [2, "THUMBNAILIMAGE"], [90, 4], [310, "0011aaff"], [0, "ENDSEC"], [0, "SECTION"], [2, "ENTITIES"], [0, "ENDSEC"], [0, "EOF"]]));
assert.deepEqual(thumbnail.sections.THUMBNAILIMAGE, [{ type: "THUMBNAILIMAGE", values: [[90, "4"], [310, "0011aaff"]] }]);
const bom = Buffer.concat([Buffer.from([0xef, 0xbb, 0xbf]), ascii]);
assert.deepEqual(canonical(parseDxfDocument(bom)), canonical(asciiDocument));
for (const content of ["", "0\nSECTION\n2", "x\nSECTION\n", "0\nSECTION\n2\nENTITIES\n0\nEOF\n", "0\nENDSEC\n0\nEOF\n", "0\nSECTION\n2\nENTITIES\n0\nENDSEC\n", "0\nSECTION\n2\nENTITIES\n0\nENDSEC\n0\nEOF\n0\nLINE\n"]) assert.throws(() => parseDxfDocument(content));
for (const length of [21, 22, 23, binary.length - 1, binary.length - 4]) assert.throws(() => parseDxfDocument(binary.subarray(0, length)));
assert.throws(() => parseDxfDocument(Buffer.concat([binary, Buffer.from([0])])), /EOF/);
assert.throws(() => parseDxfDocument("AutoCAD Binary DXF"), /原始字节/);
assert.throws(() => parseDxfDocument(" ".repeat(32 * 1024 * 1024 + 1)), /32 MB/);
assert.throws(() => parseDxfDocument(new Uint8Array(32 * 1024 * 1024 + 1)), /32 MB/);
assert.throws(() => parseDxfDocument(writeBinary([[0, "SECTION"], [2, "ENTITIES"], [0, "LINE"], [10, NaN, "double"], [0, "ENDSEC"], [0, "EOF"]])), /有限/);
assert.throws(() => parseDxfDocument(writeBinary([[0, "SECTION"], [2, "ENTITIES"], [0, "LINE"], [290, 2, "bool"], [0, "ENDSEC"], [0, "EOF"]])), /布尔/);
console.log("Neutral DXF reader: ASCII/binary fixture equivalence, ordered tags, numeric precision, UTF-8/GBK, spaces, comments, thumbnail and invalid-input checks passed.");
