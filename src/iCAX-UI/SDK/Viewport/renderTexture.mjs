import * as THREE from "../ThirdParty/three/three.module.js";
import { parseRenderTextureResource } from "./renderResource.mjs";

// A material references image bytes through the same ResourceClient as its
// geometry. The loader never reads product state or a local filesystem path.
export async function loadRenderTexture(resourceClient, url) {
  const response = await resourceClient.get(url, { headers: { Accept: "application/vnd.icax.flatbuffer" } });
  if (!response.ok) throw new Error(`Render texture GET failed (${response.status}): ${url}`);
  const resource = parseRenderTextureResource(await response.arrayBuffer());
  const blob = new Blob([resource.encodedImage], { type: resource.mimeType });
  let source, bitmap = false;
  if (typeof createImageBitmap === "function") {
    source = await createImageBitmap(blob, { imageOrientation: "flipY", premultiplyAlpha: "none", colorSpaceConversion: "none" });bitmap = true;
  } else {
    const objectUrl = URL.createObjectURL(blob);
    try { source = new Image();source.src = objectUrl;await source.decode(); }
    finally { URL.revokeObjectURL(objectUrl); }
  }
  if ((source.width ?? source.naturalWidth) !== resource.pixelWidth || (source.height ?? source.naturalHeight) !== resource.pixelHeight) {
    if (bitmap) source.close();throw new Error("Decoded render texture dimensions do not match its resource");
  }
  const texture = new THREE.Texture(source);texture.colorSpace = THREE.SRGBColorSpace;
  texture.flipY = !bitmap;texture.needsUpdate = true;
  const release = texture.dispose.bind(texture);let disposed = false;
  texture.dispose = () => { if (disposed) return;disposed = true;release();if (bitmap) source.close(); };
  return texture;
}
