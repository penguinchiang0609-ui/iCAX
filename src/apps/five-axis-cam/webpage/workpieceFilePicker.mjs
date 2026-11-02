import { createFilePreviewPicker } from '../../../iCAX-UI/SDK/Forms/filePreviewPicker.mjs';
import { createWireframePreview } from '../../../iCAX-UI/SDK/Forms/wireframePreview.mjs';
import {readJsonDocument} from '../../../iCAX-UI/SDK/Resources/jsonDocument.mjs';

async function previewWorkpiece(context, sourcePath, previewKey, { signal }) {
  let task, releasePromise;
  const release = () => {
    if (task && !releasePromise) releasePromise = context.sceneProxy.invoke('CamWorkpiece.ReleaseFilePreview',
      { epoch: task.epoch, taskId: task.taskId }).catch(() => {});
    return releasePromise;
  };
  const abort = () => { void release(); };
  const checkCancelled = () => { if (signal.aborted) throw new DOMException('模型预览已取消', 'AbortError'); };
  signal.addEventListener('abort', abort, { once: true });
  try {
    checkCancelled();
    task = await context.sceneProxy.invoke('CamWorkpiece.PreviewFile', { sourcePath, previewKey });
    checkCancelled();
    while (task.status === 'Running') {
      await new Promise(resolve => setTimeout(resolve, 100));
      checkCancelled();
      task = await context.sceneProxy.invoke('CamWorkpiece.QueryFilePreview', { epoch: task.epoch, taskId: task.taskId });
      checkCancelled();
    }
    if (task.status !== 'Completed') throw Error(task.error || '模型预览已取消');
    const body=await readJsonDocument(context.sceneProxy.resources,task.result.wireframeResourceRef,{signal,maxDocumentBytes:128*1024*1024});
    checkCancelled();return {...task.result,wireframe:body.wireframe};
  } finally {
    signal.removeEventListener('abort', abort);
    await release();
  }
}

const picker = createFilePreviewPicker({
  key: 'workpiece-file-picker', stateKey: 'fiveAxisFilePicker', memoryNamespace: 'icax.five-axis-cam.window-state',
  title: '导入模型', fileType: 'cad', accepts: /\.(?:step|stp|iges|igs)$/i,
  fileFilters:[
    {id:'all',label:'所有支持格式',accepts:/\.(?:step|stp|iges|igs)$/i},
    {id:'step',label:'STEP（*.step; *.stp）',accepts:/\.(?:step|stp)$/i},
    {id:'iges',label:'IGES（*.iges; *.igs）',accepts:/\.(?:iges|igs)$/i},
  ],
  label: 'STEP / STP / IGES / IGS', multiple: false,
  idleMessage: '选择文件查看模型预览', loadingMessage: '正在加载模型预览…', emptyMessage: '此目录没有 STEP / IGES 文件',
  caption: '右键拖动旋转 · 中键平移 · 滚轮缩放', showFitButton: false,
  isActive: view => !view.disposed, createPreview: createWireframePreview,
  listDirectory: (context, directory) => context.sceneProxy.invoke('CamWorkpiece.ListFiles', directory ? { directory } : {}, { timeoutMs: 30000 }),
  preview: previewWorkpiece,
  previewData: response => response?.wireframe, hasData: data => data?.polylines?.length,
});
export async function chooseModelFile(context, view) { return (await picker.choose(context, view))[0] ?? ''; }
export const disposeWorkpieceFilePicker = picker.dispose;
export const disposeInactiveWorkpieceFilePicker = picker.disposeInactive;
