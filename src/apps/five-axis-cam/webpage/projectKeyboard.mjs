const attached=new Map();

export function ownedProjectKeydown(mount,binding,isRegistered,handle){
  return event=>{
    if(event.defaultPrevented||!mount.isConnected||binding.view?.disposed||!isRegistered()
      ||binding.area!==binding.view?.activeAreaId||binding.context.isCurrentProject?.()===false)return;
    handle(event);
  };
}

function detach(binding){
  const record=attached.get(binding);if(!record)return;
  attached.delete(binding);
  record.document.removeEventListener('keydown',binding.keydown);
  record.document.removeEventListener('visibilitychange',binding.visibility);
  binding.disposeMachineJog?.();binding.disposeMachineJog=null;
}

export function activateProjectDocumentBinding(mount,binding,attachJog){
  const document=mount.ownerDocument;
  // Area mounts are cached, while their project view is shared. Only the
  // current mount owns document shortcuts and press-and-hold jog listeners.
  for(const [previous,record] of attached){
    if(previous===binding||record.document!==document)continue;
    if(!record.mount.isConnected||previous.view?.disposed||previous.view===binding.view||previous.context.isCurrentProject?.()===false)detach(previous);
  }
  if(!mount.isConnected||binding.view?.disposed||binding.context.isCurrentProject?.()===false)return;
  if(attached.has(binding))return;
  attachJog();
  document.addEventListener('keydown',binding.keydown);
  document.addEventListener('visibilitychange',binding.visibility);
  attached.set(binding,{mount,document});
}

export function releaseProjectDocumentBindings(view){
  for(const binding of [...attached.keys()])if(binding.view===view)detach(binding);
}
