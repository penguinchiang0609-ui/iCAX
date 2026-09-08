export function confirmWithoutTitle(message, confirmLabel = "确定") {
  if (typeof document === "undefined") return Promise.resolve(false);
  return new Promise(resolve => {
    const dialog = document.createElement("dialog");
    dialog.className = "tube-designer-confirm-dialog";
    dialog.dataset.tubeDesignerWindowDrag = "1";
    dialog.setAttribute("aria-label", message);
    const content = document.createElement("p");
    content.textContent = message;
    const actions = document.createElement("div");
    const cancel = document.createElement("button");
    cancel.type = "button";
    cancel.textContent = "取消";
    cancel.autofocus = true;
    const confirm = document.createElement("button");
    confirm.type = "button";
    confirm.textContent = confirmLabel;
    confirm.className = "confirm";
    const finish = result => { dialog.remove(); resolve(result); };
    cancel.addEventListener("click", () => finish(false));
    confirm.addEventListener("click", () => finish(true));
    dialog.addEventListener("cancel", event => { event.preventDefault(); finish(false); });
    actions.append(cancel, confirm);
    dialog.append(content, actions);
    document.body.append(dialog);
    dialog.showModal();
  });
}
