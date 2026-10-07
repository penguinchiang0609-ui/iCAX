import { createNestingWireframePreview } from "./nestingWireframePreview.mjs";
import { createNestingDxfPreview } from "./nestingDxfPreview.mjs";
import { createWindowStateMemory } from "../../../iCAX-UI/SDK/Forms/windowStateMemory.mjs";
import { dialogAppearanceCss } from "./styles/dialogAppearance.css.mjs";

const CAD_FILE = /\.(?:step|stp|iges|igs)$/i;
const PREVIEW_DEBOUNCE_MS = 120;
const MAX_CACHE_ENTRIES = 8;
const MAX_CACHE_BYTES = 32 * 1024 * 1024;
let pickerSequence = 0;
let activePicker = null;
const scenePreviewSessions = new WeakMap();
const dxfPreviewSessions = new WeakMap();
const filePickerMemory = createWindowStateMemory({ namespace: 'icax.tube-designer.window-state' });
const CAD_PICKER = { key: 'nesting-file-picker', title: '导入下料零件', fileType: 'cad', accepts: CAD_FILE,
  label: 'STEP / STP / IGES / IGS', multiple: true, sessions: scenePreviewSessions,
  idleMessage: '选择文件查看线框预览', loadingMessage: '正在加载线框预览…', emptyMessage: '此目录没有 STEP / IGES 文件',
  caption: '右键拖动旋转 · 滚轮缩放', showFitButton: true };
const DXF_PICKER = { key: 'nesting-profile-dxf-picker', title: '选择 DXF 截面', fileType: 'dxf', accepts: /\.dxf$/i,
  label: 'DXF 截面', multiple: false, sessions: dxfPreviewSessions,
  idleMessage: '选择文件查看截面预览', loadingMessage: '正在加载截面预览…', emptyMessage: '此目录没有 DXF 文件',
  caption: '', showFitButton: false };

function fileIdentity(file) {
  const path = String(file.path ?? file.sourcePath ?? "").replaceAll("\\", "/").toLowerCase();
  const modified = String(file.lastModifiedNs ?? "");
  const size = Number(file.sizeBytes);
  return path && file.sizeBytes != null && String(file.sizeBytes) !== "" && /^-?\d+$/.test(modified) && Number.isSafeInteger(size) && size >= 0
    ? JSON.stringify([path, size, modified]) : "";
}

function button(action, label) {
  const element = document.createElement("button");
  element.type = "button";
  element.dataset.nestingFileAction = action;
  element.textContent = label;
  return element;
}

function pathKey(path) {
  return String(path).replaceAll("\\", "/").replace(/\/+$/, "").toLowerCase();
}

function entryIcon(directory) {
  const icon = document.createElementNS("http://www.w3.org/2000/svg", "svg");
  icon.classList.add("tube-nesting-file-icon");
  icon.setAttribute("viewBox", "0 0 24 24");
  icon.setAttribute("aria-hidden", "true");
  icon.innerHTML = directory
    ? '<path class="folder-closed" d="M3 7V5h6l2 2h10v13H3Z"/><path class="folder-open" d="M3 10V5h6l2 2h9v3M3 20l2-10h17l-3 10Z"/>'
    : '<path d="M6 3h8l4 4v14H6ZM14 3v5h4M9 12h6M9 16h6"/>';
  return icon;
}

function navigationButton(action, label, drawing) {
  const element = button(action, label);
  element.className = "tube-nesting-file-navigation-button";
  const icon = document.createElementNS("http://www.w3.org/2000/svg", "svg");
  icon.setAttribute("viewBox", "0 0 24 24");
  icon.setAttribute("aria-hidden", "true");
  icon.innerHTML = drawing;
  element.prepend(icon);
  return element;
}

function ensureStyles() {
  if (document.querySelector("[data-nesting-file-picker-style]")) return;
  const link = document.createElement("link");
  link.rel = "stylesheet";
  link.href = new URL("./nestingPartFilePicker.css", import.meta.url).href;
  link.dataset.nestingFilePickerStyle = "";
  const appearance = document.createElement("style");
  appearance.dataset.tubeDesignerDialogAppearance = "";
  appearance.textContent = dialogAppearanceCss;
  document.head.append(link, appearance);
}

