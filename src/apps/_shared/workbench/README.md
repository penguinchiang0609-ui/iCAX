# Shared workbench

Product pages must not import another product directory.

- `entry.mjs` composes the neutral workbench for TubeDesigner. It has no default CAM scene queries and does not import CAM panels/actions.
- `camWorkbench.mjs` provides opt-in reusable machine, workpiece and machining capabilities. TubeDesigner owns its current tube workflow and opts into neutral services through its own scene commands.
- `createWorkbench.mjs` owns the existing layout, view lifecycle, selection and command dispatch. Features are injected per workbench instance; product context callbacks still own their pages.
- State, formatting, view cube and styles are shared. Existing CSS classes and page layout are intentionally preserved.
- `mapAreaViewSnapshot` can map product display coordinates before the viewport consumes a native snapshot. `onAreaViewSnapshotApplied` runs after geometry is applied, allowing a product to restore the camera for its display frame. These hooks do not modify native engineering transforms.
- `viewportOptions.viewCubeAppearance` optionally declares `colors` (center/edge/corner), `faceLabels` and `seamless`. Seamless rendering retains the 26 pick regions, draws complete face borders, and suppresses edge-on labels. Product CSS owns size, placement and hover colors; other products retain the default appearance.
- `viewCubeAppearance.beveled` draws a chamfered solid with directional lighting, gradients and face-aligned lettering. Its 26 face/edge/corner targets have larger transparent edge/corner hit polygons; hover/focus illuminates only the corresponding visible surface. Per-host SVG IDs isolate gradients/shadows across viewports. This changes presentation only, not view direction semantics or camera projections.
- The automation global `__icaxWorkbench` exposes the current shared workbench to development scripts.

Native CAM capabilities are built by `iCAX-Plugins/cam/CamRuntime/CamRuntime.vcxproj` as `CamRuntime.dll`. This is a shared capability module, not a standalone product. Machine components, loaders and constraints are owned by `iCAX-Plugins/cam/Machine`.

`SceneBootstrap.dll` owns `CSceneBootstrapComponent` / `CSceneBootstrapBehaviour`. CAM-specific initialization owns the distinct `CCamSceneBootstrapComponent` / `CCamSceneBootstrapBehaviour` and adds CAM root and selection data.

Checks:

- `node src/tests/iCAX-UI/ProductIsolationTest.mjs`
- `node src/tests/iCAX-UI/SDKTest.mjs`
- After building TubeDesigner and ProductTest: `ProductTest.exe --gtest_filter=ProductRuntimeIntegrationTest.* --gtest_also_run_disabled_tests`. This checks simultaneous product-scoped registration using the active manifest and isolated test product definitions; it does not use interactive projects.
