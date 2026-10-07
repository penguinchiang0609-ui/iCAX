import { tubeDesignerCss } from "./tubeDesigner.css.mjs";
import { nestingSettingsCss } from "./nestingSettings.css.mjs";
import { tileWindowCss } from "./tileWindow.css.mjs";
import { batchExcelAutomationCss } from "./batchExcelAutomation.css.mjs";
import { floatingEditorsCss } from "./floatingEditors.css.mjs";
import { installTubeDesignerDialogDragging } from "../dialogWindow.mjs";

export function ensureTubeDesignerStyles() {
  const existing = document.querySelector("style[data-tube-designer-styles]");
  if (existing) {
    if (!existing.dataset.tubeDesignerExcelAutomation) {
      existing.textContent += `\n${batchExcelAutomationCss}`;
      existing.dataset.tubeDesignerExcelAutomation = "1";
    }
    if (!existing.dataset.tubeDesignerTileWindows) {
      existing.textContent += `\n${tileWindowCss}`;
      existing.dataset.tubeDesignerTileWindows = "1";
    }
    if (!existing.dataset.tubeDesignerFloatingEditors) {
      existing.textContent += `\n${floatingEditorsCss}`;
      existing.dataset.tubeDesignerFloatingEditors = "1";
    }
    installTubeDesignerDialogDragging(document);
    return;
  }
  const style = document.createElement("style");
  style.dataset.tubeDesignerStyles = "1";
  style.dataset.tubeDesignerTileWindows = "1";
  style.dataset.tubeDesignerExcelAutomation = "1";
  style.dataset.tubeDesignerFloatingEditors = "1";
  style.textContent = `${tubeDesignerCss}\n${nestingSettingsCss}\n${tileWindowCss}\n${batchExcelAutomationCss}`;
  document.head.appendChild(style);
  installTubeDesignerDialogDragging(document);
}
