import { tubeDesignerCss } from "./tubeDesigner.css.mjs";
import { nestingSettingsCss } from "./nestingSettings.css.mjs";

export function ensureTubeDesignerStyles() {
  if (document.querySelector("style[data-tube-designer-styles]")) return;
  const style = document.createElement("style");
  style.dataset.tubeDesignerStyles = "1";
  style.textContent = `${tubeDesignerCss}\n${nestingSettingsCss}`;
  document.head.appendChild(style);
}
