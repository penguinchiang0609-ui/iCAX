"""Author the independent DWG test source using ezdxf 1.4.2 (test-only)."""
import sys
from pathlib import Path

if len(sys.argv) > 2 and sys.argv[2]:
    sys.path.insert(0, sys.argv[2])
import ezdxf

output = Path(sys.argv[1])
output.mkdir(parents=True, exist_ok=True)
doc = ezdxf.new("R2000")
doc.units = 4
doc.layers.new("CUT")
block = doc.blocks.new("SHAPES")
block.add_lwpolyline([(0, 0), (8, 0), (8, 12), (0, 12)], close=True)
block.add_circle((16, 0), radius=3)
block.add_ellipse((28, 0), major_axis=(0, 5), ratio=0.6)
block.add_open_spline([(0, 24, 0), (4, 20, 0), (8, 29, 0), (12, 24, 0)], degree=3)
doc.modelspace().add_blockref("SHAPES", (24, 85), dxfattribs={"layer": "CUT"})
doc.modelspace().add_text("Imported cutting contours", dxfattribs={"insert": (10, 20), "height": 2})
assert not doc.audit().has_errors
doc.saveas(output / "dwg-source.dxf")
