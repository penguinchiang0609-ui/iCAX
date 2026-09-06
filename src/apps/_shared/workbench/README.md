# Shared workbench

Product pages must not import another product directory.

- `entry.mjs` composes the neutral workbench for TubeDesigner. It has no default CAM scene queries and does not import CAM panels/actions.
- `camWorkbench.mjs` opts into reusable machine, workpiece and machining capabilities for TubeOne and Laser3DCAM.
- `createWorkbench.mjs` owns the existing layout, view lifecycle, selection and command dispatch. Features are injected per workbench instance; product context callbacks still own their pages.
- State, formatting, view cube and styles are shared. Existing CSS classes and page layout are intentionally preserved.
- The automation global `__icaxLaser3DCAM` is retained as a compatibility name for existing development scripts; it does not load the Laser3DCAM product.

Native CAM capabilities are built as `CamRuntime.dll`. The C++ source project remains named `iCAX-Plugins/cam/Laser3DCAM` for source/build compatibility; it is consumed as a shared capability, not by starting the Laser3DCAM product.

`SceneBootstrap.dll` owns `CSceneBootstrapComponent` / `CSceneBootstrapBehaviour`. CAM-specific initialization owns the distinct `CCamSceneBootstrapComponent` / `CCamSceneBootstrapBehaviour` and adds CAM root and selection data. CAM manifests also register the generic marker so older scene data remains recognizable.

Checks:

- `node src/tests/iCAX-UI/ProductIsolationTest.mjs`
- `node src/tests/iCAX-UI/SDKTest.mjs`
- After building the native products and ProductTest: `ProductTest.exe --gtest_filter=ProductRuntimeIntegrationTest.* --gtest_also_run_disabled_tests`. This opens memory-only projects using all three real manifests and checks simultaneous product-scoped registration; it does not use interactive projects.
