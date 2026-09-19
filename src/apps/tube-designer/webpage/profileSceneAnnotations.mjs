import { resolveProductSpecificationAnnotations } from './productParameterDiagram.mjs';
import { matchesParameterCondition } from './parameterConditions.mjs';

// Section anchors are generated from the package display expressions. The
// shared viewport owns double-click, camera pass-through and editor lifecycle.
export function profileSpecificationAnchors(snapshot, descriptor) {
  const declarations = descriptor?.display?.views?.scene?.annotations ?? {};
  const scale = Math.max(1, Number(snapshot?.width ?? 0), Number(snapshot?.depth ?? 0));
  return (snapshot?.parameterDiagram?.annotations ?? []).flatMap((item, index) => {
    if (!declarations[item.parameter]) return [];
    const offset = scale * (0.18 + index * 0.012);
    const point = p => [p[0], p[1], 0];
    const from = item.from ?? item.point, to = item.to ?? (item.point && [item.point[0] + offset, item.point[1] + offset]);
    if (!from || !to) return [];
    const axis = item.axis === 'x' ? [0, item.side === 'top' ? offset : -offset, 0]
      : item.axis === 'y' ? [item.side === 'right' ? offset : -offset, 0, 0] : [0, 0, 0];
    const source = descriptor.display.views.section?.parameterDiagram?.annotations?.find(a => a.parameter === item.parameter
      && matchesParameterCondition(a.visibleWhen, snapshot.parameters ?? {}));
    return [{ id: `profile.${item.parameter}.${index}`, ...item, visibleWhen: source?.visibleWhen, start: point(from), end: point(to), offset: axis,
      generatedValue: snapshot.parameters?.[item.parameter] }];
  });
}

export function resolveProfileSpecificationAnnotations(designer, view) {
  return resolveProductSpecificationAnnotations(designer, view)
    .filter(annotation => view.tubeDesignerSpecificationAnnotationsVisible !== false
      && view.tubeDesignerSpecificationAnnotationGroupVisibility?.[annotation.groupKey] !== false);
}
