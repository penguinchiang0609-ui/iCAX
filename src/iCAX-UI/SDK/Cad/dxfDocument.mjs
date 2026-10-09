// Neutral DXF records: this reader has no sketch, scene or manufacturing data.
// Binary value types follow Autodesk's DXF Group Code Value Types Reference.
// https://help.autodesk.com/cloudhelp/2026/ENU/AutoCAD-DXF/files/GUID-2553CF98-44F6-4828-82DD-FE3BC7448113.htm
const MAX_BYTES = 32 * 1024 * 1024;
const sentinel = "AutoCAD Binary DXF\r\n\x1a\0";
const unicode = text => String(text).replace(/\\U\+([0-9a-f]{4})/gi, (_, code) => String.fromCharCode(parseInt(code, 16)));

function encodingFor(header) {
  if (Number(String(header.$ACADVER?.[0]?.[1] ?? "").trim().replace(/^AC/i, "")) >= 1021) return "utf-8";
  const name = String(header.$DWGCODEPAGE?.[0]?.[1] ?? "ANSI_1252").trim().toUpperCase();
  const encodings = { ANSI_936: "gbk", ANSI_950: "big5", ANSI_932: "shift_jis", ANSI_949: "euc-kr", ANSI_874: "windows-874", UTF8: "utf-8", "UTF-8": "utf-8" };
  return encodings[name] ?? (/^ANSI_125[0-8]$/.test(name) ? name.replace("ANSI_", "windows-") : "windows-1252");
}

function asciiPairs(text) {
  if (!text.trim()) throw new Error("DXF 文件为空。");
  if (text.startsWith("AutoCAD Binary DXF")) throw new Error("二进制 DXF 需要以原始字节读取。");
  const lines = text.replace(/^\uFEFF/, "").replace(/\r\n?/g, "\n").split("\n");
  while (lines.length && lines.at(-1) === "") lines.pop();
  if (lines.length % 2) throw new Error("DXF 组码和值没有成对出现。");
  const pairs = [];
  for (let i = 0; i < lines.length; i += 2) {
    if (!/^\s*\d+\s*$/.test(lines[i])) throw new Error(`DXF 第 ${i + 1} 行不是有效组码。`);
    const code = Number(lines[i]);
    if (code > 1071) throw new Error(`DXF 组码 ${code} 无效。`);
    // String groups (including TEXT and XDATA) retain their meaningful spaces.
    // Only structural keywords are trimmed by splitDocument.
    pairs.push([code, unicode(lines[i + 1])]);
  }
  return pairs;
}

function binaryType(code) {
  if ((code >= 10 && code <= 59) || (code >= 110 && code <= 149) || (code >= 210 && code <= 239) || (code >= 460 && code <= 469) || (code >= 1010 && code <= 1059)) return "double";
  if ((code >= 60 && code <= 79) || (code >= 170 && code <= 179) || (code >= 270 && code <= 289) || (code >= 370 && code <= 389) || (code >= 400 && code <= 409) || code === 1070) return "short";
  if ((code >= 90 && code <= 99) || (code >= 420 && code <= 429) || (code >= 440 && code <= 459) || code === 1071) return "int";
  if (code >= 160 && code <= 169) return "long";
  if (code >= 290 && code <= 299) return "bool";
  if ((code >= 310 && code <= 319) || code === 1004) return "chunk";
  if ((code >= 0 && code <= 9) || (code >= 100 && code <= 102) || code === 105 || (code >= 300 && code <= 309) || (code >= 320 && code <= 369) || (code >= 390 && code <= 399) || (code >= 410 && code <= 419) || (code >= 430 && code <= 439) || (code >= 470 && code <= 481) || code === 999 || (code >= 1000 && code <= 1005)) return "string";
  throw new Error(`二进制 DXF 组码 ${code} 无效。`);
}

