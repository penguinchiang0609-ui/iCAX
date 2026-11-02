# Three.js

本目录内置 Three.js，用于 H5 `ThreeRenderViewport`。

- Version: `0.185.1` / r185
- Source package: `three@0.185.1`
- Entry: `three.module.js`
- Runtime files: `three.module.js`, `three.core.js`
- Addon: `TransformControls.js`, from the same `three@0.185.1` package (`examples/jsm/controls/TransformControls.js`). Source: https://cdn.jsdelivr.net/npm/three@0.185.1/examples/jsm/controls/TransformControls.js . The only vendor change is the import specifier from `three` to `./three.module.js`.
- License: MIT, see `LICENSE`

产品页面不直接 import 本目录。需要三维视口时，从 `iCAX-UI/SDK/index.mjs` 使用 `createThreeViewport()`。
