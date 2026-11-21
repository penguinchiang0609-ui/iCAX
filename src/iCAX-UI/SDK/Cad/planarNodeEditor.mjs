export class PlanarNodeEditor {
  constructor(geometry) {
    this.geometry = geometry;
    const curve = geometry?.curves?.length === 1 ? geometry.curves[0] : null;
    this.points = curve && ["line", "polyline"].includes(curve.type) ? structuredClone(curve.points) : null;
  }
  get supported() { return Boolean(this.points?.length); }
  request(values) {
    if (!this.supported) throw new Error("请选择单一直线或多段线编辑节点。");
    const points = this.points.map((point, index) => point.map((value, axis) => {
      const text = String(values[`point${index}${axis ? "y" : "x"}`] ?? value).trim();
      return text ? Number(text) : NaN;
    }));
    if (!points.every(point => point.every(Number.isFinite))) throw new Error("节点坐标必须为有限数值。");
    return { operation: "nodes", points, expectedGeometryRef: { ...this.geometry.geometryRef } };
  }
}
