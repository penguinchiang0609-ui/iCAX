"""Real display graphs retain outside corners without intersecting frame shells."""
from copy import deepcopy
import math
import unittest

from WindowCatalogueTests import package
from WindowManufacturingInputTests import forbid_product_processing


def dot(a, b):
    return sum(x*y for x, y in zip(a, b))


def sub(a, b):
    return [x-y for x, y in zip(a, b)]


def cross(a, b):
    return [a[1]*b[2]-a[2]*b[1], a[2]*b[0]-a[0]*b[2], a[0]*b[1]-a[1]*b[0]]


def unit(a):
    length = math.sqrt(dot(a, a))
    return [v/length for v in a]


def in_loop(loop, point):
    """Exact horizontal ray against the actual line/circle-arc paths used here."""
    px, py = point
    crossings = 0
    for edge in loop['segments']:
        a, b = edge['start'], edge['end']
        if edge['kind'] == 'line':
            if (a[1] > py) != (b[1] > py):
                crossings += a[0]+(b[0]-a[0])*(py-a[1])/(b[1]-a[1]) > px
            continue
        if edge['kind'] != 'arc':
            raise AssertionError('Fixture requires exact line/arc paths, got '+edge['kind'])
        m = edge['middle']
        u, v = sub(m, a), sub(b, a)
        den = 2*(u[0]*v[1]-u[1]*v[0])
        cx = a[0]+(v[1]*dot(u, u)-u[1]*dot(v, v))/den
        cy = a[1]+(u[0]*dot(v, v)-v[0]*dot(u, u))/den
        radius = math.dist(a, [cx, cy])
        if abs(py-cy) >= radius:
            continue
        start, middle, end = [math.atan2(p[1]-cy, p[0]-cx) for p in (a, m, b)]
        ccw = (middle-start) % math.tau < (end-start) % math.tau
        sweep = (end-start) % math.tau if ccw else (start-end) % math.tau
        offset = math.sqrt(radius*radius-(py-cy)**2)
        for x in (cx-offset, cx+offset):
            angle = math.atan2(py-cy, x-cx)
            fraction = (angle-start) % math.tau if ccw else (start-angle) % math.tau
            if x > px and fraction < sweep-1.e-10:
                crossings += 1
    return crossings % 2 == 1


class DisplayGraph:
    """Independent point classification of the emitted exact CSG resources."""
    def __init__(self, document):
        self.nodes = {node['key']: node for node in document['resources']}
        self.items = {item['key']: item for item in document['items']}

    @staticmethod
    def inverse(point, placement):
        x = placement.get('xAxis', [1., 0., 0.])
        y = placement.get('yAxis', [0., 1., 0.])
        z = placement.get('zAxis', cross(x, y))
        delta = sub(point, placement.get('origin', [0., 0., 0.]))
        return [dot(delta, axis) for axis in (x, y, z)]

    def member(self, key, point):
        geometry = self.items[key]['geometry']
        return self.contains(geometry['resource'], self.inverse(point, geometry.get('placement', {})))

    def contains(self, key, point):
        node = self.nodes[key]
        args = node['arguments']
        inputs = node.get('inputs', [])
        if node['operator'] == 'transform':
            return self.contains(inputs[0], self.inverse(point, args['placement']))
        if node['operator'] == 'boolean':
            target = args.get('target', inputs[0])
            tools = args.get('tools', inputs[1:])
            assert args['operation'] == 'subtract'
            return self.contains(target, point) and not any(self.contains(tool, point) for tool in tools)
        if node['operator'] != 'extrude':
            raise AssertionError('Unexpected volume operator '+node['operator'])
        profile = self.nodes[inputs[0]]['arguments']
        placement = profile['placement']
        local = self.inverse(point, placement)
        vector = args['vector']
        z = cross(placement['xAxis'], placement['yAxis'])
        fraction = local[2]/dot(vector, z)
        if not -1.e-9 <= fraction <= 1.+1.e-9:
            return False
        base = [point[i]-fraction*vector[i] for i in range(3)]
        xy = self.inverse(base, placement)[:2]
        return sum(in_loop(loop, xy) for loop in profile['contours']) % 2 == 1


class WindowDisplayCornerGeometryTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.descriptor, cls.defaults, cls.product = package('single_face_security_window')

    def display(self, **changes):
        values = dict(self.defaults, width=1200., height=1800., sideWidth=600.,
                      leftWidth=600., rightWidth=600., depth=600., **changes)
        saved = deepcopy(values)
        with forbid_product_processing():
            display = self.product.display(values)
            source = self.product._generate_resource_document(values, {
                'template': self.descriptor, 'geometryPurpose': 'display'})
        self.assertEqual(values, saved)
        self.assertEqual(source['parameters'], saved)
        return display

    def test_single_outer_fixed_and_leaf_have_complementary_shells_and_complete_outside_corners(self):
        graph = DisplayGraph(self.display(faceType='single', accessDoorEnabled=True))
        for prefix in ('outer_frame', 'access_door.fixed_frame', 'access_door.leaf.frame'):
            for side, vertical_side in (('bottom', 'left'), ('bottom', 'right'), ('top', 'left'), ('top', 'right')):
                vertical, rail = prefix+'.'+vertical_side+'.0001', prefix+'.'+side+'.0001'
                v = graph.items[vertical]['properties']['assemblyFrame.member']
                h = graph.items[rail]['properties']['assemblyFrame.member']
                end = 'start' if side == 'bottom' else 'end'
                rail_end = 'start' if vertical_side == 'left' else 'end'
                node = v[end]
                self.assertEqual(node, h[rail_end])
                inward = unit(sub(v['end' if end == 'start' else 'start'], node))
                across = unit(sub(h['end' if rail_end == 'start' else 'start'], node))
                half = graph.items[vertical]['properties']['tubeDesigner.profile']['width']/2
                # Positive quadrant was a real flat-wall intersection; the
                # negative quadrant was absent from centre-to-centre blanks.
                for sign in (-1., 1.):
                    point = [node[i]+sign*((half-.2)*inward[i]+(half-.3)*across[i]) for i in range(3)]
                    with self.subTest(prefix=prefix, corner=(side, vertical_side), sign=sign):
                        self.assertEqual(int(graph.member(vertical, point))+int(graph.member(rail, point)), 1)
                cavity = [node[i]+2.*inward[i]+3.*across[i] for i in range(3)]
                self.assertFalse(graph.member(vertical, cavity))
                self.assertFalse(graph.member(rail, cavity))
        left = graph.items['outer_frame.left.0001']['properties']['assemblyFrame.member']
        self.assertEqual(left['start'], [19., 0., 19.])
        self.assertEqual(left['end'], [19., 0., 1781.])

    def test_multi_perimeter_turns_are_complementary_and_posts_stop_below_rails(self):
        for face in ('two', 'three', 'five'):
            graph = DisplayGraph(self.display(faceType=face, accessDoorEnabled=False))
            rails = [item for item in graph.items.values() if item['key'].startswith('outer_frame.')
                     and not item['key'].startswith('outer_frame.vertical.')]
            corner_count = 0
            for index, a in enumerate(rails):
                ma = a['properties']['assemblyFrame.member']
                for b in rails[index+1:]:
                    mb = b['properties']['assemblyFrame.member']
                    for end in ('start', 'end'):
                        for mate_end in ('start', 'end'):
                            node = ma[end]
                            if math.dist(node, mb[mate_end]) > 1.e-7:
                                continue
                            da = unit(sub(ma['end' if end == 'start' else 'start'], node))
                            db = unit(sub(mb['end' if mate_end == 'start' else 'start'], node))
                            half = a['properties']['tubeDesigner.profile']['depth']/2
                            # At a horizontal turn the top shell, not only
                            # bounding boxes, must have one unique owner.
                            for sign in (-1., 1.):
                                point = [node[i]+sign*(3.*da[i]+4.*db[i])+(half-.2 if i == 2 else 0.)
                                         for i in range(3)]
                                self.assertEqual(int(graph.member(a['key'], point))+int(graph.member(b['key'], point)), 1)
                            corner_count += 1
            self.assertEqual(corner_count, {'two': 2, 'three': 4, 'five': 8}[face])
            for post in (item for item in graph.items.values() if item['key'].startswith('outer_frame.vertical.')):
                member = post['properties']['assemblyFrame.member']
                for end in ('start', 'end'):
                    node = member[end]
                    inward = unit(sub(member['end' if end == 'start' else 'start'], node))
                    across = member['sectionFrame']['xAxis']
                    point = [node[i]+18.8*inward[i]+18.7*across[i] for i in range(3)]
                    self.assertFalse(graph.member(post['key'], point))

    def test_partial_single_frame_open_ends_are_not_trimmed(self):
        for layout in ('left_right', 'top_bottom'):
            display = self.display(faceType='single', frameLayout=layout, accessDoorEnabled=False)
            graph = DisplayGraph(display)
            for key, item in graph.items.items():
                if not key.startswith('outer_frame.'):
                    continue
                member = item['properties']['assemblyFrame.member']
                self.assertAlmostEqual(member['axisLength'], 1800. if layout == 'left_right' else 1200.)
                across = member['sectionFrame']['xAxis']
                point = [member['start'][i]+.1*member['sectionFrame']['zAxis'][i]+18.7*across[i] for i in range(3)]
                self.assertTrue(graph.member(key, point))

    def test_all_manufacturing_choices_keep_display_and_member_facts_identical(self):
        for face in ('single', 'two', 'three', 'five'):
            baseline = self.display(faceType=face, accessDoorEnabled=True)
            modes = ('segment_weld', 'plane_v_notch', 'spatial_v_notch') if face in ('two', 'three') else ('segment_weld', 'plane_v_notch')
            for mode in modes:
                for tool, bottom in (('system:v-notch-sharp', 'rounded'), ('system:edge-arc-groove', 'sharp')):
                    with self.subTest(face=face, mode=mode, tool=tool):
                        other = self.display(faceType=face, accessDoorEnabled=True, frameManufacturingMode=mode,
                            outerFrameGrooveTool=tool, outerFrameVGrooveBottomStrategy=bottom,
                            outerFrameVGrooveRoundRadius=4., outerFrameBendKFactor=.62, outerFrameFoldBridge=.8,
                            doorFrameJoinType='v_groove_90:tool_library', doorLeafFrameJoinType='butt_90',
                            doorFrameGrooveTool='system:edge-arc-groove', foldedPostJoint='tabs')
                        self.assertEqual(other, baseline)


if __name__ == '__main__':
    unittest.main()