function binaryPairs(bytes) {
  const view = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength), decoder = new TextDecoder("windows-1252");
  let offset = 22;
  const requireBytes = count => { if (offset + count > bytes.length) throw new Error(`二进制 DXF 在字节 ${offset} 处被截断。`); };
  // Modern binary DXF uses 16-bit group codes. A one-byte group stream is not
  // guessed: otherwise a damaged code can silently shift every later value.
  requireBytes(2);
  if (view.getUint16(offset, true) !== 0) throw new Error("仅支持使用双字节组码的二进制 DXF。");
  const pairs = [], rawStrings = [];
  while (offset < bytes.length) {
    requireBytes(2); const code = view.getUint16(offset, true); offset += 2;
    let value;
    switch (binaryType(code)) {
      case "double": requireBytes(8); value = view.getFloat64(offset, true); offset += 8; break;
      case "short": requireBytes(2); value = view.getInt16(offset, true); offset += 2; break;
      case "int": requireBytes(4); value = view.getInt32(offset, true); offset += 4; break;
      case "long": requireBytes(8); value = view.getBigInt64(offset, true).toString(); offset += 8; break;
      case "bool": requireBytes(1); value = bytes[offset++]; if (value > 1) throw new Error("二进制 DXF 布尔值无效。"); break;
      case "chunk": {
        requireBytes(1); const length = bytes[offset++]; requireBytes(length);
        value = Array.from(bytes.subarray(offset, offset + length), byte => byte.toString(16).padStart(2, "0")).join(""); offset += length; break;
      }
      case "string": {
        const start = offset;
        while (offset < bytes.length && bytes[offset] !== 0) offset++;
        if (offset === bytes.length) throw new Error(`二进制 DXF 字符串在字节 ${start} 处被截断。`);
        const raw = bytes.subarray(start, offset++); value = decoder.decode(raw); rawStrings.push({ index: pairs.length, raw }); break;
      }
    }
    if (typeof value === "number" && !Number.isFinite(value)) throw new Error(`二进制 DXF 组码 ${code} 不是有限数值。`);
    pairs.push([code, value]);
    if (code === 0 && value === "EOF") { if (offset !== bytes.length) throw new Error("二进制 DXF 的 EOF 后存在额外数据。"); break; }
  }
  const header = splitDocument(pairs, "binary").header, textDecoder = new TextDecoder(encodingFor(header));
  for (const { index, raw } of rawStrings) pairs[index][1] = unicode(textDecoder.decode(raw));
  return pairs;
}

function splitDocument(pairs, sourceFormat) {
  const header = {}, sections = {};
  let section = null, variable = null, record = null, ended = false;
  const finishRecord = () => { if (record) sections[section].push(record); record = null; };
  for (let i = 0; i < pairs.length; i++) {
    const [code, value] = pairs[i], type = String(value).trim().toUpperCase();
    // Standard comments have no document semantics, wherever they appear.
    if (code === 999) continue;
    if (code === 0 && type === "SECTION") {
      if (section || pairs[i + 1]?.[0] !== 2) throw new Error("DXF SECTION 结构无效。");
      section = String(pairs[++i][1]).trim().toUpperCase(); sections[section] ??= []; variable = null;
    } else if (code === 0 && type === "ENDSEC") {
      if (!section) throw new Error("DXF ENDSEC 没有对应的 SECTION。");
      finishRecord(); section = null; variable = null;
    } else if (code === 0 && type === "EOF") {
      if (section) throw new Error("DXF SECTION 缺少 ENDSEC。");
      ended = true;
      if (pairs.slice(i + 1).some(([next]) => next !== 999)) throw new Error("DXF 的 EOF 后存在额外数据。");
      break;
    } else if (section === "HEADER") {
      if (code === 9) { variable = String(value).trim().toUpperCase(); header[variable] = []; }
      else if (variable) header[variable].push([code, value]);
    } else if (section) {
      if (code === 0) { finishRecord(); record = { type, values: [] }; }
      else {
        // THUMBNAILIMAGE stores its groups directly in the section, without
        // entity markers. Preserve such section data in a neutral record.
        record ??= { type: section, values: [] };
        record.values.push([code, value]);
      }
    } else if (code !== 999) throw new Error("DXF 实体出现在 SECTION 之外。");
  }
  if (!ended) throw new Error("DXF 文件缺少 EOF。");
  if (!sections.ENTITIES) throw new Error("DXF 文件缺少 ENTITIES 段。");
  return { header, sections, sourceFormat };
}

/** Read string, ArrayBuffer or an ArrayBuffer view into a neutral document.
 * Header variables hold ordered [groupCode, value] pairs. Sections hold ordered
 * {type, values} records, including BLOCK/ENDBLK and POLYLINE/VERTEX/SEQEND.
 * No groups are interpreted as scene geometry here. The business mapper owns
 * units, visibility, supported geometry and block expansion.
 */
export function parseDxfDocument(content) {
  if (typeof content === "string") {
    if (content.length > MAX_BYTES) throw new Error("DXF 文件超过 32 MB。");
    return splitDocument(asciiPairs(content), "ascii");
  }
  let bytes = content instanceof ArrayBuffer ? new Uint8Array(content) : ArrayBuffer.isView(content) ? new Uint8Array(content.buffer, content.byteOffset, content.byteLength) : null;
  if (!bytes?.length) throw new Error("DXF 文件为空或输入格式无效。");
  if (bytes.byteLength > MAX_BYTES) throw new Error("DXF 文件超过 32 MB。");
  if (bytes[0] === 0xef && bytes[1] === 0xbb && bytes[2] === 0xbf) bytes = bytes.subarray(3);
  const isBinary = bytes.length >= 22 && Array.from(bytes.subarray(0, 22), byte => String.fromCharCode(byte)).join("") === sentinel;
  if (isBinary) return splitDocument(binaryPairs(bytes), "binary");
  const provisional = new TextDecoder("windows-1252").decode(bytes);
  const header = splitDocument(asciiPairs(provisional), "ascii").header;
  return splitDocument(asciiPairs(new TextDecoder(encodingFor(header)).decode(bytes)), "ascii");
}