/** An isolated file browser: preview resources never enter the workbench View. */
export function chooseNestingPartFiles(context, view) {
  return chooseNestingFiles(context, view, CAD_PICKER);
}

export async function chooseNestingProfileDxf(context, view) {
  const paths = await chooseNestingFiles(context, view, DXF_PICKER);
  return paths[0] ?? '';
}

function chooseNestingFiles(context, view, config) {
  const sceneProxy = context.sceneProxy;
  const productProxy = context.productProxy;
  if (typeof sceneProxy?.invoke !== "function") return Promise.reject(new Error("当前项目未连接，无法浏览下料零件。"));
  disposeInactiveNestingPartFilePicker(context, view);
  if (view.tubeDesignerNestingFilePicker?.fileType !== config.fileType) view.tubeDesignerNestingFilePicker?.close();
  if (view.tubeDesignerNestingFilePicker) return view.tubeDesignerNestingFilePicker.promise;
  if (config.fileType === 'dxf' && typeof productProxy?.invoke !== 'function') return Promise.reject(new Error('当前产品未连接，无法预览 DXF 截面。'));
  let session = config.sessions.get(sceneProxy);
  if (!session) {
    session = { previewKey: `nesting-file-picker-${Date.now()}-${++pickerSequence}`, queue: Promise.resolve(), cache: new Map(), cacheBytes: 0 };
    config.sessions.set(sceneProxy, session);
  }
  ensureStyles();
  const previousFocus = document.activeElement;
  const backdrop = document.createElement("div");
  backdrop.className = "tube-designer-modal-backdrop tube-nesting-file-picker-backdrop";
  backdrop.innerHTML = `<section class="tube-nesting-file-picker" role="dialog" aria-modal="true" aria-labelledby="nesting-file-picker-title" data-window-state-ignore>
    <header><strong id="nesting-file-picker-title">导入下料零件</strong><button type="button" class="tube-designer-dialog-close" data-nesting-file-action="cancel" aria-label="关闭文件选择">×</button></header>
    <div class="tube-nesting-file-navigation"><label>目录<input data-nesting-file-directory aria-label="文件目录" spellcheck="false"></label></div>
    <div class="tube-nesting-file-content"><div class="tube-nesting-file-browser"><div class="tube-nesting-file-list" role="listbox" aria-multiselectable="true" aria-label="STEP / IGES 文件"></div><p class="tube-nesting-file-selection-hint">Ctrl 点选 · Shift 连选 · Ctrl+A 全选</p><p data-nesting-file-directory-status role="status"></p></div>
    <div class="tube-nesting-file-preview"><div data-nesting-file-viewport></div><div class="tube-nesting-file-preview-message" data-nesting-file-preview-status role="status">选择文件查看线框预览</div><div class="tube-nesting-file-preview-caption">右键拖动旋转 · 滚轮缩放</div></div></div>
    <footer><span data-nesting-file-selection>STEP / STP / IGES / IGS</span></footer>
  </section>`;
  const dialog = backdrop.querySelector("section");
  dialog.querySelector('header strong').textContent = config.title;
  dialog.querySelector('.tube-nesting-file-selection-hint').hidden = !config.multiple;
  const caption = dialog.querySelector('.tube-nesting-file-preview-caption');
  if (config.caption) caption.textContent = config.caption;
  else caption.remove();
  const navigation = dialog.querySelector(".tube-nesting-file-navigation");
  const directoryInput = dialog.querySelector("[data-nesting-file-directory]");
  directoryInput.value = String(filePickerMemory.read(config.key, 'directory', ''));
  const list = dialog.querySelector(".tube-nesting-file-list");
  list.setAttribute('aria-label', config.label);
  list.setAttribute('aria-multiselectable', String(config.multiple));
  const directoryStatus = dialog.querySelector("[data-nesting-file-directory-status]");
  const previewStatus = dialog.querySelector("[data-nesting-file-preview-status]");
  previewStatus.textContent = config.idleMessage;
  const selectionLabel = dialog.querySelector("[data-nesting-file-selection]");
  selectionLabel.textContent = config.label;
  const up = navigationButton("up", "上级", '<path d="M12 20V4M5 11l7-7 7 7"/>');
  const browse = navigationButton("browse", "浏览…", '<path d="M3 18V5h6l2 2h10v6M3 18l3-8h16l-3 8Z"/>');
  const cancel = button("cancel", "取消");
  const open = button("open", "打开");
  open.className = "tube-designer-primary";
  open.disabled = true;
  const bridge = context.appProxy?.bridge ?? context.productProxy?.bridge ?? sceneProxy.bridge;
  browse.hidden = typeof bridge?.openDirectoryDialog !== "function";
  navigation.append(up, browse);
  const footer = dialog.querySelector("footer");
  if (config.showFitButton) footer.append(button("fit", "适合窗口"));
  footer.append(cancel, open);
  document.body.append(backdrop);
  let resolveChoice;
  const state = {
    promise: new Promise(resolve => { resolveChoice = resolve; }), backdrop, sceneProxy,
    fileType: config.fileType, productProxy,
    previewKey: session.previewKey,
    directory: "", parentDirectory: "", selectedPath: "", selectedPaths: new Set(), anchorPath: "", directorySequence: 0, previewSequence: 0,
    closed: false, viewport: null, previewQueue: session.queue, ownerView: view,
    previewTimer: null, selectedIdentity: "", previewStatus: "idle", displaySequence: 0,
    previewCache: session.cache,
    selectedDirectoryPath: "", folders: new Map(), folderSequence: 0, interactionSequence: 0,
    get previewCacheBytes() { return session.cacheBytes; },
    set previewCacheBytes(value) { session.cacheBytes = value; },
  };
  view.tubeDesignerNestingFilePicker = state;
  activePicker = state;
  const current = () => !state.closed && currentScene();
  const showPreviewMessage = message => { previewStatus.textContent = message; previewStatus.hidden = !message; };
  const hidePreview = () => {
    state.viewport?.clear();
  };
  const close = (selectedPaths, restoreFocus = true) => {
    if (state.closed) return;
    state.closed = true;
    state.directorySequence++;
    state.previewSequence++;
    clearTimeout(state.previewTimer);
    state.previewTimer = null;
    // Keep bounded wireframe data for reopening this scene's chooser. They are
    // independent of the transient native resources released below.
    observer.disconnect();
    backdrop.removeEventListener("click", onClick);
    backdrop.removeEventListener("dblclick", onDoubleClick);
    backdrop.removeEventListener("keydown", onKeyDown);
    for (const type of ["pointerdown", "wheel", "input"]) backdrop.removeEventListener(type, onInteraction);
    state.viewport?.dispose();
    state.viewport = null;
    // Wait for pending native work before releasing this preview session.
    session.queue = state.previewQueue.finally(() => config.fileType === 'cad'
      ? sceneProxy.invoke("TubeDesigner.ReleaseNestingPartFilePreview", { previewKey: state.previewKey }, { timeoutMs: 30000 })
      : undefined).catch(() => {});
    backdrop.remove();
    if (view.tubeDesignerNestingFilePicker === state) view.tubeDesignerNestingFilePicker = null;
    if (activePicker === state) activePicker = null;
    // A successful open changes the selected part. Never restore a same-named
    // field on that new part; cancellation only restores the original DOM node.
    if (restoreFocus && !selectedPaths.length && previousFocus?.isConnected) previousFocus.focus?.({ preventScroll: true });
    resolveChoice(currentScene() ? selectedPaths : []);
  };
  const currentScene = () => context.sceneProxy === sceneProxy && (config.fileType !== 'dxf' || context.productProxy === productProxy);
  state.close = () => close([], false);
  const observer = new MutationObserver(() => {
    if (!currentScene() || (context.mount && !context.mount.isConnected)) close([], false);
  });
  observer.observe(document.body, { childList: true, subtree: true });

  function listDirectory(directory) {
    const payload = directory ? { directory } : {};
    if (config.fileType === 'dxf') payload.fileType = 'dxf';
    return sceneProxy.invoke('TubeDesigner.ListNestingPartFiles', payload, { timeoutMs: 30000 });
  }

  async function loadDirectory(directory, { selectDirectory = "" } = {}) {
    const sequence = ++state.directorySequence;
    const inputAtRequest = directoryInput.value;
    const focusAtRequest = document.activeElement;
    const interactionAtRequest = state.interactionSequence;
    ++state.previewSequence;
    clearTimeout(state.previewTimer);
    state.previewTimer = null;
    state.selectedPath = "";
    state.selectedPaths.clear();
    state.selectedDirectoryPath = "";
    state.folders.clear();
    state.anchorPath = "";
    state.selectedIdentity = "";
    state.previewStatus = "idle";
    open.disabled = true;
    open.textContent = "打开";
    selectionLabel.textContent = config.label;
    hidePreview();
    showPreviewMessage(config.idleMessage);
    directoryStatus.textContent = "正在读取目录…";
    up.disabled = true;
    list.replaceChildren();
    list.setAttribute("aria-busy", "true");
    try {
      const response = await listDirectory(directory);
      if (!current() || sequence !== state.directorySequence) return;
      state.directory = String(response.directory ?? "");
      state.parentDirectory = String(response.parentDirectory ?? "");
      if (directoryInput.value === inputAtRequest && directoryInput.value !== state.directory) {
        const selection = [directoryInput.selectionStart, directoryInput.selectionEnd, directoryInput.selectionDirection];
        directoryInput.value = state.directory;
        if (document.activeElement === directoryInput) directoryInput.setSelectionRange(...selection);
      }
      if (directoryInput.value === state.directory) filePickerMemory.write(config.key, 'directory', state.directory);
      up.disabled = !state.parentDirectory;
      const entries = sortedEntries(response);
      const identities = new Map(entries.filter(entry => !entry.directory).map(entry => [String(entry.path), fileIdentity(entry)]));
      for (const [identity, cached] of state.previewCache) {
        if (identities.has(cached.sourcePath) && identities.get(cached.sourcePath) !== identity) {
          state.previewCache.delete(identity);
          state.previewCacheBytes -= cached.byteLength;
        }
      }
      list.replaceChildren(...entries.map(entry => createEntry(entry)));
      directoryStatus.textContent = entries.length ? "" : config.emptyMessage;
      if (selectDirectory) {
        const folderRow = allRows().find(row => row.dataset.nestingFileIsDirectory === "true"
          && pathKey(row.dataset.nestingFilePath) === pathKey(selectDirectory));
        if (folderRow) {
          selectFolder(folderRow);
          // Only navigation initiated by the user may reposition the list. A
          // delayed response must not steal a newer input focus or scroll.
          if (interactionAtRequest === state.interactionSequence && directoryInput.value === state.directory) {
            folderRow.scrollIntoView({ block: "nearest" });
            if (document.activeElement === focusAtRequest) folderRow.focus({ preventScroll: true });
          }
        }
        return;
      }
      const rememberedPaths = filePickerMemory.read(config.key, 'selectedPaths', []);
      const files = fileRows();
      for (const row of files) {
        if (Array.isArray(rememberedPaths) && rememberedPaths.includes(row.dataset.nestingFilePath)) state.selectedPaths.add(row.dataset.nestingFilePath);
        if (!config.multiple && state.selectedPaths.size) break;
      }
      const rememberedPath = String(filePickerMemory.read(config.key, 'selectedPath', ''));
      const rememberedRow = files.find(row => row.dataset.nestingFilePath === rememberedPath && state.selectedPaths.has(rememberedPath))
        ?? files.find(row => state.selectedPaths.has(row.dataset.nestingFilePath));
      state.anchorPath = rememberedRow?.dataset.nestingFilePath ?? "";
      updateSelection(false);
      if (rememberedRow) void previewFile(rememberedRow);
    } catch (error) {
      if (!current() || sequence !== state.directorySequence) return;
      directoryStatus.textContent = error?.message ?? String(error);
    } finally {
      if (current() && sequence === state.directorySequence) list.removeAttribute("aria-busy");
    }
  }

  function sortedEntries(response) {
    return (response.entries ?? []).filter(entry => entry.directory || config.accepts.test(entry.name ?? entry.path ?? ""))
      .sort((left, right) => Number(Boolean(right.directory)) - Number(Boolean(left.directory))
        || String(left.name).localeCompare(String(right.name), "zh-CN", { numeric: true }));
  }

  function allRows() {
    return [...list.querySelectorAll("[data-nesting-file-path]")];
  }

  function visibleRows() {
    return allRows().filter(row => !row.closest("[hidden]"));
  }

  function fileRows(visible = true) {
    return (visible ? visibleRows() : allRows()).filter(row => row.dataset.nestingFileIsDirectory === "false");
  }

  function createEntry(entry, depth = 0, parent = null) {
    const item = document.createElement("div");
    item.className = "tube-nesting-file-item";
    const line = document.createElement("div");
    line.className = "tube-nesting-file-entry";
    line.style.setProperty("--nesting-file-depth", depth);
    const row = button("select", "");
    row.className = "tube-nesting-file-row";
    row.dataset.nestingFilePath = String(entry.path);
    row.dataset.nestingFileIsDirectory = String(Boolean(entry.directory));
    row.dataset.nestingFileSizeBytes = String(entry.sizeBytes ?? "");
    row.dataset.nestingFileLastModifiedNs = String(entry.lastModifiedNs ?? "");
    row.setAttribute("role", "option");
    row.setAttribute("aria-selected", "false");
    row.title = String(entry.name);
    row.append(entryIcon(entry.directory), document.createTextNode(String(entry.name)));
    item.append(line);
    if (entry.directory) {
      const disclosure = button("toggle-folder", "");
      disclosure.className = "tube-nesting-folder-toggle";
      disclosure.innerHTML = '<svg viewBox="0 0 16 16" aria-hidden="true"><path d="m6 3 5 5-5 5"/></svg>';
      const group = document.createElement("div");
      group.className = "tube-nesting-file-children";
      group.id = `nesting-folder-${pickerSequence}-${++state.folderSequence}`;
      group.setAttribute("role", "group");
      group.hidden = true;
      disclosure.setAttribute("aria-controls", group.id);
      disclosure.setAttribute("aria-expanded", "false");
      disclosure.setAttribute("aria-label", `展开 ${entry.name}`);
      disclosure.title = `展开 ${entry.name}`;
      line.append(disclosure, row);
      item.append(group);
      const folder = { item, row, disclosure, group, depth, parent, name: String(entry.name), expanded: false, loaded: false, loading: false };
      state.folders.set(row, folder);
    } else {
      const spacer = document.createElement("span");
      spacer.className = "tube-nesting-folder-spacer";
      spacer.setAttribute("aria-hidden", "true");
      line.append(spacer, row);
    }
    return item;
  }

  async function toggleFolder(folder, expanded = !folder.expanded) {
    folder.expanded = expanded;
    folder.group.hidden = !expanded;
    folder.item.classList.toggle("is-expanded", expanded);
    folder.disclosure.setAttribute("aria-expanded", String(expanded));
    folder.disclosure.setAttribute("aria-label", `${expanded ? "收起" : "展开"} ${folder.name}`);
    folder.disclosure.title = folder.disclosure.getAttribute("aria-label");
    if (!expanded || folder.loaded || folder.loading) return;
    const sequence = state.directorySequence;
    folder.loading = true;
    folder.disclosure.setAttribute("aria-busy", "true");
    const status = document.createElement("p");
    status.setAttribute("role", "status");
    status.textContent = "正在读取目录…";
    folder.group.replaceChildren(status);
    try {
      const response = await listDirectory(folder.row.dataset.nestingFilePath);
      if (!current() || sequence !== state.directorySequence) return;
      const entries = sortedEntries(response);
      // Change only this branch; the input, other rows, preview and current
      // scrolling/focus all remain in place. A collapsed branch stays hidden.
      folder.group.replaceChildren(...entries.map(entry => createEntry(entry, folder.depth + 1, folder)));
      if (!entries.length) { status.textContent = config.emptyMessage; folder.group.append(status); }
      folder.loaded = true;
      updateSelection(false);
    } catch (error) {
      if (current() && sequence === state.directorySequence) status.textContent = error?.message ?? String(error);
    } finally {
      folder.loading = false;
      folder.disclosure.removeAttribute("aria-busy");
    }
  }

  function selectFolder(row) {
    state.selectedDirectoryPath = row.dataset.nestingFilePath;
    state.selectedPaths.clear();
    state.selectedPath = "";
    state.anchorPath = "";
    state.selectedIdentity = "";
    state.previewStatus = "idle";
    ++state.previewSequence;
    clearTimeout(state.previewTimer);
    state.previewTimer = null;
    hidePreview();
    showPreviewMessage(config.idleMessage);
    updateSelection();
  }

  function selectedFiles() {
    return fileRows(false).map(row => row.dataset.nestingFilePath).filter(path => state.selectedPaths.has(path));
  }

  function updateSelection(remember = true) {
    const paths = selectedFiles();
    for (const row of allRows()) {
      const selected = state.selectedPaths.has(row.dataset.nestingFilePath) || row.dataset.nestingFilePath === state.selectedDirectoryPath;
      row.setAttribute("aria-selected", String(selected));
      row.parentElement.classList.toggle("is-selected", selected);
    }
    open.disabled = !paths.length;
    open.textContent = paths.length > 1 ? `打开（${paths.length}）` : "打开";
    const name = state.selectedPath.split(/[\\/]/).at(-1);
    selectionLabel.textContent = paths.length > 1 ? `已选 ${paths.length} 个文件${name ? ` · 预览：${name}` : ""}`
      : paths.length ? paths[0].split(/[\\/]/).at(-1)
      : state.selectedDirectoryPath ? state.selectedDirectoryPath.split(/[\\/]/).at(-1) : config.label;
    if (remember) filePickerMemory.write(config.key, 'selectedPaths', paths);
  }

  function selectFile(row, { ctrlKey = false, metaKey = false, shiftKey = false } = {}) {
    state.selectedDirectoryPath = "";
    const path = row.dataset.nestingFilePath;
    const additive = config.multiple && (ctrlKey || metaKey);
    const files = fileRows();
    const anchor = files.findIndex(item => item.dataset.nestingFilePath === state.anchorPath);
    const index = files.indexOf(row);
    if (config.multiple && shiftKey && anchor >= 0) {
      if (!additive) state.selectedPaths.clear();
      for (const item of files.slice(Math.min(anchor, index), Math.max(anchor, index) + 1)) state.selectedPaths.add(item.dataset.nestingFilePath);
    } else {
      if (!additive) state.selectedPaths.clear();
      if (additive && state.selectedPaths.has(path)) state.selectedPaths.delete(path);
      else state.selectedPaths.add(path);
      state.anchorPath = path;
    }
    updateSelection();
    void previewFile(row);
  }

  async function previewFile(row) {
    const path = row.dataset.nestingFilePath;
    if (!config.accepts.test(path)) return;
    const identity = fileIdentity({ path, sizeBytes: row.dataset.nestingFileSizeBytes, lastModifiedNs: row.dataset.nestingFileLastModifiedNs });
    if (state.selectedPath === path && state.selectedIdentity === identity && ["loading", "ready"].includes(state.previewStatus)) return;
    state.selectedPath = path;
    filePickerMemory.write(config.key, 'selectedPath', path);
    state.selectedIdentity = identity;
    state.previewStatus = "loading";
    updateSelection(false);
    const sequence = ++state.previewSequence;
    clearTimeout(state.previewTimer);
    state.previewTimer = null;
    hidePreview();
    showPreviewMessage(config.loadingMessage);
    const cached = identity && state.previewCache.get(identity);
    if (cached) {
      state.previewCache.delete(identity);
      state.previewCache.set(identity, cached);
      await displayPreview(cached, sequence);
      return;
    }
    state.previewTimer = setTimeout(() => {
      state.previewTimer = null;
      const task = state.previewQueue.then(async () => {
        if (!current() || sequence !== state.previewSequence) return;
        try {
          const response = config.fileType === 'dxf'
            ? await productProxy.invoke('TubeDesigner.ImportProfileDxf', { sourcePath: path }, { timeoutMs: 60000 })
            : await sceneProxy.invoke("TubeDesigner.PreviewNestingPartFile", { sourcePath: path, previewKey: state.previewKey, representation: "wireframe" }, { timeoutMs: 120000 });
          if (!currentScene()) return;
          const data = config.fileType === 'dxf' ? response?.profile : response?.wireframe;
          if (config.fileType === 'dxf' ? !data?.contours?.length : !data?.polylines?.length) throw new Error('文件未返回有效的预览轮廓。');
          const freshIdentity = fileIdentity(response);
          const candidate = { sourcePath: path, identity: freshIdentity, data,
            byteLength: JSON.stringify(data).length * 2 };
          // Retain completed work even if selection changed during native read.
          rememberPreview(candidate);
          if (!current() || sequence !== state.previewSequence) return;
          if (freshIdentity) {
            row.dataset.nestingFileSizeBytes = String(response.sizeBytes);
            row.dataset.nestingFileLastModifiedNs = String(response.lastModifiedNs);
            state.selectedIdentity = freshIdentity;
          }
          await displayPreview(candidate, sequence, true);
        } catch (error) {
          await previewFailed(error, sequence, true);
        }
      });
      state.previewQueue = task;
      session.queue = task;
    }, PREVIEW_DEBOUNCE_MS);
  }

  function rememberPreview(candidate) {
    const { identity, byteLength } = candidate;
    if (!identity || !byteLength || byteLength > MAX_CACHE_BYTES || state.previewCache.has(identity)) return;
    state.previewCache.set(identity, candidate);
    state.previewCacheBytes += byteLength;
    while (state.previewCache.size > MAX_CACHE_ENTRIES || state.previewCacheBytes > MAX_CACHE_BYTES) {
      const oldest = state.previewCache.keys().next().value;
      state.previewCacheBytes -= state.previewCache.get(oldest).byteLength;
      state.previewCache.delete(oldest);
    }
  }

  async function previewFailed(error, sequence, nativePipeline = false) {
    if (!current() || sequence !== state.previewSequence) return;
    state.viewport?.dispose();
    state.viewport = null;
    state.previewStatus = "error";
    showPreviewMessage(error?.message ?? String(error));
    if (config.fileType === 'dxf') return;
    const release = () => sceneProxy.invoke("TubeDesigner.ReleaseNestingPartFilePreview", { previewKey: state.previewKey }, { timeoutMs: 30000 }).catch(() => {});
    if (nativePipeline) await release();
    else {
      const cleanup = state.previewQueue.then(release);
      state.previewQueue = cleanup;
      session.queue = cleanup;
      await cleanup;
    }
  }

  async function displayPreview(candidate, sequence, nativePipeline = false) {
    if (!current() || sequence !== state.previewSequence) return;
    try {
      if (!state.viewport) {
        state.viewport = config.fileType === 'dxf' ? createNestingDxfPreview() : createNestingWireframePreview();
        if (config.fileType === 'cad') state.viewport.setData = state.viewport.setWireframe;
        state.viewport.mount(dialog.querySelector("[data-nesting-file-viewport]"));
      }
      state.viewport.setData(candidate.data);
      rememberPreview(candidate);
      state.previewStatus = "ready";
      showPreviewMessage("");
    } catch (error) {
      await previewFailed(error, sequence, nativePipeline);
    }
  }

  async function onClick(event) {
    const target = event.target.closest?.("[data-nesting-file-action]");
    if (!target || !backdrop.contains(target)) return;
    switch (target.dataset.nestingFileAction) {
      case "cancel": close([]); break;
      case "open": if (state.selectedPaths.size) close(selectedFiles()); break;
      case "up": if (state.parentDirectory) await loadDirectory(state.parentDirectory, { selectDirectory: state.directory }); break;
      case "fit": state.viewport?.fitView(); break;
      case "browse": {
        browse.disabled = true;
        try {
          const directory = await bridge.openDirectoryDialog({ title: `选择 ${config.fileType === 'dxf' ? 'DXF' : 'STEP / IGES'} 文件目录`, initialDirectory: state.directory });
          if (current() && directory) await loadDirectory(String(directory));
        } catch (error) { if (current()) directoryStatus.textContent = error?.message ?? String(error); }
        finally { if (current()) browse.disabled = false; }
        break;
      }
      case "select":
        if (target.dataset.nestingFileIsDirectory === "true") selectFolder(target);
        else selectFile(target, event);
        break;
      case "toggle-folder": {
        const row = target.parentElement.querySelector("[data-nesting-file-path]");
        await toggleFolder(state.folders.get(row));
        break;
      }
    }
  }
  function onDoubleClick(event) {
    const row = event.target.closest?.("[data-nesting-file-path]");
    if (row?.dataset.nestingFileIsDirectory === "true") void loadDirectory(row.dataset.nestingFilePath);
    if (row?.dataset.nestingFileIsDirectory === "false" && state.selectedPaths.has(row.dataset.nestingFilePath)) close(selectedFiles());
  }
  function onKeyDown(event) {
    onInteraction();
    if (event.key === "Escape") { event.preventDefault(); event.stopPropagation(); close([]); }
    if (event.key === "Enter" && event.target === directoryInput) { event.preventDefault(); void loadDirectory(directoryInput.value.trim()); }
    if (list.contains(event.target)) {
      if (config.multiple && (event.ctrlKey || event.metaKey) && event.key.toLowerCase() === "a") {
        event.preventDefault();
        const files = fileRows();
        state.selectedDirectoryPath = "";
        state.selectedPaths = new Set(files.map(row => row.dataset.nestingFilePath));
        updateSelection();
        if (!state.selectedPath && files.length) {
          state.anchorPath = files[0].dataset.nestingFilePath;
          void previewFile(files[0]);
        }
      } else if (event.key === "Enter" && event.target.dataset.nestingFileAction !== "toggle-folder") {
        event.preventDefault();
        if (event.target.dataset.nestingFileIsDirectory === "true") void loadDirectory(event.target.dataset.nestingFilePath);
        else if (state.selectedPaths.size) close(selectedFiles());
      } else if (event.key === "ArrowDown" || event.key === "ArrowUp") {
        event.preventDefault();
        const rows = visibleRows();
        const currentRow = event.target.closest(".tube-nesting-file-entry")?.querySelector("[data-nesting-file-path]");
        const index = rows.indexOf(currentRow);
        const row = rows[Math.max(0, Math.min(rows.length - 1, index + (event.key === "ArrowDown" ? 1 : -1)))];
        row?.focus({ preventScroll: true });
        row?.scrollIntoView({ block: "nearest" });
        if (row?.dataset.nestingFileIsDirectory === "false" && !(event.ctrlKey || event.metaKey)) selectFile(row, event);
        else if (row?.dataset.nestingFileIsDirectory === "true" && !(event.ctrlKey || event.metaKey)) selectFolder(row);
      } else if (event.key === "ArrowRight" || event.key === "ArrowLeft") {
        const row = event.target.closest(".tube-nesting-file-entry")?.querySelector("[data-nesting-file-path]");
        const folder = state.folders.get(row);
        if (folder) {
          event.preventDefault();
          if (event.key === "ArrowRight") {
            if (!folder.expanded) void toggleFolder(folder, true);
            else folder.group.querySelector("[data-nesting-file-path]")?.focus({ preventScroll: true });
          } else if (folder.expanded) void toggleFolder(folder, false);
          else folder.parent?.row.focus({ preventScroll: true });
        } else if (event.key === "ArrowLeft") {
          const parentRow = row?.closest(".tube-nesting-file-children")?.previousElementSibling?.querySelector("[data-nesting-file-path]");
          if (parentRow) { event.preventDefault(); parentRow.focus({ preventScroll: true }); }
        }
      }
    }
    if (event.key === "Tab") {
      const controls = [...dialog.querySelectorAll("input,button")].filter(node => !node.disabled && !node.hidden && node.getClientRects().length);
      const index = controls.indexOf(document.activeElement);
      if (event.shiftKey && index <= 0) { event.preventDefault(); controls.at(-1)?.focus(); }
      else if (!event.shiftKey && index === controls.length - 1) { event.preventDefault(); controls[0]?.focus(); }
    }
  }
  function onInteraction() { state.interactionSequence++; }
  backdrop.addEventListener("click", onClick);
  backdrop.addEventListener("dblclick", onDoubleClick);
  backdrop.addEventListener("keydown", onKeyDown);
  for (const type of ["pointerdown", "wheel", "input"]) backdrop.addEventListener(type, onInteraction);
  directoryInput.addEventListener('input', () => filePickerMemory.write(config.key, 'directory', directoryInput.value));
  directoryInput.focus({ preventScroll: true });
  void loadDirectory(directoryInput.value.trim());
  return state.promise;
}

export function disposeNestingPartFilePicker(view) {
  view.tubeDesignerNestingFilePicker?.close();
}

/** AppShell can replace the project context while reusing its connected mount. */
export function disposeInactiveNestingPartFilePicker(context, view) {
  if (activePicker && (activePicker.ownerView !== view || activePicker.sceneProxy !== context.sceneProxy
    || (activePicker.fileType === 'dxf' && activePicker.productProxy !== context.productProxy) || view.activeAreaId !== "nesting")) {
    activePicker.close();
  }
}
