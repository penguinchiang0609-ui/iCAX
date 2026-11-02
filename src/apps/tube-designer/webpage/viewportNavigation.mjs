// Reuse the shared beveled cube, compass and world-axis triad used by Laser3D.
// These options only affect camera navigation; model and editing state stay owned
// by the existing workbench or preview controller.
export const tubeDesignerViewportNavigation = Object.freeze({
  showProjectionToggle: true,
  showOrientationGizmo: false,
  viewCubeAppearance: Object.freeze({
    beveled: true,
    compass: true,
    triad: true,
    faceLabels: Object.freeze({ front: "前视", back: "后视", left: "左视", right: "右视", top: "顶视", bottom: "底视" }),
  }),
});
