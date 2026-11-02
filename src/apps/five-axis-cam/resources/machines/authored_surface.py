"""Build the exterior of each authored rigid link before splitting by material.

This is an offline asset build dependency, never part of the application runtime.
Boolean union removes buried faces and coincident contact faces; retaining input
face provenance keeps the exposed surfaces in their authored material groups.
"""
from collections import defaultdict

import manifold3d as manifold
import numpy as np


class AuthoredSurface:
    def __init__(self):
        self.links = defaultdict(list)
        self.colors = {}

    def add(self, link, color, triangles):
        vertices, indices, lookup = [], [], {}
        for triangle in triangles:
            face = []
            for point in triangle:
                key = tuple(round(value, 9) for value in point)
                if key not in lookup:
                    lookup[key] = len(vertices)
                    vertices.append(key)
                face.append(lookup[key])
            indices.append(face)
        vertices = np.array(vertices, dtype=np.float64)
        indices = np.array(indices, dtype=np.uint32)
        points = vertices[indices]
        volume6 = np.einsum('ij,ij->i', points[:, 0],
                            np.cross(points[:, 1], points[:, 2])).sum()
        # Descending lathe profiles have the opposite winding to box rings.
        if volume6 < 0:
            indices = indices[:, ::-1].copy()
        original = manifold.Manifold.reserve_ids(1)
        self.colors[original] = color
        mesh = manifold.Mesh64(vertices, indices.astype(np.uint64),
                               run_index=np.array([0, indices.size], dtype=np.uint64),
                               run_original_id=np.array([original], dtype=np.uint32))
        solid = manifold.Manifold(mesh)
        if solid.status() != manifold.Error.NoError or solid.is_empty():
            raise ValueError(f'Invalid authored solid: {link}/{color}: {solid.status()}')
        self.links[link].append(solid)

    def finish(self):
        parts = {}
        for link, solids in self.links.items():
            solid = manifold.Manifold.batch_boolean(solids, manifold.OpType.Add)
            if solid.status() != manifold.Error.NoError or solid.is_empty():
                raise ValueError(f'Cannot construct exterior of {link}: {solid.status()}')
            mesh = solid.to_mesh64()
            # Weld in the actual STL storage precision before emitting faces.
            # Boolean intersections can leave sub-float32 slivers; when two
            # vertices become identical in metres that face has no surface.
            vertices = (mesh.vert_properties[:, :3] / 1000).astype(np.float32).astype(np.float64) * 1000
            groups = defaultdict(list)
            for run, original in enumerate(mesh.run_original_id):
                start, end = mesh.run_index[run:run + 2]
                for indices in mesh.tri_verts[start // 3:end // 3]:
                    triangle = tuple(tuple(float(v) for v in vertices[i]) for i in indices)
                    if len(set(triangle)) == 3:
                        groups[self.colors[original]].append(triangle)
            parts[link] = groups
        return parts
