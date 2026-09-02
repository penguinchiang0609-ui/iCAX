const STYLE_ID = "tube-one-style";

export function ensureTubeOneStyles() {
  if (document.getElementById(STYLE_ID)) {
    return;
  }
  const link = document.createElement("link");
  link.id = STYLE_ID;
  link.rel = "stylesheet";
  link.href = new URL("./tubeOne.css", import.meta.url).href;
  document.head.appendChild(link);
}
