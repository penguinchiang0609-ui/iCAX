import assert from "node:assert/strict";
import { renderDesignerProductPartsDock } from "../../apps/tube-designer/webpage/designerViews.mjs";

const activeProduct = { entityId: "product-active", name: "当前防盗窗", templateId: "single-face-security-window" };
const activePart = {
  entityId: "part-active", index: 1, partNumber: "TD-001", name: "外框左边",
  sourceMemberId: "member-active", quantity: 1, length: 1800, profile: { specification: "矩形管 38 × 38 × 1.2" },
};
const otherPart = { ...activePart, entityId: "part-other", partNumber: "OTHER-001", name: "其他实例零件" };
const otherProduct = { ...activeProduct, entityId: "product-other", name: "另一个防盗窗" };

const view = {
  scene: { tubeDesigner: {
    product: activeProduct,
    manufacturingGroups: [
      { productEntityId: "product-other", parts: [otherPart] },
      { productEntityId: activeProduct.entityId, parts: [activePart] },
    ],
  } },
};

const dock = renderDesignerProductPartsDock({}, view);
assert.match(dock, /实例零件清单/);
assert.match(dock, /当前防盗窗 · 1 种零件 · 共 1 件/);
assert.match(dock, /TD-001/);
assert.doesNotMatch(dock, /OTHER-001/);
assert.match(dock, /data-cam-action="tube-designer-select-product-part"[^>]*data-tube-designer-part-id="part-active"/);
assert.doesNotMatch(dock, /tube-designer-toggle-product-parts-dock|收起|<th>操作<\/th>/);
assert.doesNotMatch(dock, /预估加工规划|模板预估/);

const mergedDock = renderDesignerProductPartsDock({}, {
  ...view,
  scene: { tubeDesigner: {
    ...view.scene.tubeDesigner,
    product: { ...activeProduct, quantity: 3 },
    manufacturingGroups: [
      view.scene.tubeDesigner.manufacturingGroups[0],
      { productEntityId: activeProduct.entityId, parts: [
        { ...activePart, quantity: 6, unitQuantity: 2, instanceQuantity: 3 },
        { ...activePart, entityId: "part-merged", index: 2, quantity: 9, unitQuantity: 3, instanceQuantity: 3 },
      ] },
    ],
  } },
});
assert.match(mergedDock, /当前防盗窗 · 2 种零件 · 共 15 件/);
assert.doesNotMatch(mergedDock, /OTHER-001/);

const switchedDock = renderDesignerProductPartsDock({}, {
  ...view,
  scene: { tubeDesigner: { ...view.scene.tubeDesigner, product: otherProduct, activeProductId: otherProduct.entityId } },
});
assert.match(switchedDock, /OTHER-001/);
assert.match(switchedDock, /另一个防盗窗 · 1 种零件 · 共 1 件/);
assert.doesNotMatch(switchedDock, /TD-001/);

const empty = renderDesignerProductPartsDock({}, { scene: { tubeDesigner: { product: activeProduct, manufacturingGroups: [] } } });
assert.match(empty, /生成零件清单/);

const selected = renderDesignerProductPartsDock({}, { ...view, tubeDesignerActivePartId: "part-active" });
assert.match(selected, /<tr class="is-active"[^>]*aria-selected="true"/);
const selectedFromScene = renderDesignerProductPartsDock({}, { ...view, tubeDesignerActiveProductPartIds: ["part-active"] });
assert.match(selectedFromScene, /<tr class="is-active"[^>]*aria-selected="true"/);

for (const outdated of [{ modelOutdated: true }, { partsOutdated: true }]) {
  const stale = renderDesignerProductPartsDock({}, { ...view,
    scene: { tubeDesigner: { ...view.scene.tubeDesigner, product: { ...activeProduct, ...outdated } } },
  });
  assert.match(stale, /零件清单已过期，是否重新生成/);
  assert.match(stale, /product-parts-dock is-outdated/);
}
assert.match(renderDesignerProductPartsDock({}, { ...view, tubeDesignerRightDraftDirty: true }),
  /零件清单已过期，是否重新生成/);

console.log("Product instance parts dock uses only the active instance's realised parts.");
