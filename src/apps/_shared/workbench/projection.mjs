export const RENDER_ENTITY_VIEW_PROJECTION = Object.freeze([
  { alias: "geometry", component: "CRenderInstanceComponent", property: "GeometryResourceID", resourceReference: true, resourceVersionProperty: "GeometryResourceVersion" },
  { alias: "material", component: "CRenderInstanceComponent", property: "MaterialResourceID", resourceReference: true, resourceVersionProperty: "MaterialResourceVersion" },
  { alias: "geometryKind", component: "CRenderInstanceComponent", property: "GeometryKind" },
  { alias: "renderClass", component: "CRenderInstanceComponent", property: "RenderClass" },
  { alias: "layerMask", component: "CRenderInstanceComponent", property: "LayerMask" },
  { alias: "visible", component: "CRenderInstanceComponent", property: "Visible" },
  { alias: "selectable", component: "CRenderInstanceComponent", property: "Selectable" },
  { alias: "highlighted", component: "CRenderInstanceComponent", property: "Highlighted" },
  { alias: "selected", component: "CRenderInstanceComponent", property: "Selected" },
  { alias: "localToWorldMatrix", component: "CTransformComponent", property: "LocalToWorldMatrix" },
]);
