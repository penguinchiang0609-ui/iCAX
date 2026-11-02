# Three.js line addons

LineSegments2.js, LineSegmentsGeometry.js and LineMaterial.js are from
`three@0.185.1/examples/jsm/lines/`, matching the existing runtime.
Source: https://cdn.jsdelivr.net/npm/three@0.185.1/examples/jsm/lines/

Only the `three` import specifier is changed to `../three/three.module.js`.
MIT license: the viewer publishes the existing Three.js LICENSE in `three/`.
These addons provide real screen-space line widths; WebGL native line width
is not relied on for the thick Skin boundary style.
