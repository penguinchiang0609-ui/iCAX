import { tubeDesignerCss } from "./tubeDesigner.css.mjs";

export function ensureTubeDesignerStyles() {
  if (document.querySelector("style[data-tube-designer-styles]")) return;
  const style = document.createElement("style");
  style.dataset.tubeDesignerStyles = "1";
  style.textContent = tubeDesignerCss;
  document.head.appendChild(style);
}
