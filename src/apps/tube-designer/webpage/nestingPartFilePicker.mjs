import { createFilePreviewPicker } from '../../../iCAX-UI/SDK/Forms/filePreviewPicker.mjs';
import { createWireframePreview } from '../../../iCAX-UI/SDK/Forms/wireframePreview.mjs';
import { createNestingDxfPreview } from './nestingDxfPreview.mjs';
import { dialogAppearanceCss } from './styles/dialogAppearance.css.mjs';

function ensureStyles() {
  if (document.querySelector('[data-tube-designer-dialog-appearance]')) return;
  const style = document.createElement('style');
  style.dataset.tubeDesignerDialogAppearance = '';
  style.textContent = dialogAppearanceCss;
  document.head.append(style);
}
const common = { stateKey: 'tubeDesignerNestingFilePicker', memoryNamespace: 'icax.tube-designer.window-state',
  isActive: view => view.activeAreaId === 'nesting', ensureStyles,
  listDirectory: (context, directory) => context.sceneProxy.invoke('TubeDesigner.ListNestingPartFiles', directory ? { directory } : {}, { timeoutMs: 30000 }) };
const cad = createFilePreviewPicker({ ...common, key: 'nesting-file-picker', title: '导入下料零件', fileType: 'cad',
  accepts: /\.(?:step|stp|iges|igs)$/i, label: 'STEP / STP / IGES / IGS', multiple: true,
  idleMessage: '选择文件查看线框预览', loadingMessage: '正在加载线框预览…', emptyMessage: '此目录没有 STEP / IGES 文件',
  caption: '右键拖动旋转 · 滚轮缩放', showFitButton: true, createPreview: createWireframePreview,
  previewData: response => response?.wireframe && ({ ...response.wireframe,
    polylines: JSON.parse(response.wireframe.polylinesJson) }), hasData: data => data?.polylines?.length,
  preview: (context, sourcePath, previewKey) => context.sceneProxy.invoke('TubeDesigner.PreviewNestingPartFile', { sourcePath, previewKey, representation: 'wireframe' }, { timeoutMs: 120000 }),
  release: (context, previewKey) => context.sceneProxy.invoke('TubeDesigner.ReleaseNestingPartFilePreview', { previewKey }, { timeoutMs: 30000 }),
});
const dxf = createFilePreviewPicker({ ...common, key: 'nesting-profile-dxf-picker', title: '选择 DXF 截面', fileType: 'dxf',
  accepts: /\.dxf$/i, label: 'DXF 截面', multiple: false,
  idleMessage: '选择文件查看截面预览', loadingMessage: '正在加载截面预览…', emptyMessage: '此目录没有 DXF 文件',
  caption: '', showFitButton: false, createPreview: createNestingDxfPreview,
  previewData: response => response?.profile, hasData: data => data?.contours?.length,
  listDirectory: (context, directory) => context.sceneProxy.invoke('TubeDesigner.ListNestingPartFiles', { ...(directory ? { directory } : {}), fileType: 'dxf' }, { timeoutMs: 30000 }),
  preview: (context, sourcePath) => context.productProxy.invoke('TubeDesigner.ImportProfileDxf', { sourcePath }, { timeoutMs: 60000 }),
});
export function chooseNestingPartFiles(context, view) { return cad.choose(context, view); }
export async function chooseNestingProfileDxf(context, view) { return (await dxf.choose(context, view))[0] ?? ''; }
export function disposeNestingPartFilePicker(view) { cad.dispose(view); dxf.dispose(view); }
export function disposeInactiveNestingPartFilePicker(context, view) { cad.disposeInactive(context, view); dxf.disposeInactive(context, view); }
