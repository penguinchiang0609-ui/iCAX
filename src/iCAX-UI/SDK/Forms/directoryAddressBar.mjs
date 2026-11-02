export function directorySegments(directory) {
  const path=String(directory??'').replaceAll('\\','/');
  let root='',label='',tail=path;
  const share=path.match(/^\/\/[^/]+\/[^/]+(?:\/|$)/);
  if(share){root=share[0].replace(/\/$/,'')+'/';label=root.slice(0,-1).replaceAll('/','\\');tail=path.slice(share[0].length);}
  else if(/^[a-z]:($|\/)/i.test(path)){root=path.slice(0,2)+'/';label=path.slice(0,2);tail=path.slice(path[2]==='/'?3:2);}
  else if(path.startsWith('/')){root='/';label='/';tail=path.slice(1);}
  const result=root?[{label,path:root}]:[];
  let accumulated=root;
  for(const part of tail.split('/').filter(Boolean)){accumulated+=(accumulated&&!accumulated.endsWith('/')?'/':'')+part;result.push({label:part,path:accumulated});}
  return result;
}

// Keeps the existing input node alive through breadcrumb/edit transitions.
// The owner retains directory I/O, cancellation and response ordering.
export function createDirectoryAddressBar(input,{navigate,onRestore=()=>{}}) {
  const root=document.createElement('div');root.className='tube-directory-address';root.tabIndex=0;root.setAttribute('role','group');root.setAttribute('aria-label','目录地址');root.title='点击路径跳转；点击空白处或按 Ctrl+L 输入路径';
  const crumbs=document.createElement('nav');crumbs.className='tube-directory-breadcrumbs';crumbs.setAttribute('aria-label','目录路径');
  input.parentNode.insertBefore(root,input);root.append(crumbs,input);
  let directory='',editing=false,disposed=false;
  const key=value=>String(value).replaceAll('\\','/').replace(/\/+$/,'').toLowerCase();
  function edit(selectAll=true){if(disposed)return;editing=true;root.classList.add('is-editing');input.focus({preventScroll:true});if(selectAll)input.select();}
  function collapse(moveFocus=false){editing=false;root.classList.remove('is-editing');if(moveFocus&&document.activeElement===input)root.focus({preventScroll:true});}
  function cancelEdit(){input.value=directory;onRestore(directory);collapse(true);}
  function setDirectory(value,{finishEditing=false,scrollToEnd=true}={}){
    const next=String(value??'');directory=next;root.dataset.directory=next;
    const parts=directorySegments(next),available=new Map([...crumbs.children].map(n=>[n.dataset.directoryPath,n]));let cursor=crumbs.firstChild;
    for(const [index,part] of parts.entries()){
      let item=available.get(part.path);if(item)available.delete(part.path);
      else {item=document.createElement('span');item.className='tube-directory-segment';item.dataset.directoryPath=part.path;const separator=document.createElement('span');separator.className='tube-directory-separator';separator.textContent='›';separator.setAttribute('aria-hidden','true');const button=document.createElement('button');button.type='button';button.dataset.nestingFileDirectoryTarget=part.path;item.append(separator,button);}
      item.firstChild.hidden=index===0;const button=item.lastChild;if(button.textContent!==part.label)button.textContent=part.label;button.title=part.path;button.setAttribute('aria-label','转到 '+part.path);if(index===parts.length-1)button.setAttribute('aria-current','location');else button.removeAttribute('aria-current');
      if(item!==cursor)crumbs.insertBefore(item,cursor);cursor=item.nextSibling;
    }
    for(const item of available.values())item.remove();
    crumbs.classList.toggle('is-empty',!parts.length);
    if(finishEditing&&key(input.value)===key(next))collapse(true);
    else if(document.activeElement!==input&&key(input.value)===key(next))collapse();
    if(scrollToEnd)crumbs.scrollLeft=crumbs.scrollWidth;
  }
  function click(event){
    const target=event.target.closest('[data-nesting-file-directory-target]');
    if(target&&root.contains(target)){
      const parts=directorySegments(directory),index=parts.findIndex(p=>p.path===target.dataset.nestingFileDirectoryTarget);
      if(index>=0&&index<parts.length-1)void navigate(parts[index].path,{selectDirectory:parts[index+1].path});
      return;
    }
    if(event.target!==input)edit();
  }
  function keydown(event){
    if(event.__directoryNavigation)return;
    if(event.key==='Escape'&&editing){event.preventDefault();event.stopPropagation();cancelEdit();return;}
    if(event.target===root&&['Enter',' '].includes(event.key)){event.preventDefault();edit();}
  }
  function focus(){editing=true;root.classList.add('is-editing');}
  function blur(){if(key(input.value)===key(directory))collapse();}
  root.addEventListener('click',click);root.addEventListener('keydown',keydown);input.addEventListener('focus',focus);input.addEventListener('blur',blur);
  return {root,input,edit,setDirectory,focus:()=>root.focus({preventScroll:true}),failed:()=>{editing=true;root.classList.add('is-editing');},dispose(){disposed=true;root.removeEventListener('click',click);root.removeEventListener('keydown',keydown);input.removeEventListener('focus',focus);input.removeEventListener('blur',blur);}};
}
