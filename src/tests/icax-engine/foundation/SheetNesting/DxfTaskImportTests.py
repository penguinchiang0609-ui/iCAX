"""Input-fidelity and rejection checks for the native-task DXF adapter."""
import importlib.util
from fractions import Fraction
import hashlib
import json
import math
from pathlib import Path
import struct
import tempfile
import unittest
from unittest import mock

ROOT = Path(__file__).resolve().parents[4]
spec = importlib.util.spec_from_file_location("dxf_task", ROOT / "tools/sheet-nesting/dxf_task.py")
adapter = importlib.util.module_from_spec(spec)
spec.loader.exec_module(adapter)


def drawing(entities):
    return "0\nSECTION\n2\nENTITIES\n" + entities + "0\nENDSEC\n0\nEOF\n"


def polyline(points, flags=1):
    result = f"0\nPOLYLINE\n70\n{flags}\n"
    for x, y in points:
        result += f"0\nVERTEX\n10\n{x}\n20\n{y}\n30\n0\n"
    return result + "0\nSEQEND\n"


def line(a, b):
    return f"0\nLINE\n10\n{a[0]}\n20\n{a[1]}\n30\n0\n11\n{b[0]}\n21\n{b[1]}\n31\n0\n"


def circle(center, radius):
    return f"0\nCIRCLE\n10\n{center[0]}\n20\n{center[1]}\n30\n0\n40\n{radius}\n"


def arc(center, radius, start, end):
    return f"0\nARC\n10\n{center[0]}\n20\n{center[1]}\n30\n0\n40\n{radius}\n50\n{start}\n51\n{end}\n"


def bulge_polyline(points, bulges, lightweight=True):
    if lightweight:
        return (f"0\nLWPOLYLINE\n70\n1\n90\n{len(points)}\n" +
                "".join(f"10\n{x}\n20\n{y}\n42\n{bulge}\n" for (x, y), bulge in zip(points, bulges)))
    return ("0\nPOLYLINE\n70\n1\n" +
            "".join(f"0\nVERTEX\n10\n{x}\n20\n{y}\n30\n0\n42\n{bulge}\n" for (x, y), bulge in zip(points, bulges)) +
            "0\nSEQEND\n")


def spline(points, degree, knots, weights=None, flags=8):
    result = (f"0\nSPLINE\n70\n{flags}\n71\n{degree}\n72\n{len(knots)}\n"
              f"73\n{len(points)}\n74\n0\n")
    result += "".join(f"40\n{value}\n" for value in knots)
    if weights is not None:
        result += "".join(f"41\n{value}\n" for value in weights)
    result += "".join(f"10\n{x}\n20\n{y}\n30\n0\n" for x, y in points)
    return result


def ellipse(center, major, ratio, start=0, end=math.tau):
    return (f"0\nELLIPSE\n10\n{center[0]}\n20\n{center[1]}\n30\n0\n"
            f"11\n{major[0]}\n21\n{major[1]}\n31\n0\n40\n{ratio}\n41\n{start}\n42\n{end}\n")


def block(name, entities, base=(0, 0)):
    return f"0\nBLOCK\n2\n{name}\n70\n0\n10\n{base[0]}\n20\n{base[1]}\n30\n0\n" + entities + "0\nENDBLK\n"


def insert(name, at=(0, 0), scales=(1, 1), rotation=0, columns=1, rows=1, spacing=(0, 0)):
    return (f"0\nINSERT\n2\n{name}\n10\n{at[0]}\n20\n{at[1]}\n30\n0\n"
            f"41\n{scales[0]}\n42\n{scales[1]}\n50\n{rotation}\n70\n{columns}\n71\n{rows}\n"
            f"44\n{spacing[0]}\n45\n{spacing[1]}\n")


def block_drawing(blocks, entities):
    return "0\nSECTION\n2\nBLOCKS\n" + blocks + "0\nENDSEC\n" + drawing(entities)


def ole_frame(payload=b"embedded-object", raw_hex=None):
    encoded = payload.hex() if raw_hex is None else raw_hex
    return ("0\nOLE2FRAME\n5\nABC\n8\nembedded-note\n70\n2\n71\n2\n72\n0\n73\n2\n"
            "10\n10000\n20\n10000\n30\n0\n11\n11000\n21\n9000\n31\n0\n"
            f"90\n{len(payload)}\n310\n{encoded}\n")


def infinite_construction(kind):
    return f"0\n{kind}\n5\nINFINITE\n8\nconstruction\n10\n10000\n20\n10000\n30\n0\n11\n1\n21\n0\n31\n0\n"


def hatch_lines(paths, style=0, normal=(0, 0, 1), polyline_boundary=False):
    text = (f"0\nHATCH\n5\nHATCH_HANDLE\n8\nfill\n10\n0\n20\n0\n30\n0\n"
            f"210\n{normal[0]}\n220\n{normal[1]}\n230\n{normal[2]}\n2\nSOLID\n70\n1\n71\n0\n91\n{len(paths)}\n")
    for points in paths:
        if polyline_boundary:
            text += f"92\n2\n72\n0\n73\n1\n93\n{len(points)}\n"
            text += "".join(f"10\n{x}\n20\n{y}\n" for x, y in points)
        else:
            text += f"92\n1\n93\n{len(points)}\n"
            for a, b in zip(points, points[1:] + points[:1]):
                text += f"72\n1\n10\n{a[0]}\n20\n{a[1]}\n11\n{b[0]}\n21\n{b[1]}\n"
        text += "97\n0\n"
    return text + f"75\n{style}\n76\n1\n98\n0\n"


def hatch_spline(points, degree, knots):
    return (f"0\nHATCH\n5\nHATCH_SPLINE\n8\nfill\n10\n0\n20\n0\n30\n0\n"
            f"2\nSOLID\n70\n1\n71\n0\n91\n1\n92\n1\n93\n1\n72\n4\n94\n{degree}\n73\n0\n74\n0\n"
            f"95\n{len(knots)}\n96\n{len(points)}\n" + "".join(f"40\n{k}\n" for k in knots) +
            "".join(f"10\n{x}\n20\n{y}\n" for x, y in points) + "97\n0\n75\n2\n76\n1\n98\n0\n")


def solid_triangle(points, layer="0"):
    points = [*points, points[-1]]
    return "0\nSOLID\n8\n" + layer + "\n" + "".join(
        f"{code}\n{x}\n{code+10}\n{y}\n{code+20}\n0\n" for code, (x, y) in zip((10, 11, 12, 13), points))


def dimension_fixture(edges=None, text_at=(50, 38)):
    edges = edges or [((40, 40), (60, 40)), ((40, 40), (40, 50)), ((60, 40), (60, 50))]
    lines = "".join(line(a, b) for a, b in edges)
    solids = [solid_triangle([(40, 40), (41, 39.8), (41, 40.2)]),
              solid_triangle([(60, 40), (59, 39.8), (59, 40.2)])]
    text = f"0\nMTEXT\n8\n0\n10\n{text_at[0]}\n20\n{text_at[1]}\n30\n0\n40\n2\n1\n10\n"
    points = "0\nPOINT\n10\n40\n20\n40\n0\nPOINT\n10\n60\n20\n40\n"
    content = lines + "".join(solids) + text
    definition = block("*D0", content + points).replace("70\n0\n", "70\n1\n", 1)
    return definition, content, lines, solids, text


def source_role_drawing(entities, blocks=""):
    tables = ("0\nSECTION\n2\nTABLES\n0\nTABLE\n2\nLTYPE\n70\n1\n"
              "0\nLTYPE\n2\nCENTERX2\n3\nCenter (2x)\n72\n65\n73\n4\n40\n20.32\n"
              "49\n12.7\n49\n-2.54\n49\n2.54\n49\n-2.54\n0\nENDTAB\n"
              "0\nTABLE\n2\nLAYER\n70\n2\n0\nLAYER\n2\n0\n70\n0\n6\nCONTINUOUS\n"
              "0\nLAYER\n2\nmarker\n70\n0\n6\nCENTERX2\n0\nENDTAB\n0\nENDSEC\n")
    return tables + (block_drawing(blocks, entities) if blocks else drawing(entities))


def on_layer(source, name):
    # Add an explicit source layer to one fixture entity; POLYLINE vertices
    # retain their own zero layer as they do in ordinary DXF files.
    first = source.index("\n", source.index("\n") + 1) + 1
    return source[:first] + f"8\n{name}\n" + source[first:]


def bspline_point(points, degree, knots, parameter):
    # Independent Cox-de Boor basis evaluation, rather than the importer's
    # knot insertion / rational Bezier subdivision algorithm.
    if parameter == knots[-1]:
        return points[-1]

    def basis(index, order):
        if order == 0:
            return float(knots[index] <= parameter < knots[index + 1])
        first = knots[index + order] - knots[index]
        second = knots[index + order + 1] - knots[index + 1]
        return (((parameter - knots[index]) / first * basis(index, order - 1) if first else 0) +
                ((knots[index + order + 1] - parameter) / second * basis(index + 1, order - 1) if second else 0))

    return tuple(sum(point[axis] * basis(index, degree) for index, point in enumerate(points))
                 for axis in (0, 1))


def contains_point(ring, point):
    """Independent floating-point ray crossing for well-separated samples."""
    inside = False
    for a, b in zip(ring, ring[1:] + ring[:1]):
        if (a[1] > point[1]) != (b[1] > point[1]):
            x = a[0] + (point[1] - a[1]) * (b[0] - a[0]) / (b[1] - a[1])
            if point[0] < x:
                inside = not inside
    return inside


def area(points):
    return abs(sum(a[0] * b[1] - a[1] * b[0]
                   for a, b in zip(points, points[1:] + points[:1]))) / 2


class DxfTaskImportTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.folder = Path(self.temp.name)

    def tearDown(self):
        self.temp.cleanup()

    def file(self, content, name="part.dxf"):
        path = self.folder / name
        path.write_text(content, encoding="utf8")
        return path

    def test_original_coordinates_and_vertex_order_are_preserved(self):
        points = [[-3.25, 8.5], [6.75, 8.5], [1.75, 12.5]]
        path = self.file(drawing(polyline(points, 129)))
        self.assertEqual(adapter.straight_contour(path), points)

    def test_every_bulge_is_checked_not_only_the_last(self):
        entity = "0\nLWPOLYLINE\n70\n1\n90\n3\n10\n0\n20\n0\n42\n1\n10\n4\n20\n0\n42\n0\n10\n0\n20\n3\n42\n0\n"
        with self.assertRaisesRegex(ValueError, "bulge"):
            adapter.straight_contour(self.file(drawing(entity)))

    def test_open_and_spatial_contours_are_rejected(self):
        for flags in (0, 9):
            with self.subTest(flags=flags), self.assertRaises(ValueError):
                adapter.straight_contour(self.file(drawing(polyline([[0, 0], [4, 0], [0, 3]], flags))))

    def test_extra_material_is_not_silently_dropped(self):
        one = polyline([[0, 0], [4, 0], [0, 3]])
        with self.assertRaisesRegex(ValueError, "exactly one"):
            adapter.straight_contour(self.file(drawing(one + one)))
        with self.assertRaisesRegex(ValueError, "Unsupported DXF entity"):
            adapter.straight_contour(self.file(drawing(one + "0\nLINE\n10\n0\n20\n0\n11\n1\n21\n1\n")))

    def test_each_manifest_supplies_its_own_demand_and_stock(self):
        path = self.file(drawing(polyline([[0, 0], [4, 0], [0, 3]])))
        task = dict(units="mm", parts=[dict(id="任意编号", file=path.name, quantity=3,
                    material="steel", allowedAngles=[90])],
                    stocks=[dict(id="another-stock", width=12.4, height=6.4, quantity=2, cost=1, material="steel")],
                    rules=dict(partGap=.2, edgeMargin=.2, allowCommonEdges=True))
        manifest = self.file(json.dumps(task), "task.json")
        first, sources = adapter.prepare(manifest)
        self.assertEqual(first["parts"][0]["quantity"], 3)
        self.assertEqual(first["parts"][0]["id"], "任意编号")
        self.assertEqual(first["stocks"], task["stocks"])
        self.assertEqual(first["parts"][0]["allowedAngles"], [90])
        self.assertEqual(len(sources), 1)
        task["parts"][0]["quantity"] = 7
        task["stocks"][0]["width"] = 30.4
        manifest.write_text(json.dumps(task), encoding="utf8")
        second, _ = adapter.prepare(manifest)
        self.assertEqual(second["parts"][0]["quantity"], 7)
        self.assertEqual(second["stocks"][0]["width"], 30.4)
        self.assertEqual(first["parts"][0]["quantity"], 3)

    def test_circle_radius_is_not_polyline_width_in_the_strict_legacy_reader(self):
        with self.assertRaisesRegex(ValueError, "Unsupported DXF entity CIRCLE"):
            adapter.straight_contour(self.file(drawing(circle((0, 0), 10))))

    def test_shuffled_lines_form_an_exact_outer_and_circle_remains_a_conservative_hole(self):
        vertices = [(-20, -20), (20, -20), (20, 20), (-20, 20)]
        entities = (circle((0, 0), 10) + line(vertices[1], vertices[0]) + line(vertices[2], vertices[3])
                    + line(vertices[0], vertices[3]) + line(vertices[2], vertices[1]))
        imported = adapter.import_shape(self.file(drawing(entities)), tolerance_mm=.01)
        geometry, metadata = imported["geometry"], imported["metadata"]
        self.assertEqual(set(map(tuple, geometry["outer"])), set(vertices))
        self.assertEqual(geometry["commonEdgeRings"], [True, False])
        self.assertEqual(len(geometry["holes"]), 1)
        self.assertTrue(metadata["exactOuter"])
        self.assertFalse(metadata["exact"])
        self.assertEqual(metadata["circles"][0]["radius"], 10)
        self.assertEqual(metadata["circles"][0]["representation"], "inscribed-hole")
        hole = geometry["holes"][0]
        for x, y in hole:
            self.assertLessEqual(Fraction(x) ** 2 + Fraction(y) ** 2, 100)
        self.assertLess(area(hole), math.pi * 100)
        # Every chord leaves at most the requested radial safety band. This
        # independently checks the actual represented vertices supplied downstream.
        for a, b in zip(hole, hole[1:] + hole[:1]):
            distance = abs(a[0] * b[1] - a[1] * b[0]) / math.dist(a, b)
            self.assertGreaterEqual(distance, 10 - .01)

    def test_annotations_do_not_become_material_and_native_circle_is_exact(self):
        annotation = "0\nTEXT\n10\n9999\n20\n9999\n40\n200\n1\nDO NOT CUT\n"
        imported = adapter.import_shape(self.file(drawing(annotation + circle((3, -4), 2.5))), scale_to_mm=2)
        self.assertEqual(imported["geometry"]["radius"], 5)
        self.assertEqual(imported["geometry"]["center"], [6, -8])
        self.assertEqual(imported["geometry"]["outer"], [])
        self.assertTrue(imported["metadata"]["exact"])
        self.assertEqual(imported["metadata"]["ignoredAnnotationEntities"], {"TEXT": 1})

    def test_nested_material_islands_and_independent_components_are_all_retained(self):
        outer = [[0, 0], [20, 0], [20, 20], [0, 20]]
        hole = [[2, 2], [18, 2], [18, 18], [2, 18]]
        island = [[5, 5], [7, 5], [7, 7], [5, 7]]
        other = [[30, 0], [34, 0], [34, 3], [30, 3]]
        imported = adapter.import_shape(self.file(drawing(polyline(hole) + polyline(other)
                                                         + polyline(island) + polyline(outer))))
        self.assertIsNone(imported["geometry"])
        self.assertEqual(len(imported["geometries"]), 3)
        self.assertEqual(imported["metadata"]["materialComponents"], 3)
        frame = next(shape for shape in imported["geometries"] if len(shape["holes"]) == 1)
        self.assertEqual(frame["outer"], outer)
        self.assertEqual(frame["holes"], [hole])
        self.assertEqual(frame["commonEdgeRings"], [True, True])
        self.assertEqual(sum(area(shape["outer"]) - sum(map(area, shape["holes"]))
                             for shape in imported["geometries"]), 160)

    def test_open_branching_and_intersecting_material_is_not_silently_repaired(self):
        open_chain = line((0, 0), (5, 0)) + line((5, 0), (5, 5))
        with self.assertRaisesRegex(ValueError, "Open or branching LINE chain"):
            adapter.import_shape(self.file(drawing(open_chain)))
        first = polyline([[0, 0], [5, 0], [5, 5], [0, 5]])
        overlapping = polyline([[4, 1], [6, 1], [6, 2], [4, 2]])
        with self.assertRaisesRegex(ValueError, "intersect/touch"):
            adapter.import_shape(self.file(drawing(first + overlapping)))

    def test_open_arc_is_reported_without_returning_partial_material(self):
        entity = arc((0, 0), 5, 0, 180)
        with self.assertRaisesRegex(ValueError, "Open or branching curved"):
            adapter.import_shape(self.file(drawing(circle((0, 0), 10) + entity)))

    def test_circular_outer_with_hole_uses_an_explicit_conservative_material_bound(self):
        imported = adapter.import_shape(self.file(drawing(circle((0, 0), 10) + circle((0, 0), 3))))
        shape = imported["geometry"]
        self.assertEqual(shape["commonEdgeRings"], [False, False])
        self.assertFalse(imported["metadata"]["exactOuter"])
        self.assertGreater(area(shape["outer"]), math.pi * 100)
        self.assertLess(area(shape["holes"][0]), math.pi * 9)
        for x, y in shape["outer"]:
            self.assertLessEqual(Fraction(x) ** 2 + Fraction(y) ** 2, Fraction(10.01) ** 2)
        for a, b in zip(shape["outer"], shape["outer"][1:] + shape["outer"][:1]):
            self.assertGreaterEqual(abs(a[0] * b[1] - a[1] * b[0]) / math.dist(a, b), 10)

    def test_existing_binary_reader_is_reused_for_identical_material(self):
        points = [[0, 0], [10, 0], [10, 6], [0, 6]]
        pairs = [(0, "SECTION"), (2, "ENTITIES")]
        for a, b in zip(points, points[1:] + points[:1]):
            pairs += [(0, "LINE"), (10, a[0]), (20, a[1]), (11, b[0]), (21, b[1])]
        pairs += [(0, "ENDSEC"), (0, "EOF")]
        data = bytearray(b"AutoCAD Binary DXF\r\n\x1a\0")
        for code, value in pairs:
            data += struct.pack("<H", code)
            data += struct.pack("<d", value) if code in (10, 20, 11, 21) else str(value).encode() + b"\0"
        binary = self.folder / "binary.dxf"
        binary.write_bytes(data)
        imported = adapter.import_shape(binary)
        self.assertEqual(imported["metadata"]["sourceFormat"], "binary")
        self.assertEqual(imported["geometry"]["outer"], points)
        self.assertTrue(imported["metadata"]["exact"])

    def test_real_b2_retains_its_four_source_lines_and_actual_circle_hole(self):
        path = ROOT.parent / "TestData/Plane/DXF/B2__a2a49581d38136d9.dxf"
        before = hashlib.sha256(path.read_bytes()).hexdigest()
        imported = adapter.import_shape(path)
        self.assertEqual(before, "a2a49581d38136d9832509a82ec245fe7b1920038d5d7bcb304dbeea33f0ec4e")
        self.assertEqual(before, hashlib.sha256(path.read_bytes()).hexdigest())
        shape = imported["geometry"]
        self.assertEqual(set(map(tuple, shape["outer"])), {(-20, -20), (20, -20), (20, 20), (-20, 20)})
        self.assertEqual(shape["commonEdgeRings"], [True, False])
        self.assertEqual(len(shape["holes"]), 1)
        self.assertLess(len(shape["holes"][0]), 200)
        self.assertEqual(imported["metadata"]["circles"][0]["radius"], 9.999999999999982)
        self.assertEqual(imported["metadata"]["circles"][0]["center"], [0, 0])
        self.assertEqual(imported["metadata"]["curveToleranceMm"], .01)

    def test_arc_outer_contains_analytic_boundary_within_the_recorded_import_budget(self):
        source = arc((0, 0), 10, 0, 180) + line((-10, 0), (10, 0))
        imported = adapter.import_shape(self.file(drawing(source)))
        shape, metadata = imported["geometry"], imported["metadata"]
        self.assertEqual(shape["commonEdgeRings"], [False])
        self.assertFalse(metadata["exactOuter"])
        self.assertLessEqual(metadata["computedDeviationBudgetMm"], .01)
        self.assertFalse(metadata["materialSupersetExactCertificate"])
        self.assertEqual(metadata["arcs"][0]["startDegrees"], 0)
        self.assertEqual(metadata["arcs"][0]["endDegrees"], 180)
        for i in range(721):
            angle = math.pi * i / 720
            self.assertTrue(contains_point(shape["outer"], (10 * math.cos(angle), 10 * math.sin(angle))))
        self.assertGreater(area(shape["outer"]), math.pi * 100 / 2)
        self.assertTrue(all(-10.01 <= x <= 10.01 and -.01 <= y <= 10.01 for x, y in shape["outer"]))

    def test_arc_hole_is_preserved_and_does_not_disable_exact_outer_common_edges(self):
        outer = [[-20, -20], [20, -20], [20, 20], [-20, 20]]
        source = polyline(outer) + arc((0, 0), 10, 0, 180) + arc((0, 0), 10, 180, 360)
        imported = adapter.import_shape(self.file(drawing(source)))
        shape = imported["geometry"]
        self.assertEqual(shape["outer"], outer)
        self.assertEqual(len(shape["holes"]), 1)
        self.assertEqual(shape["commonEdgeRings"], [True, False])
        self.assertTrue(imported["metadata"]["exactOuter"])
        self.assertLess(area(shape["holes"][0]), math.pi * 100)
        for point in shape["holes"][0]:
            self.assertLess(math.hypot(*point), 10)
        for i in range(360):
            angle = 2 * math.pi * i / 360
            self.assertTrue(contains_point(shape["holes"][0], (9.99 * math.cos(angle), 9.99 * math.sin(angle))))

    def test_positive_and_negative_bulges_retain_their_source_sweep_in_both_polyline_forms(self):
        for lightweight in (True, False):
            for bulge in (1, -1):
                with self.subTest(lightweight=lightweight, bulge=bulge):
                    source = bulge_polyline([[-5, 0], [5, 0]], [bulge, 0], lightweight)
                    imported = adapter.import_shape(self.file(drawing(source)))
                    shape, metadata = imported["geometry"], imported["metadata"]
                    self.assertEqual(metadata["arcs"][0]["bulge"], bulge)
                    self.assertAlmostEqual(metadata["arcs"][0]["sweepRadians"], bulge * math.pi)
                    self.assertTrue(contains_point(shape["outer"], (0, -2.5 * bulge)))
                    self.assertFalse(contains_point(shape["outer"], (0, 2.5 * bulge)))
                    self.assertGreater(area(shape["outer"]), math.pi * 25 / 2)
                    self.assertEqual(shape["commonEdgeRings"], [False])

    def test_arc_chain_records_tiny_endpoint_connections_and_rejects_larger_gaps(self):
        source = arc((0, 0), 10, 0, 180) + line((-10, 5e-8), (10, 0))
        imported = adapter.import_shape(self.file(drawing(source)))
        self.assertGreater(imported["metadata"]["maxConnectionErrorMm"], 4.9e-8)
        self.assertLessEqual(imported["metadata"]["maxConnectionErrorMm"], 1e-7)
        source = arc((0, 0), 10, 0, 180) + line((-10, 2e-7), (10, 0))
        with self.assertRaisesRegex(ValueError, "Open or branching curved"):
            adapter.import_shape(self.file(drawing(source)))

    def test_invalid_curved_self_intersection_is_not_fixed_with_buffer_zero(self):
        source = arc((0, 0), 10, 0, 180) + line((-10, 0), (10, 10)) + line((10, 10), (10, 0))
        with self.assertRaisesRegex(ValueError, "Invalid sampled curved ring"):
            adapter.import_shape(self.file(drawing(source)))

    def test_real_paysan_arc_part_retains_all_closed_contours_and_curve_parameters(self):
        folder = ROOT.parent / "TestData/Plane/paysan_stage1_53_unique_dxf_license_mapping_pending/files/1005"
        path = next(folder.glob("*.DXF"))
        before = hashlib.sha256(path.read_bytes()).hexdigest()
        imported = adapter.import_shape(path)
        self.assertEqual(before, hashlib.sha256(path.read_bytes()).hexdigest())
        self.assertEqual(imported["metadata"]["materialComponents"], 1)
        self.assertEqual(imported["metadata"]["closedContours"], 3)
        self.assertEqual(len(imported["geometry"]["holes"]), 2)
        self.assertEqual(len(imported["metadata"]["arcs"]), 24)
        self.assertEqual(len(imported["metadata"]["circles"]), 1)
        self.assertLessEqual(imported["metadata"]["computedDeviationBudgetMm"], .01)
        self.assertLessEqual(imported["metadata"]["maxConnectionErrorMm"], 1e-7)

    def test_paperspace_drawing_curves_and_inherited_polyline_vertices_are_not_material(self):
        outer = [[0, 0], [20, 0], [20, 20], [0, 20]]
        paper_arc = arc((100, 100), 30, 0, 180).replace("0\nARC\n", "0\nARC\n67\n1\n")
        paper_poly = polyline([[100, 0], [110, 0], [110, 10]]).replace("0\nPOLYLINE\n", "0\nPOLYLINE\n67\n1\n")
        imported = adapter.import_shape(self.file(drawing(polyline(outer) + paper_arc + paper_poly)))
        self.assertTrue(imported["metadata"]["exact"])
        self.assertEqual(imported["geometry"]["outer"], outer)
        self.assertEqual(imported["metadata"]["materialComponents"], 1)
        self.assertEqual(imported["metadata"]["ignoredAnnotationEntities"],
                         {"PAPERSPACE_ARC": 1, "PAPERSPACE_POLYLINE": 1,
                          "PAPERSPACE_VERTEX": 3, "PAPERSPACE_SEQEND": 1})

    def test_real_paysan_paperspace_copy_is_excluded_from_material_count(self):
        folder = ROOT.parent / "TestData/Plane/paysan_stage1_53_unique_dxf_license_mapping_pending/files/788"
        imported = adapter.import_shape(next(folder.glob("*.DXF")))
        self.assertEqual(imported["metadata"]["materialComponents"], 1)
        self.assertEqual(len(imported["geometry"]["holes"]), 1)
        self.assertEqual(imported["metadata"]["ignoredAnnotationEntities"],
                         {"PAPERSPACE_LINE": 8, "PAPERSPACE_ARC": 4, "PAPERSPACE_VIEWPORT": 1})

    def test_spline_bezier_subdivision_covers_an_independently_evaluated_multispan_curve(self):
        points = [(-5, 0), (-4, 5), (-1, 6), (1, 6), (4, 5), (5, 0)]
        knots = [0, 0, 0, 0, .3, .7, 1, 1, 1, 1]
        imported = adapter.import_shape(self.file(drawing(spline(points, 3, knots) + line(points[-1], points[0]))))
        shape, metadata = imported["geometry"], imported["metadata"]
        self.assertEqual(len(metadata["splines"]), 1)
        self.assertEqual(metadata["splines"][0]["knots"], knots)
        self.assertEqual(metadata["splines"][0]["controlPoints"], list(map(list, points)))
        self.assertLessEqual(metadata["splines"][0]["maximumLeafHullDistanceMm"], .01 / 8)
        self.assertLessEqual(metadata["computedDeviationBudgetMm"], .01)
        for i in range(401):
            self.assertTrue(contains_point(shape["outer"], bspline_point(points, 3, knots, i / 400)))
        self.assertEqual(shape["commonEdgeRings"], [False])

    def test_positive_rational_spline_retains_the_source_quarter_circle(self):
        points, knots = [(1, 0), (1, 1), (0, 1)], [0, 0, 0, 1, 1, 1]
        weights = [1, math.sqrt(.5), 1]
        source = spline(points, 2, knots, weights, flags=12) + line((0, 1), (0, 0)) + line((0, 0), (1, 0))
        imported = adapter.import_shape(self.file(drawing(source)))
        self.assertEqual(imported["metadata"]["splines"][0]["weights"], weights)
        for i in range(361):
            angle = math.pi / 2 * i / 360
            self.assertTrue(contains_point(imported["geometry"]["outer"], (math.cos(angle), math.sin(angle))))
        self.assertGreater(area(imported["geometry"]["outer"]), math.pi / 4)

    def test_spline_with_nonpositive_weight_does_not_get_a_false_convex_hull_certificate(self):
        source = spline([(0, 0), (1, 1), (2, 0)], 2, [0, 0, 0, 1, 1, 1], [1, -1, 1], flags=12)
        with self.assertRaisesRegex(ValueError, "positive weight"):
            adapter.import_shape(self.file(drawing(source)))

    def test_proved_internal_zero_area_stroke_is_retained_without_changing_material(self):
        outer = [[0, 0], [20, 0], [20, 20], [0, 20]]
        source = polyline(outer) + line((5, 0), (5, 20))
        imported = adapter.import_shape(self.file(drawing(source)))
        self.assertTrue(imported["metadata"]["exact"])
        self.assertEqual(imported["geometry"]["outer"], outer)
        self.assertEqual(imported["geometry"]["commonEdgeRings"], [True])
        strokes = imported["metadata"]["nonBoundaryEntities"]
        self.assertEqual(len(strokes), 1)
        self.assertEqual(strokes[0]["points"], [[5, 0], [5, 20]])
        self.assertIn("process type not inferred", strokes[0]["classification"])

    def test_unclosed_stroke_outside_material_or_through_a_hole_is_not_discarded(self):
        outer = polyline([[-20, -20], [20, -20], [20, 20], [-20, 20]])
        for source in (outer + line((30, 0), (40, 0)),
                       outer + circle((0, 0), 5) + line((-20, 0), (20, 0))):
            with self.subTest(source=source), self.assertRaisesRegex(ValueError, "outside.*material|crosses a source hole"):
                adapter.import_shape(self.file(drawing(source)))

    def test_real_paysan_492_degree_one_splines_and_ten_holes_are_all_retained(self):
        path = next((ROOT.parent / "TestData/Plane/paysan_stage1_53_unique_dxf_license_mapping_pending/files/492").glob("*.DXF"))
        before = hashlib.sha256(path.read_bytes()).hexdigest()
        imported = adapter.import_shape(path)
        self.assertEqual(before, hashlib.sha256(path.read_bytes()).hexdigest())
        self.assertEqual(len(imported["geometry"]["holes"]), 10)
        self.assertEqual(imported["metadata"]["materialComponents"], 1)
        self.assertEqual(len(imported["metadata"]["splines"]), 2)
        for curve in imported["metadata"]["splines"]:
            self.assertEqual(curve["degree"], 1)
            self.assertEqual(curve["knots"], [.65, .65, .975, .975])
            self.assertEqual(curve["representation"], "exact-degree-one-edges")

    def test_real_paysan_internal_strokes_have_material_coverage_evidence(self):
        for number, holes, strokes in (("1244", 0, 1), ("1248", 4, 1), ("1249", 4, 1), ("923", 13, 6)):
            path = next((ROOT.parent / f"TestData/Plane/paysan_stage1_53_unique_dxf_license_mapping_pending/files/{number}").glob("*.DXF"))
            before = hashlib.sha256(path.read_bytes()).hexdigest()
            with self.subTest(number=number):
                imported = adapter.import_shape(path)
                self.assertEqual(before, hashlib.sha256(path.read_bytes()).hexdigest())
                self.assertEqual(len(imported["geometries"]), 1)
                self.assertEqual(len(imported["geometry"]["holes"]), holes)
                self.assertEqual(len(imported["metadata"]["nonBoundaryEntities"]), strokes)
                for stroke in imported["metadata"]["nonBoundaryEntities"]:
                    self.assertIn("no passage through holes", stroke["topologyEvidence"])
                if number == "1244":
                    self.assertTrue(imported["metadata"]["exact"])
                    self.assertEqual(len(imported["geometry"]["outer"]), 4)

    def test_ellipse_hole_preserves_its_source_parameters_and_exact_outer(self):
        outer = [[-20, -20], [20, -20], [20, 20], [-20, 20]]
        imported = adapter.import_shape(self.file(drawing(polyline(outer) + ellipse((0, 0), (10, 0), .4))))
        shape, metadata = imported["geometry"], imported["metadata"]
        self.assertEqual(shape["outer"], outer)
        self.assertEqual(shape["commonEdgeRings"], [True, False])
        self.assertEqual(metadata["ellipses"][0]["majorAxis"], [10, 0])
        self.assertEqual(metadata["ellipses"][0]["minorMajorRatio"], .4)
        self.assertLess(area(shape["holes"][0]), math.pi * 40)
        for x, y in shape["holes"][0]:
            self.assertLess((x / 10) ** 2 + (y / 4) ** 2, 1)
        self.assertLessEqual(metadata["computedDeviationBudgetMm"], .01)

    def test_rotated_ellipse_outer_and_partial_ellipse_arc_cover_analytic_boundaries(self):
        imported = adapter.import_shape(self.file(drawing(ellipse((3, -2), (6, 8), .5))))
        shape = imported["geometry"]
        for i in range(721):
            t = math.tau * i / 720
            point = (3 + 6 * math.cos(t) - 4 * math.sin(t), -2 + 8 * math.cos(t) + 3 * math.sin(t))
            self.assertTrue(contains_point(shape["outer"], point))
        self.assertGreater(area(shape["outer"]), math.pi * 50)
        source = ellipse((0, 0), (10, 0), .4, 0, math.pi) + line((-10, 0), (10, 0))
        imported = adapter.import_shape(self.file(drawing(source)))
        for i in range(361):
            t = math.pi * i / 360
            self.assertTrue(contains_point(imported["geometry"]["outer"], (10 * math.cos(t), 4 * math.sin(t))))

    def test_insert_applies_block_base_translation_rotation_and_nonuniform_line_scale_exactly(self):
        vertices = [[10, 20], [12, 20], [12, 21], [10, 21]]
        source = block_drawing(block("Part", polyline(vertices), base=(10, 20)),
                               insert("part", (100, 200), (2, 3), 90))
        imported = adapter.import_shape(self.file(source))
        self.assertEqual(set(map(tuple, imported["geometry"]["outer"])),
                         {(100, 200), (100, 204), (97, 204), (97, 200)})
        self.assertTrue(imported["metadata"]["exact"])
        self.assertEqual(imported["geometry"]["commonEdgeRings"], [True])
        self.assertEqual(imported["metadata"]["blockReferences"][0]["blockBasePoint"], [10, 20])

    def test_nested_mirrored_insert_arrays_keep_every_instance_and_unscaled_array_spacing(self):
        leaf = block("Leaf", polyline([[0, 0], [2, 0], [2, 1], [0, 1]]))
        middle = block("Middle", insert("Leaf", (10, 0), (-1, 1), columns=2, spacing=(5, 0)))
        source = block_drawing(leaf + middle, insert("Middle", (0, 20), (2, 2), 90))
        imported = adapter.import_shape(self.file(source))
        self.assertEqual(len(imported["geometries"]), 2)
        boundaries = {tuple(sorted(map(tuple, shape["outer"]))) for shape in imported["geometries"]}
        self.assertEqual(boundaries, {
            tuple(sorted([(0, 40), (0, 36), (-2, 36), (-2, 40)])),
            tuple(sorted([(0, 50), (0, 46), (-2, 46), (-2, 50)])),
        })
        self.assertEqual(len(imported["metadata"]["blockReferences"]), 3)

    def test_nonuniformly_scaled_block_circle_becomes_bounded_ellipse_not_a_fake_disk(self):
        source = block_drawing(block("Disk", circle((0, 0), 2)), insert("Disk", (10, -5), (3, 1)))
        imported = adapter.import_shape(self.file(source))
        shape, metadata = imported["geometry"], imported["metadata"]
        self.assertEqual(shape["radius"], 0)
        self.assertEqual(shape["commonEdgeRings"], [False])
        self.assertGreater(area(shape["outer"]), math.pi * 12)
        for i in range(721):
            t = math.tau * i / 720
            self.assertTrue(contains_point(shape["outer"], (10 + 6 * math.cos(t), -5 + 2 * math.sin(t))))
        self.assertEqual(metadata["circles"][0]["blockTransformToWorldMm"], [3, 0, 0, 1, 10, -5])
        self.assertLessEqual(metadata["computedDeviationBudgetMm"], .01)

    def test_missing_and_circular_block_references_are_not_silently_ignored(self):
        with self.assertRaisesRegex(ValueError, "Unresolved INSERT block"):
            adapter.import_shape(self.file(block_drawing("", insert("Missing"))))
        source = block_drawing(block("A", insert("B")) + block("B", insert("A")), insert("A"))
        with self.assertRaisesRegex(ValueError, "Circular.*INSERT"):
            adapter.import_shape(self.file(source))

    def test_real_lip_elliptical_holes_and_covapsy_blocks_are_all_retained(self):
        catalog = json.loads((ROOT.parent / "TestData/Plane/catalog/catalog.json").read_text(encoding="utf-8"))
        expected = (("Lip Inner.DXF", 2, 2, 0), ("Lip.DXF", 2, 2, 0),
                    ("Plaque trouée supérieure.dxf", 65, 0, 1), ("Plaque trouée.dxf", 344, 0, 1))
        for name, hole_count, ellipse_count, references in expected:
            asset = next(asset for asset in catalog["assets"] if asset["name"] == name)
            path = ROOT.parent / "TestData/Plane" / asset["relativePath"]
            before = hashlib.sha256(path.read_bytes()).hexdigest()
            with self.subTest(name=name):
                imported = adapter.import_shape(path)
                self.assertEqual(before, hashlib.sha256(path.read_bytes()).hexdigest())
                self.assertEqual(len(imported["geometries"]), 1)
                self.assertEqual(len(imported["geometry"]["holes"]), hole_count)
                self.assertEqual(len(imported["metadata"]["ellipses"]), ellipse_count)
                self.assertEqual(len(imported["metadata"]["blockReferences"]), references)

    def test_embedded_ole_keeps_payload_and_bounds_provenance_without_cutting_geometry(self):
        outer = [[0, 0], [20, 0], [20, 20], [0, 20]]
        payload = bytes(range(256))
        path = self.file(drawing(polyline(outer) + ole_frame(payload)))
        imported = adapter.import_shape(path)
        self.assertEqual(imported["geometry"]["outer"], outer)
        self.assertEqual(imported["metadata"]["materialComponents"], 1)
        self.assertTrue(imported["metadata"]["exact"])
        objects = imported["metadata"]["nonMaterialEntities"]
        self.assertEqual(len(objects), 1)
        entry = objects[0]
        self.assertEqual(entry["sourceType"], "OLE2FRAME")
        self.assertEqual(entry["sourceHandle"], "ABC")
        self.assertEqual(entry["sourceLayer"], "embedded-note")
        self.assertEqual(entry["payloadBytes"], len(payload))
        self.assertEqual(entry["payloadSha256"], hashlib.sha256(payload).hexdigest())
        self.assertEqual(entry["sourceDxfSha256"], hashlib.sha256(path.read_bytes()).hexdigest())
        self.assertTrue(entry["payloadLengthMatchesDeclared"])
        self.assertFalse(entry["boundsUsedAsMaterial"])
        self.assertEqual(entry["sourceBounds"]["firstCorner"], ["10000", "10000", "0"])

    def test_only_ole_has_no_material_and_preserves_provenance_on_the_actual_failure(self):
        path = self.file(drawing(ole_frame()))
        with self.assertRaisesRegex(ValueError, "no closed material") as captured:
            adapter.import_shape(path)
        objects = captured.exception.non_material_entities
        self.assertEqual(len(objects), 1)
        self.assertEqual(objects[0]["sourceType"], "OLE2FRAME")
        self.assertEqual(objects[0]["sourceDxfSha256"], hashlib.sha256(path.read_bytes()).hexdigest())

    def test_malformed_embedded_payload_is_explicit_without_corrupting_vector_material(self):
        outer = [[0, 0], [20, 0], [20, 20], [0, 20]]
        imported = adapter.import_shape(self.file(drawing(polyline(outer) + ole_frame(raw_hex="not-hex"))))
        self.assertEqual(imported["geometry"]["outer"], outer)
        entry = imported["metadata"]["nonMaterialEntities"][0]
        self.assertFalse(entry["payloadDecoded"])
        self.assertEqual(len(entry["payloadSourceTextSha256"]), 64)
        self.assertIn("invalid", entry["payloadIssue"])

    def test_infinite_construction_objects_are_retained_but_finite_named_lines_are_not_dropped(self):
        outer = [[0, 0], [20, 0], [20, 20], [0, 20]]
        source = polyline(outer) + infinite_construction("XLINE") + infinite_construction("RAY")
        imported = adapter.import_shape(self.file(drawing(source)))
        self.assertEqual(imported["geometry"]["outer"], outer)
        self.assertEqual({entry["sourceType"] for entry in imported["metadata"]["nonMaterialEntities"]}, {"XLINE", "RAY"})
        with self.assertRaisesRegex(ValueError, "no closed material"):
            adapter.import_shape(self.file(drawing(infinite_construction("XLINE"))))
        for name in ("CENTER", "CONSTRUCTION", "DASHED"):
            finite = line((30, 0), (40, 0)).replace("0\nLINE\n", f"0\nLINE\n6\n{name}\n")
            with self.subTest(name=name), self.assertRaisesRegex(ValueError, "outside.*material"):
                adapter.import_shape(self.file(drawing(polyline(outer) + finite)))

    def test_explicit_nonzero_arc_full_turns_are_not_destroyed_by_modulo(self):
        for start, end in ((0, 360), (90, 450), (0, 720), (360, 0)):
            with self.subTest(start=start, end=end):
                imported = adapter.import_shape(self.file(drawing(arc((0, 0), 10, start, end))))
                self.assertEqual(len(imported["geometries"]), 1)
                metadata = imported["metadata"]["arcs"][0]
                self.assertTrue(metadata["explicitFullTurn"])
                self.assertEqual(metadata["sourceAngleDifferenceDegrees"], end - start)
                self.assertGreater(area(imported["geometry"]["outer"]), math.pi * 100)
                for i in range(360):
                    t = math.tau * i / 360
                    self.assertTrue(contains_point(imported["geometry"]["outer"], (10 * math.cos(t), 10 * math.sin(t))))

    def test_ordinary_arc_crossing_zero_keeps_its_small_sweep(self):
        start = (10 * math.cos(math.radians(350)), 10 * math.sin(math.radians(350)))
        end = (10 * math.cos(math.radians(10)), 10 * math.sin(math.radians(10)))
        source = arc((0, 0), 10, 350, 10) + line(end, (0, 0)) + line((0, 0), start)
        imported = adapter.import_shape(self.file(drawing(source)))
        metadata = imported["metadata"]["arcs"][0]
        self.assertFalse(metadata["explicitFullTurn"])
        self.assertAlmostEqual(metadata["sweepRadians"], math.radians(20))
        self.assertTrue(contains_point(imported["geometry"]["outer"], (9, 0)))
        self.assertFalse(contains_point(imported["geometry"]["outer"], (-1, 0)))
        with self.assertRaisesRegex(ValueError, "zero-sweep"):
            adapter.import_shape(self.file(drawing(arc((0, 0), 10, 90, 90))))

    def test_real_same_angle_arcs_still_have_no_source_full_turn_evidence(self):
        catalog = json.loads((ROOT.parent / "TestData/Plane/catalog/catalog.json").read_text(encoding="utf-8"))
        for name in ("Back.dxf", "Front.dxf", "Q9-Plate-File-20220624.dxf"):
            asset = next(asset for asset in catalog["assets"] if asset["name"] == name)
            with self.subTest(name=name), self.assertRaisesRegex(ValueError, "zero-sweep"):
                adapter.import_shape(ROOT.parent / "TestData/Plane" / asset["relativePath"])

    def test_negative_z_ocs_arc_and_wcs_line_have_their_actual_world_orientation(self):
        # OCS -Z has X=(-1,0,0), Y=(0,1,0). WCS LINE coordinates stay WCS,
        # even when that LINE declares the same extrusion normal.
        negative = "210\n0\n220\n0\n230\n-1\n"
        source = (arc((10, 20), 5, 0, 180) + negative +
                  line((-5, 20), (-15, 20)) + negative)
        result = adapter.import_shape(self.file(drawing(source)))
        ring = result["geometry"]["outer"]
        for i in range(1, 180):
            theta = math.pi * i / 180
            self.assertTrue(contains_point(ring, (-10 - 4.999 * math.cos(theta), 20 + 4.999 * math.sin(theta))))
        self.assertFalse(contains_point(ring, (10, 22)))
        self.assertFalse(contains_point(ring, (-10, 19)))
        self.assertEqual(result["metadata"]["arcs"][0]["transformToWorldMm"], [-1, 0, 0, 1, 0, 0])

    def test_negative_z_circle_and_ellipse_preserve_projected_centers_and_minor_direction(self):
        negative = "210\n0\n220\n0\n230\n-1\n"
        disk = adapter.import_shape(self.file(drawing(circle((12, 7), 3) + negative)))
        self.assertTrue(contains_point(disk["geometry"]["outer"], (-12, 7)))
        self.assertFalse(contains_point(disk["geometry"]["outer"], (12, 7)))
        # ELLIPSE's center and major axis are WCS; -Z changes only its minor
        # direction. A quarter ellipse and two WCS radii close in quadrant IV.
        source = (ellipse((10, 20), (6, 0), .5, 0, math.pi / 2) + negative +
                  line((10, 17), (10, 20)) + line((10, 20), (16, 20)))
        curved = adapter.import_shape(self.file(drawing(source)))
        self.assertTrue(contains_point(curved["geometry"]["outer"], (12, 19)))
        self.assertFalse(contains_point(curved["geometry"]["outer"], (-12, 19)))
        self.assertFalse(contains_point(curved["geometry"]["outer"], (12, 21)))

    def test_negative_z_polyline_and_insert_compose_ocs_before_nested_block_transforms(self):
        negative = "210\n0\n220\n0\n230\n-1\n"
        source = ("0\nLWPOLYLINE\n70\n1\n90\n4\n" + negative +
                  "10\n0\n20\n0\n10\n4\n20\n0\n10\n4\n20\n2\n10\n0\n20\n2\n")
        blocks = block("part", source)
        # Positive INSERT scales local (-4..0,0..2) to (-8..0,0..6),
        # rotates it +90, translates to (10,20), then OCS -Z reflects X.
        reference = insert("part", (10, 20), (2, 3), 90) + negative
        result = adapter.import_shape(self.file(block_drawing(blocks, reference)))
        ring = result["geometry"]["outer"]
        self.assertEqual(set(map(tuple, ring)), {(-10, 20), (-10, 12), (-4, 12), (-4, 20)})
        self.assertEqual(result["geometry"]["commonEdgeRings"], [True])

    def test_only_bounded_planar_roundoff_is_mapped_and_source_evidence_is_retained(self):
        outer = [[0, 0], [20, 0], [20, 20], [0, 20]]
        near = circle((10, 10), 2).replace("30\n0\n", "30\n-7.105427357601002e-14\n")
        near += "210\n8.8579770191803285e-35\n220\n-1.1524605513628282e-52\n230\n1\n"
        result = adapter.import_shape(self.file(drawing(polyline(outer) + near)))
        self.assertEqual(len(result["geometry"]["holes"]), 1)
        self.assertEqual(result["geometry"]["outer"], outer)
        mapping = result["metadata"]["planarMappings"][0]
        self.assertEqual(mapping["sourceElevationValues"], [[30, -7.105427357601002e-14]])
        self.assertGreater(mapping["maximumOutOfPlaneResidualMm"], 0)
        self.assertLess(result["metadata"]["maximumPlanarResidualMm"], 1e-9)
        self.assertFalse(result["geometry"]["commonEdgeRings"][1])
        for normal in ((1e-6, 0, 1), (0, 0, 0)):
            spatial = circle((10, 10), 2) + f"210\n{normal[0]}\n220\n{normal[1]}\n230\n{normal[2]}\n"
            with self.subTest(normal=normal), self.assertRaisesRegex(ValueError, "extrusion"):
                adapter.import_shape(self.file(drawing(polyline(outer) + spatial)))
        spatial = line((0, 0), (1, 1)).replace("31\n0\n", "31\n0.01\n")
        with self.assertRaisesRegex(ValueError, "nonplanar"):
            adapter.import_shape(self.file(drawing(polyline(outer) + spatial)))

    def test_planar_residual_bound_accounts_for_block_scale_and_nonaxial_insert_is_explicit(self):
        residual = circle((2, 3), 1).replace("30\n0\n", "30\n1e-10\n")
        source = block_drawing(block("part", residual), insert("part", scales=(100, 100)))
        with self.assertRaisesRegex(ValueError, "amplifies.*residual"):
            adapter.import_shape(self.file(source))
        tilted = insert("part") + "210\n1e-35\n220\n0\n230\n1\n"
        with self.assertRaisesRegex(ValueError, "spatial block transform"):
            adapter.import_shape(self.file(block_drawing(block("part", circle((0, 0), 1)), tilted)))

    def test_insert_z_scale_cannot_hide_nonplanar_hole_or_nested_world_residual(self):
        outer = polyline([[0, 0], [20, 0], [20, 20], [0, 20]])
        hole = circle((10, 10), 2).replace("30\n0\n", "30\n5e-10\n")
        definitions = block("part", outer + hole)
        unit = adapter.import_shape(self.file(block_drawing(definitions, insert("part"))))
        self.assertEqual(len(unit["geometry"]["holes"]), 1)
        self.assertLess(unit["metadata"]["maximumPlanarResidualMm"], 1e-9)
        scaled = insert("part") + "43\n1000000000\n"
        with self.assertRaisesRegex(ValueError, "amplifies.*planar residual"):
            adapter.import_shape(self.file(block_drawing(definitions, scaled)))
        definitions += block("parent", insert("part") + "43\n-100000\n")
        with self.assertRaisesRegex(ValueError, "amplifies.*planar residual"):
            adapter.import_shape(self.file(block_drawing(definitions, insert("parent") + "43\n100000\n")))

    def test_legacy_polyline_parent_normal_bounds_the_full_bulged_arc(self):
        normal_x, bulge = 9e-13, 40000
        # The near-axis normal looks harmless at the two vertices. The major
        # circular arc extends about 10000 mm from them and exceeds the plane
        # residual limit even though the endpoint coordinates do not.
        self.assertGreater(normal_x * (bulge + 1 / bulge) / 4, 1e-9)
        source = (f"0\nPOLYLINE\n70\n1\n210\n{normal_x}\n220\n0\n230\n1\n"
                  f"0\nVERTEX\n10\n0\n20\n0\n30\n0\n42\n{bulge}\n"
                  "0\nVERTEX\n10\n1\n20\n0\n30\n0\n0\nSEQEND\n")
        with self.assertRaisesRegex(ValueError, "extrusion direction.*POLYLINE"):
            adapter.import_shape(self.file(drawing(source)))

    def test_real_planar_cad_residuals_do_not_fail_as_nonplanar_before_topology(self):
        catalog = json.loads((ROOT.parent / "TestData/Plane/catalog/catalog.json").read_text(encoding="utf-8"))
        for name, holes in (("MMP.00.00.00.001 Front panel.DXF", 24), ("MMP.00.00.00.002 Back panel.DXF", 15), ("pcb-cutouts.dxf", None)):
            asset = next(asset for asset in catalog["assets"] if asset["name"] == name)
            path = ROOT.parent / "TestData/Plane" / asset["relativePath"]
            with self.subTest(name=name):
                try:
                    result = adapter.import_shape(path)
                    self.assertIsNotNone(holes)
                    self.assertEqual(len(result["geometries"]), 1)
                    self.assertEqual(len(result["geometry"]["holes"]), holes)
                    self.assertTrue(result["metadata"]["planarMappings"])
                    self.assertLess(result["metadata"]["maximumPlanarResidualMm"], 1e-9)
                except ValueError as error:
                    # Subsequent source topology issues must be reported on
                    # their own evidence, rather than blaming floating Z.
                    self.assertNotIn("nonplanar", str(error))
                    self.assertNotIn("extrusion", str(error))
                    self.assertIsNone(holes)

    def test_explicit_endpoint_budget_records_every_mapping_and_keeps_the_strict_default(self):
        source = arc((0, 0), 10, 0, 180) + line((-10, 2e-6), (10, 0))
        path = self.file(drawing(source))
        before = hashlib.sha256(path.read_bytes()).hexdigest()
        with self.assertRaisesRegex(ValueError, "Open or branching curved"):
            adapter.import_shape(path)
        result = adapter.import_shape(path, connection_tolerance_mm=1e-5)
        metadata = result["metadata"]
        self.assertEqual(metadata["connectionToleranceMm"], 1e-5)
        self.assertTrue(metadata["explicitConnectionTolerance"])
        joined = max(metadata["endpointConnections"], key=lambda c: c["displacementMm"])
        self.assertEqual(joined["sourcePointMm"], [-10, 2e-6])
        self.assertEqual(joined["sourceEndpoint"], "start")
        self.assertGreater(joined["displacementMm"], 1.99e-6)
        self.assertLessEqual(joined["displacementMm"], 1e-5)
        self.assertLess(metadata["computedDeviationBudgetMm"], .01)
        self.assertGreaterEqual(metadata["roundBufferDesiredOffsetMm"], 2 * metadata["maxConnectionErrorMm"])
        self.assertEqual(result["geometry"]["commonEdgeRings"], [False])
        self.assertEqual(hashlib.sha256(path.read_bytes()).hexdigest(), before)

    def test_explicit_endpoint_budget_never_closes_over_limit_or_ambiguous_nearby_graphs(self):
        source = arc((0, 0), 10, 0, 180) + line((-10, 2e-5), (10, 0))
        with self.assertRaisesRegex(ValueError, "Open or branching curved"):
            adapter.import_shape(self.file(drawing(source)), connection_tolerance_mm=1e-5)
        ambiguous = (circle((100, 100), 5) + line((0, 0), (5, 0)) +
                     line((1.5e-5, 0), (5, 5)) + line((.75e-5, 0), (5, -5)))
        with self.assertRaisesRegex(ValueError, "Ambiguous curved-chain endpoints"):
            adapter.import_shape(self.file(drawing(ambiguous)), connection_tolerance_mm=1e-5)
        branch = circle((100, 100), 5) + line((0, 0), (1, 1)) + line((0, 0), (1, -1)) + line((0, 0), (2, 0))
        with self.assertRaisesRegex(ValueError, "Open|outside"):
            adapter.import_shape(self.file(drawing(branch)), connection_tolerance_mm=1e-5)
        for connection in (0, -.01, .002, math.inf):
            with self.subTest(connection=connection), self.assertRaises(ValueError):
                adapter.import_shape(self.file(drawing(circle((0, 0), 1))), connection_tolerance_mm=connection)

    def test_real_winch_bounded_connection_covers_the_original_cubic_and_all_source_lines(self):
        catalog = json.loads((ROOT.parent / "TestData/Plane/catalog/catalog.json").read_text(encoding="utf-8"))
        asset = next(asset for asset in catalog["assets"] if asset["name"] == "Winch Mount Flange.DXF")
        path = ROOT.parent / "TestData/Plane" / asset["relativePath"]
        before = hashlib.sha256(path.read_bytes()).hexdigest()
        with self.assertRaisesRegex(ValueError, "no closed material boundary"):
            adapter.import_shape(path)
        result = adapter.import_shape(path, connection_tolerance_mm=1e-5)
        self.assertEqual(len(result["geometries"]), 1)
        self.assertEqual(len(result["geometry"]["holes"]), 0)
        ring = result["geometry"]["outer"]
        controls = [(564.4492015073362, 533.8186960442368), (584.9058396381993, 552.8705688935719),
                    (605.434412563241, 571.8460301180062), (626.0349202824614, 590.7450797175394)]
        for step in range(101):
            t = step / 100
            basis = ((1-t)**3, 3*t*(1-t)**2, 3*t*t*(1-t), t**3)
            point = tuple(sum(weight * control[axis] for weight, control in zip(basis, controls)) for axis in (0, 1))
            self.assertTrue(contains_point(ring, point))
        lines = [((484.6463132924, 732.1336867076), (425.3902620681, 672.8776354834)),
                 ((425.3902620681, 672.8776354834), (564.4492015073, 533.8186960442)),
                 ((626.0349212565, 590.7450787435), (484.6463132924, 732.1336867076))]
        for a, b in lines:
            for step in range(21):
                t = step / 20
                self.assertTrue(contains_point(ring, tuple((1-t)*a[axis] + t*b[axis] for axis in (0, 1))))
        maximum = result["metadata"]["maxConnectionErrorMm"]
        self.assertGreater(maximum, 1.37e-6)
        self.assertLess(maximum, 1.39e-6)
        self.assertLess(result["metadata"]["computedDeviationBudgetMm"], .01)
        self.assertEqual(hashlib.sha256(path.read_bytes()).hexdigest(), before)

    def test_real_sifrl_bounded_connections_retain_all_eighty_five_holes(self):
        catalog = json.loads((ROOT.parent / "TestData/Plane/catalog/catalog.json").read_text(encoding="utf-8"))
        asset = next(asset for asset in catalog["assets"] if asset["name"] == "sifrl-plate.dxf")
        path = ROOT.parent / "TestData/Plane" / asset["relativePath"]
        before = hashlib.sha256(path.read_bytes()).hexdigest()
        with self.assertRaisesRegex(ValueError, "open curved stroke"):
            adapter.import_shape(path)
        result = adapter.import_shape(path, connection_tolerance_mm=1e-5)
        self.assertEqual(len(result["geometries"]), 1)
        self.assertEqual(len(result["geometry"]["holes"]), 85)
        self.assertGreater(result["metadata"]["maxConnectionErrorMm"], 3.49e-7)
        self.assertLess(result["metadata"]["maxConnectionErrorMm"], 3.50e-7)
        self.assertEqual(hashlib.sha256(path.read_bytes()).hexdigest(), before)

    def test_exact_zero_length_line_keeps_source_point_provenance_without_inventing_material(self):
        outer = [[0, 0], [10, 0], [10, 5], [0, 5]]
        point = line((10000, 10000), (10000, 10000))
        path = self.file(drawing(polyline(outer) + point))
        before = hashlib.sha256(path.read_bytes()).hexdigest()
        result = adapter.import_shape(path)
        self.assertEqual(result["geometry"]["outer"], outer)
        self.assertEqual(result["geometry"]["holes"], [])
        entry = result["metadata"]["nonMaterialEntities"][0]
        self.assertEqual(entry["sourceType"], "LINE")
        self.assertIn("exact zero-length", entry["classification"])
        self.assertEqual(list(map(float, entry["sourcePoint"])), [10000, 10000, 0])
        self.assertFalse(entry["boundsUsedAsMaterial"])
        self.assertEqual(entry["sourceDxfSha256"], before)
        self.assertEqual(hashlib.sha256(path.read_bytes()).hexdigest(), before)
        with self.assertRaisesRegex(ValueError, "no closed material") as raised:
            adapter.import_shape(self.file(drawing(point)))
        self.assertEqual(len(raised.exception.non_material_entities), 1)

    def test_zero_length_classification_never_drops_short_or_spatial_source_lines(self):
        outer = polyline([[0, 0], [10, 0], [10, 5], [0, 5]])
        short = line((30, 30), (30 + 1e-9, 30))
        with self.assertRaises(ValueError) as raised:
            adapter.import_shape(self.file(drawing(outer + short)))
        self.assertFalse(any(entry["sourceType"] == "LINE" for entry in raised.exception.non_material_entities))
        spatial = line((30, 30), (30, 30)).replace("31\n0\n", "31\n.01\n")
        with self.assertRaisesRegex(ValueError, "nonplanar"):
            adapter.import_shape(self.file(drawing(outer + spatial)))

    def test_real_q6_zero_length_record_no_longer_hides_the_remaining_topology_issue(self):
        catalog = json.loads((ROOT.parent / "TestData/Plane/catalog/catalog.json").read_text(encoding="utf-8"))
        asset = next(asset for asset in catalog["assets"] if asset["name"] == "Q6-Plate-20220616.dxf")
        path = ROOT.parent / "TestData/Plane" / asset["relativePath"]
        before = hashlib.sha256(path.read_bytes()).hexdigest()
        with self.assertRaises(ValueError) as raised:
            adapter.import_shape(path)
        self.assertNotIn("Zero-length LINE", str(raised.exception))
        points = [entry for entry in raised.exception.non_material_entities if entry["sourceType"] == "LINE"]
        self.assertTrue(points)
        self.assertEqual(hashlib.sha256(path.read_bytes()).hexdigest(), before)

    def test_hatch_duplicate_proof_preserves_fill_semantics_and_never_adds_extra_parts_or_fills_holes(self):
        outer = [[0, 0], [20, 0], [20, 10], [0, 10]]
        hole = [[3, 3], [7, 3], [7, 7], [3, 7]]
        for poly_boundary in (False, True):
            source = polyline(outer) + polyline(hole) + hatch_lines([outer, hole], style=2, polyline_boundary=poly_boundary)
            path = self.file(drawing(source))
            before = hashlib.sha256(path.read_bytes()).hexdigest()
            result = adapter.import_shape(path)
            self.assertEqual(len(result["geometries"]), 1)
            self.assertEqual(result["geometry"]["outer"], outer)
            self.assertEqual(result["geometry"]["holes"], [hole])
            entry = result["metadata"]["nonMaterialEntities"][0]
            self.assertEqual(entry["sourceType"], "HATCH")
            self.assertEqual(int(entry["fillSemantics"]["hatchStyle"]), 2)
            self.assertTrue(entry["sourceTags"])
            proof = entry["boundaryProof"]
            self.assertEqual(proof["status"], "proved-redundant-boundary")
            self.assertEqual(len(proof["paths"]), 2)
            self.assertEqual(proof["maximumDeviationMm"], 0)
            self.assertTrue(all(match["matchedEntities"] for p in proof["paths"] for match in p["matches"]))
            self.assertEqual(hashlib.sha256(path.read_bytes()).hexdigest(), before)

    def test_hatch_spline_duplicate_is_proved_by_complete_parameters_and_changed_controls_do_not_match(self):
        points = [(0, 0), (0, 3), (3, 3), (3, 0), (3, -3), (0, -3), (0, 0)]
        knots = [0] * 4 + [1] * 3 + [2] * 4
        source = spline(points, 3, knots, flags=9)
        result = adapter.import_shape(self.file(drawing(source + hatch_spline(points, 3, knots))))
        self.assertEqual(len(result["geometries"]), 1)
        proof = result["metadata"]["hatchBoundaryMappings"][0]
        self.assertEqual(proof["paths"][0]["matches"][0]["type"], "SPLINE")
        changed = list(points)
        changed[1] = (.01, 3)
        with self.assertRaisesRegex(ValueError, "Unresolved DXF HATCH.*SPLINE") as raised:
            adapter.import_shape(self.file(drawing(source + hatch_spline(changed, 3, knots))))
        self.assertEqual(raised.exception.hatch_boundary_mappings[0]["status"], "unresolved")
        self.assertTrue(raised.exception.hatch_boundary_mappings[0]["sourceTags"])

    def test_hatch_near_duplicate_endpoint_error_enters_conservative_budget_and_exact_common_edges_are_excluded(self):
        outer = [[0, 0], [10, 0], [10, 5], [0, 5]]
        filled = [[0, 0], [10 + 1e-8, 0], [10, 5], [0, 5]]
        result = adapter.import_shape(self.file(drawing(polyline(outer) + hatch_lines([filled]))))
        self.assertEqual(len(result["geometries"]), 1)
        self.assertEqual(result["geometry"]["commonEdgeRings"], [False])
        metadata = result["metadata"]
        self.assertGreater(metadata["hatchBoundaryMaximumDeviationMm"], 9.9e-9)
        self.assertLessEqual(metadata["computedDeviationBudgetMm"], .01)
        for point in filled:
            self.assertTrue(contains_point(result["geometry"]["outer"], point))
        filled[1][0] = 10 + 2e-7
        with self.assertRaisesRegex(ValueError, "Unresolved DXF HATCH"):
            adapter.import_shape(self.file(drawing(polyline(outer) + hatch_lines([filled]))))

    def test_hatch_only_unmatched_or_unsupported_boundaries_are_never_silently_ignored(self):
        outer = [[0, 0], [10, 0], [10, 5], [0, 5]]
        other = [[100, 100], [110, 100], [110, 105], [100, 105]]
        for source in (hatch_lines([outer]), polyline(outer) + hatch_lines([other]),
                       polyline(outer) + hatch_lines([outer]).replace("72\n1\n10\n", "72\n2\n10\n", 1),
                       polyline(outer) + hatch_lines([outer]).replace("93\n4\n", "93\n5\n", 1)):
            with self.subTest(source=source[:60]), self.assertRaisesRegex(ValueError, "Unresolved DXF HATCH"):
                adapter.import_shape(self.file(drawing(source)))

    def test_hatch_ocs_and_nested_mirrored_insert_match_world_vectors_and_keep_source_tags(self):
        local = [[0, 0], [4, 0], [4, 2], [0, 2]]
        world = [[0, 0], [-4, 0], [-4, 2], [0, 2]]
        source = polyline(world) + hatch_lines([local], normal=(0, 0, -1))
        blocks = block("part", source) + block("outer", insert("part", (3, 4), (-2, 3), 90))
        result = adapter.import_shape(self.file(block_drawing(blocks, insert("outer", (10, -5)))))
        self.assertEqual(len(result["geometries"]), 1)
        self.assertEqual(set(map(tuple, result["geometry"]["outer"])), {(13, -1), (13, 7), (7, 7), (7, -1)})
        proof = result["metadata"]["hatchBoundaryMappings"][0]
        self.assertEqual(proof["maximumDeviationMm"], 0)
        source_normal = [float(value) for code, value in proof["sourceTags"] if int(code) == 230]
        self.assertEqual(source_normal, [-1])

    def test_real_rear_bracket_all_seven_hatch_presentations_keep_seven_parts_and_eight_holes(self):
        catalog = json.loads((ROOT.parent / "TestData/Plane/catalog/catalog.json").read_text(encoding="utf-8"))
        asset = next(asset for asset in catalog["assets"] if asset["name"] == "rear_lower_coilover_bracketai.dxf")
        path = ROOT.parent / "TestData/Plane" / asset["relativePath"]
        before = hashlib.sha256(path.read_bytes()).hexdigest()
        result = adapter.import_shape(path, scale_to_mm=asset["units"]["scaleToMm"])
        self.assertEqual(len(result["geometries"]), 7)
        self.assertEqual(sum(len(g["holes"]) for g in result["geometries"]), 8)
        self.assertEqual(len(result["metadata"]["hatchBoundaryMappings"]), 7)
        self.assertTrue(all(p["status"] == "proved-redundant-boundary" for p in result["metadata"]["hatchBoundaryMappings"]))
        self.assertGreater(result["metadata"]["hatchBoundaryMaximumDeviationMm"], 0)
        self.assertLess(result["metadata"]["hatchBoundaryMaximumDeviationMm"], 3e-15)
        self.assertEqual(hashlib.sha256(path.read_bytes()).hexdigest(), before)

    def test_real_stude_all_twenty_six_hatch_splines_have_exact_proof_before_remaining_curve_issue(self):
        catalog = json.loads((ROOT.parent / "TestData/Plane/catalog/catalog.json").read_text(encoding="utf-8"))
        asset = next(asset for asset in catalog["assets"] if asset["name"] == "stude_seat_bracket.dxf")
        path = ROOT.parent / "TestData/Plane" / asset["relativePath"]
        with self.assertRaisesRegex(ValueError, "clearance below") as raised:
            adapter.import_shape(path, scale_to_mm=asset["units"]["scaleToMm"])
        proof = raised.exception.hatch_boundary_mappings[0]
        self.assertEqual(proof["status"], "proved-redundant-boundary")
        self.assertEqual(len(proof["paths"]), 26)
        self.assertEqual(proof["maximumDeviationMm"], 0)

    def test_complete_anonymous_dimension_copy_uses_the_source_group_without_creating_arrow_parts(self):
        definitions, content, _, _, _ = dimension_fixture()
        outer = [[0, 0], [20, 0], [20, 20], [0, 20]]
        path = self.file(block_drawing(definitions, polyline(outer) + circle((10, 10), 2) + hatch_lines([outer]) + content))
        before = hashlib.sha256(path.read_bytes()).hexdigest()
        result = adapter.import_shape(path)
        self.assertEqual(result["geometry"]["outer"], outer)
        self.assertEqual(len(result["geometries"]), 1)
        self.assertEqual(len(result["geometry"]["holes"]), 1)
        proof = result["metadata"]["dimensionCopyMappings"][0]
        self.assertEqual(proof["status"], "proved-complete-dimension-copy")
        self.assertEqual(len(proof["matches"]), 6)
        self.assertTrue(all(not match["materialRoleRetained"] for match in proof["matches"]))
        self.assertEqual(sum(e["sourceType"] == "SOLID" for e in result["metadata"]["nonMaterialEntities"]), 2)
        self.assertEqual(hashlib.sha256(path.read_bytes()).hexdigest(), before)

    def test_dimension_copy_never_removes_matched_lines_that_also_form_a_real_hole(self):
        left, bottom, right, top = (((10, 10), (10, 20)), ((10, 10), (20, 10)),
                                    ((20, 10), (20, 20)), ((10, 20), (20, 20)))
        definitions, content, _, _, _ = dimension_fixture([left, bottom, right], (15, 8))
        outer = [[0, 0], [30, 0], [30, 30], [0, 30]]
        result = adapter.import_shape(self.file(block_drawing(definitions, polyline(outer) + content + line(*top))))
        self.assertEqual(len(result["geometry"]["holes"]), 1)
        self.assertEqual(area(result["geometry"]["outer"]) - area(result["geometry"]["holes"][0]), 800)
        matches = result["metadata"]["dimensionCopyMappings"][0]["matches"]
        self.assertTrue(all(m["materialRoleRetained"] for m in matches if m["sourceType"] == "LINE"))

    def test_dimension_line_internal_crossing_has_no_endpoint_only_bridge_certificate(self):
        edges = [((10, 10), (10, 20)), ((10, 10), (20, 10)), ((20, 10), (20, 20))]
        definitions, content, _, _, _ = dimension_fixture(edges, (15, 8))
        outer = [[0, 0], [70, 0], [70, 70], [0, 70]]
        source = polyline(outer) + content + line((5, 15), (15, 15))
        result = adapter.import_shape(self.file(block_drawing(definitions, source)))
        matches = result["metadata"]["dimensionCopyMappings"][0]["matches"]
        protected = [m for m in matches if m["sourceType"] == "LINE" and m["materialRoleRetained"]]
        self.assertTrue(protected)
        self.assertTrue(any("internal contact" in m["materialProtection"] for m in protected))

    def test_partial_ambiguous_or_non_dimension_block_copies_do_not_hide_any_solid(self):
        definitions, content, lines, solids, text = dimension_fixture()
        outer = polyline([[0, 0], [30, 0], [30, 30], [0, 30]])
        variants = [(definitions, lines + solids[0] + text),
                    (definitions, content + solids[0]),
                    (definitions.replace("*D0", "REAL_PART"), content),
                    (definitions.replace("70\n1\n", "70\n0\n", 1), content),
                    (definitions + definitions.replace("*D0", "*D1"), content)]
        for blocks, model in variants:
            with self.subTest(model=model[:40]), self.assertRaisesRegex(ValueError, "SOLID") as raised:
                adapter.import_shape(self.file(block_drawing(blocks, outer + model)))
            self.assertFalse(any(e["sourceType"] == "SOLID" for e in raised.exception.non_material_entities))

    def test_same_shaped_real_solid_on_another_layer_survives_dimension_copy_recognition(self):
        definitions, content, _, solids, _ = dimension_fixture()
        real = solids[0].replace("8\n0\n", "8\nactual-part\n")
        source = polyline([[0, 0], [30, 0], [30, 30], [0, 30]]) + content + real
        with self.assertRaisesRegex(ValueError, "SOLID") as raised:
            adapter.import_shape(self.file(block_drawing(definitions, source)))
        self.assertEqual(sum(e["sourceType"] == "SOLID" for e in raised.exception.non_material_entities), 2)

    def test_real_q60_twenty_arrows_have_complete_dimension_copy_proof_but_open_strokes_remain_pending(self):
        catalog = json.loads((ROOT.parent / "TestData/Plane/catalog/catalog.json").read_text(encoding="utf-8"))
        asset = next(asset for asset in catalog["assets"] if asset["name"] == "Q60-Plate-File-20220923.dxf")
        path = ROOT.parent / "TestData/Plane" / asset["relativePath"]
        before = hashlib.sha256(path.read_bytes()).hexdigest()
        with self.assertRaises(ValueError) as raised:
            adapter.import_shape(path)
        proofs = raised.exception.dimension_copy_mappings
        complete = [p for p in proofs if p["status"] == "proved-complete-dimension-copy"]
        self.assertEqual(len(complete), 11)
        self.assertEqual(sum(m["sourceType"] == "SOLID" for p in complete for m in p["matches"]), 20)
        self.assertEqual(sum(e["sourceType"] == "SOLID" for e in raised.exception.non_material_entities), 20)
        self.assertEqual(next(p for p in proofs if p["sourceBlockName"] == "*D7")["status"], "unresolved")
        unselected = [c for p in proofs for match in p["matches"] for c in match.get("unselectedBoundedCandidates", [])]
        self.assertTrue(any(c["sourceHandle"] == "79B4" and c["geometryRetained"] and c["sourceTags"] for c in unselected))
        self.assertEqual(hashlib.sha256(path.read_bytes()).hexdigest(), before)

    def test_joint_profile_roles_preserve_the_complete_part_and_hole_across_bend_strokes(self):
        outer = on_layer(polyline([[0, 0], [20, 0], [20, 20], [0, 20]]), "OUTER_PROFILES")
        hole = on_layer(circle((10, 10), 2), "INTERIOR_PROFILES")
        bend = on_layer(line((5, 0), (5, 20)), "BEND")
        extent = on_layer(line((-5, 10), (25, 10)), "BEND_EXTENT")
        path = self.file(drawing(outer + hole + bend + extent))
        before = hashlib.sha256(path.read_bytes()).hexdigest()
        result = adapter.import_shape(path)
        self.assertEqual(len(result["geometries"]), 1)
        self.assertEqual(len(result["geometry"]["holes"]), 1)
        proof = result["metadata"]["sourceRoleValidation"]
        self.assertEqual(proof["status"], "proved-process-strokes")
        self.assertEqual(len(proof["acceptedEntities"]), 2)
        self.assertTrue(all(e["nonCandidateCutVectorsAllClosed"] for e in proof["acceptedEntities"].values()))
        self.assertTrue(result["metadata"]["sourceRoleProvenance"]["profileConvention"])
        self.assertEqual(hashlib.sha256(path.read_bytes()).hexdigest(), before)

    def test_source_role_recovery_never_downgrades_three_open_hole_edges_after_removing_bend_bottom(self):
        outer = on_layer(polyline([[0, 0], [30, 0], [30, 30], [0, 30]]), "OUTER_PROFILES")
        bottom = on_layer(line((10, 10), (20, 10)), "BEND")
        rest = "".join(on_layer(line(a, b), "INTERIOR_PROFILES") for a, b in
                       [((20, 10), (20, 20)), ((20, 20), (10, 20)), ((10, 20), (10, 10))])
        extent = on_layer(line((1, 1), (2, 1)), "BEND_EXTENT")
        complete = outer + bottom + rest + extent
        baseline = adapter.import_shape(self.file(drawing(complete)))
        self.assertEqual(len(baseline["geometry"]["holes"]), 1)
        with self.assertRaises(ValueError) as raised:
            adapter.import_shape(self.file(drawing(complete + on_layer(line((-10, 0), (-5, 0)), "BEND"))))
        self.assertIn("open strokes", raised.exception.source_role_validation["reason"])

    def test_center_role_recovery_uses_full_declared_linetype_and_keeps_caller_near_closed_hole(self):
        outer = polyline([[0, 0], [30, 0], [30, 30], [0, 30]])
        center = line((10 + 5e-6, 10), (20 - 5e-6, 10)) + "6\nCENTERX2\n"
        rest = "".join(line(a, b) for a, b in [((20, 10), (20, 20)), ((20, 20), (10, 20)), ((10, 20), (10, 10))])
        source = outer + center + rest
        baseline = adapter.import_shape(self.file(source_role_drawing(source)), connection_tolerance_mm=1e-5)
        self.assertEqual(len(baseline["geometry"]["holes"]), 1)
        outside = line((-10, 0), (-5, 0)) + "6\nCENTERX2\n"
        with self.assertRaises(ValueError) as raised:
            adapter.import_shape(self.file(source_role_drawing(source + outside)), connection_tolerance_mm=1e-5)
        self.assertIn("open strokes", raised.exception.source_role_validation["reason"])

    def test_center_candidates_do_not_remove_a_closed_region_or_positive_length_cut_edge(self):
        outer = polyline([[0, 0], [30, 0], [30, 30], [0, 30]])
        box = "".join(line(a, b) + "6\nCENTERX2\n" for a, b in
                      [((40, 40), (50, 40)), ((50, 40), (50, 50)), ((50, 50), (40, 50)), ((40, 50), (40, 40))])
        outside = line((-10, 0), (-5, 0)) + "6\nCENTERX2\n"
        with self.assertRaises(ValueError) as raised:
            adapter.import_shape(self.file(source_role_drawing(outer + box + outside)))
        self.assertIn("closed region", raised.exception.source_role_validation["reason"])
        near_box = box.replace("10\n40\n20\n40\n", "10\n40.000005\n20\n40\n", 1)
        with self.assertRaises(ValueError) as raised:
            adapter.import_shape(self.file(source_role_drawing(outer + near_box + outside)), connection_tolerance_mm=1e-5)
        self.assertIn("closed region", raised.exception.source_role_validation["reason"])
        same_edge = line((0, 0), (30, 0)) + "6\nCENTERX2\n"
        with self.assertRaises(ValueError):
            adapter.import_shape(self.file(source_role_drawing(outer + same_edge + outside)))

    def test_source_roles_preserve_successful_original_import_without_attempting_recovery(self):
        outer = polyline([[0, 0], [30, 0], [30, 30], [0, 30]])
        adapter.import_shape(self.file(drawing(outer)))  # Load the production module for this isolated test.
        source = outer + line((5, 10), (25, 10)) + "6\nCENTERX2\n"
        with mock.patch.object(adapter._material_module, "_try_source_process_roles", side_effect=AssertionError("successful original geometry must not be replaced")):
            result = adapter.import_shape(self.file(source_role_drawing(source)))
        self.assertEqual(len(result["geometry"]["holes"]), 0)
        self.assertIsNone(result["metadata"]["sourceRoleValidation"])

    def test_center_source_role_resolves_layer_zero_and_byblock_through_mirrored_insert(self):
        source = (polyline([[0, 0], [20, 0], [20, 20], [0, 20]]) + circle((10, 10), 2) +
                  line((-5, 10), (25, 10)) + "6\nBYBLOCK\n")
        reference = insert("part", (30, -10), (-2, 3), 90) + "8\nmarker\n6\nBYLAYER\n"
        result = adapter.import_shape(self.file(source_role_drawing(reference, block("part", source))))
        self.assertEqual(len(result["geometry"]["holes"]), 1)
        entry = next(e for e in result["metadata"]["nonMaterialEntities"] if e.get("materialProof"))
        role = entry["sourceRole"]
        self.assertEqual(role["effectiveLayer"], "marker")
        self.assertEqual(role["effectiveLinetype"], "CENTERX2")
        self.assertEqual(role["referencePath"], ["part"])
        self.assertEqual(role["inheritedFrom"]["sourceType"], "INSERT")

    def test_real_profile_and_center_sources_retain_all_original_holes(self):
        catalog = json.loads((ROOT.parent / "TestData/Plane/catalog/catalog.json").read_text(encoding="utf-8"))
        for name, holes, processes in (("Rigid Z Bracket Flat Pattern.dxf", 7, 9),
                                      ("MMP.05.00.00.001 Support.DXF", 12, 4),
                                      ("MMP.01.00.00.001 Cover.DXF", 15, 10),
                                      ("MMP.00.00.00.003 Side panel.DXF", 16, 16)):
            asset = next(asset for asset in catalog["assets"] if asset["name"] == name)
            path = ROOT.parent / "TestData/Plane" / asset["relativePath"]
            before = hashlib.sha256(path.read_bytes()).hexdigest()
            with self.subTest(name=name):
                result = adapter.import_shape(path, scale_to_mm=asset["units"]["scaleToMm"])
                self.assertEqual(len(result["geometries"]), 1)
                self.assertEqual(len(result["geometry"]["holes"]), holes)
                self.assertEqual(len(result["metadata"]["sourceRoleValidation"]["acceptedEntities"]), processes)
                self.assertEqual(hashlib.sha256(path.read_bytes()).hexdigest(), before)

    def test_real_center_flange_keeps_unclosed_arcs_pending_after_center_proof(self):
        catalog = json.loads((ROOT.parent / "TestData/Plane/catalog/catalog.json").read_text(encoding="utf-8"))
        asset = next(asset for asset in catalog["assets"] if asset["name"] == "MMP.01.00.00.002 Cover flange.DXF")
        with self.assertRaises(ValueError) as raised:
            adapter.import_shape(ROOT.parent / "TestData/Plane" / asset["relativePath"])
        self.assertIn("open curved stroke", raised.exception.source_role_validation["reason"])

    def test_real_embedded_objects_preserve_provenance_even_if_other_drawing_entities_are_unresolved(self):
        names = ("Beam X - Main.DXF", "Beam X1.DXF", "Beam X2.DXF", "Beam Y.DXF",
                 "Bracket for tighteners - left.DXF", "Bracket for tighteners - right.DXF",
                 "L Profile - Chain Support.DXF", "Tightener - left.DXF", "Tightener - right.DXF",
                 "Tightener - Spacer.DXF", "Plasma.DXF", "Beam Short X.DXF", "Beam Short.DXF",
                 "Trolley X.DXF", "Trolley Y.DXF", "Bottom plate.DXF", "Linear guide mount.DXF",
                 "Nut casing - Bottom.DXF", "Nut casing - Upper.DXF", "Torch mount.DXF", "Z Axis Assembly.DXF")
        catalog = json.loads((ROOT.parent / "TestData/Plane/catalog/catalog.json").read_text(encoding="utf-8"))
        for name in names:
            asset = next(asset for asset in catalog["assets"] if asset["name"] == name)
            path = ROOT.parent / "TestData/Plane" / asset["relativePath"]
            before = hashlib.sha256(path.read_bytes()).hexdigest()
            with self.subTest(name=name):
                try:
                    imported = adapter.import_shape(path)
                    objects = imported["metadata"]["nonMaterialEntities"]
                except ValueError as error:
                    objects = error.non_material_entities
                    self.assertNotIn("Unsupported DXF material entity OLE2FRAME", str(error))
                self.assertEqual(before, hashlib.sha256(path.read_bytes()).hexdigest())
                self.assertEqual(len(objects), 1)
                self.assertEqual(objects[0]["sourceType"], "OLE2FRAME")
                self.assertEqual(objects[0]["sourceDxfSha256"], before)
                self.assertEqual(len(objects[0]["payloadSha256"]), 64)
                self.assertTrue(objects[0]["payloadLengthMatchesDeclared"])
                self.assertFalse(objects[0]["boundsUsedAsMaterial"])


if __name__ == "__main__":
    unittest.main()
