const STYLE_ID = "laser-3d-cam-style";

export function ensureStyles() {
  if (document.getElementById(STYLE_ID)) {
    return;
  }

  const link = document.createElement("link");
  link.id = STYLE_ID;
  link.rel = "stylesheet";
  link.href = new URL("./laser3dcam.css", import.meta.url).href;
  document.head.appendChild(link);
}
