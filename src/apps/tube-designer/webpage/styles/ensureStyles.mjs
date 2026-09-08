import { tubeDesignerCss } from "./tubeDesigner.css.mjs";
import { nestingSettingsCss } from "./nestingSettings.css.mjs";
import { tileWindowCss } from "./tileWindow.css.mjs";
import { installTubeDesignerDialogDragging } from "../dialogWindow.mjs";

export function ensureTubeDesignerStyles() {
  const existing = document.querySelector("style[data-tube-designer-styles]");
  if (existing) {
    if (!existing.dataset.tubeDesignerTileWindows) {
      existing.textContent += `\n${tileWindowCss}`;
      existing.dataset.tubeDesignerTileWindows = "1";
    }
    installTubeDesignerDialogDragging(document);
    return;
  }
  const style = document.createElement("style");
  style.dataset.tubeDesignerStyles = "1";
  style.dataset.tubeDesignerTileWindows = "1";
  style.textContent = `${tubeDesignerCss}\n${nestingSettingsCss}\n${tileWindowCss}`;
  document.head.appendChild(style);
  installTubeDesignerDialogDragging(document);
}
