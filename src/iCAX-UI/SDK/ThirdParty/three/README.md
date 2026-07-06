# Three.js

本目录内置 Three.js，用于 H5 `ThreeRenderViewport`。

- Version: `0.185.1` / r185
- Source package: `three@0.185.1`
- Entry: `three.module.js`
- Runtime files: `three.module.js`, `three.core.js`
- License: MIT, see `LICENSE`

产品页面不直接 import 本目录。需要三维视口时，从 `iCAX-UI/SDK/index.mjs` 使用 `createThreeViewport()`。
