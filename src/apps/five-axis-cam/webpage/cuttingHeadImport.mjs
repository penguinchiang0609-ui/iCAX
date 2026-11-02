import {createFilePreviewPicker} from '../../../iCAX-UI/SDK/Forms/filePreviewPicker.mjs';

const accepts=/\.(?:urdf|sdf|step|stp|iges|igs|stl|obj)$/i;
const picker=createFilePreviewPicker({
 key:'cutting-head-file-picker',stateKey:'fiveHeadFilePicker',memoryNamespace:'icax.five-axis-cam.window-state',
 title:'导入切割头',fileType:'cutting-head',accepts,label:'URDF / SDF / STEP / IGES / STL / OBJ',multiple:false,
 fileFilters:[
  {id:'all',label:'所有切割头文件',accepts},
  {id:'assembly',label:'关节机构（URDF / SDF）',accepts:/\.(?:urdf|sdf)$/i},
  {id:'rigid',label:'刚性外形（STEP / IGES / STL / OBJ）',accepts:/\.(?:step|stp|iges|igs|stl|obj)$/i},
 ],
 preview:false,directoryDialog:false,idleMessage:'选择要导入的切割头文件',emptyMessage:'此目录没有支持的切割头文件',
 isActive:view=>!view.disposed&&view.fiveAxisArea==='toolpath',
 listDirectory:(context,directory)=>context.sceneProxy.invoke('CamWorkpiece.ListFiles',{...(directory?{directory}:{}),fileType:'cutting-head'},{timeoutMs:30000}),
});
export async function chooseCuttingHeadFile(context,view){return (await picker.choose(context,view))[0]??'';}
export const disposeCuttingHeadFilePicker=picker.dispose;
export const disposeInactiveCuttingHeadFilePicker=picker.disposeInactive;
