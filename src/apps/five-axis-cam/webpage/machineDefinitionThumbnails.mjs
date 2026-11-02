import {escapeAttr as attr} from '../../_shared/workbench/utils/format.mjs';

// These images are rendered from the bundled definitions and their visual meshes.
// Unknown/imported definitions keep an explicit placeholder, never another machine's image.
const bundled = new Map([
  ['b21ac27d-75e8-4847-a80a-4c25feaa3015','LC-3015'],
  ['bf0d5260-2b94-4454-929e-c075b1ee1600','IR-1600'],
  ['f4ed0130-37ef-45fb-9883-7af0b1fc1000','RC-1000'],
  ['a30b2cab-c634-4b3a-94c0-c7ad1eac0800','TC-800'],
]);
export function machineDefinitionThumbnail(definition){
  const name=bundled.get(definition.id);
  if(name)return `<img class="five-machine-thumbnail" src="${attr(new URL(`../resources/machines/thumbnails/${name}.png`,import.meta.url).href)}" alt="${attr(definition.name||definition.modelName||name)}缩略图" loading="lazy" width="160" height="94">`;
  return '<span class="five-machine-thumbnail five-machine-thumbnail-empty"><svg viewBox="0 0 48 40" aria-hidden="true"><path d="M6 33h36V14H6Zm5 0V6h26v8M14 14v12h20V14M24 6v15m-4 0h8"/></svg><small>暂无预览</small></span>';
}
