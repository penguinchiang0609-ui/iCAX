# CAD import fixtures

`block-contours.dxf` is a small independently authored ASCII drawing with a
block containing a rectangle, circle, ellipse and cubic spline, inserted at
(24, 85) mm. Annotation text is deliberately separate from the cutting curves.
`block-contours-binary.dxf` has the same records, encoded independently as
binary DXF. The production reader is not used to construct either source.

`block-contours.dwg` is a real AC1015 (DWG 2000) drawing. Its complete ownership
and handle tables are authored with test-only ezdxf 1.4.2, then written with
the pinned upstream LibreDWG 0.14.8601 `dxf2dwg` tool. The geometry is checked
against the independent minimal DXF source; successful conversion alone does
not establish that the contours survived. Tools/authoring caches are excluded
from runtime deployment.

To regenerate the two DXF fixtures, run `node createFixtures.mjs`. To regenerate
the DWG too, set `ICAX_DXF2DWG` to that pinned test writer,
`ICAX_FIXTURE_PYTHON` to Python 3.12 with ezdxf installed, and optionally
`ICAX_FIXTURE_PYTHON_PACKAGES` to the test package directory before running the
same script. Complete authoring intermediates go under `output/tests`.

`libredwg-samples.json` pins 15 independently authored upstream examples for
DWG 2000, 2004, 2007, 2013 and 2018. Tests verify their SHA-256, curve types and
inch-to-mm conversion. Download the test inputs with
`src/tools/testing/prepare_sketch_cad_import_samples.ps1`; none are application
runtime inputs. Original source, provenance and licensing remain in the
pinned LibreDWG project/release.
