# SPDX-License-Identifier: MIT
"""Verify export geometry and the case revision used by the download page."""

from io import BytesIO
import math
import hashlib
from pathlib import Path
import tempfile
import unittest
import xml.etree.ElementTree as xml
import zipfile

import export_3mf as exporter


ROOT = Path(__file__).resolve().parents[2]
REVISION = ROOT / "hardware" / "step" / "rev1"
NAMESPACE = {"m": exporter.MODEL_NAMESPACE}


def tetrahedron(name="Slab"):
    return exporter.Mesh(name, [(0, 0, 0), (2, 0, 0), (0, 3, 0), (0, 0, 4)],
                         [(0, 2, 1), (0, 1, 3), (0, 3, 2), (1, 2, 3)], 4)


def read_meshes(path):
    with zipfile.ZipFile(path) as archive:
        model = xml.fromstring(archive.read("3D/3dmodel.model"))
        names = sorted(archive.namelist())
        # Only a file of several parts carries the slicer's description of them, and nothing else.
        assert names in (["3D/3dmodel.model", "[Content_Types].xml", "_rels/.rels"],
                         ["3D/3dmodel.model", "Metadata/model_settings.config", "[Content_Types].xml", "_rels/.rels"])
    meshes = {}
    for obj in model.findall("m:resources/m:object", NAMESPACE):
        mesh = obj.find("m:mesh", NAMESPACE)
        if mesh is None:
            continue
        vertices = [tuple(float(vertex.attrib[axis]) for axis in ("x", "y", "z"))
                    for vertex in mesh.findall("m:vertices/m:vertex", NAMESPACE)]
        triangles = [tuple(int(triangle.attrib[axis]) for axis in ("v1", "v2", "v3"))
                     for triangle in mesh.findall("m:triangles/m:triangle", NAMESPACE)]
        name = obj.attrib["name"]
        meshes[name] = exporter.Mesh(name, vertices, triangles)
    return model, meshes


def bounds(mesh):
    return tuple(tuple(operation(point[axis] for point in mesh.vertices) for axis in range(3))
                 for operation in (min, max))


class GeometryTests(unittest.TestCase):
    def test_rotations_keep_handedness_and_signed_volume(self):
        for name in exporter.EXPECTED_SOLIDS:
            mesh = tetrahedron(name)
            rotated = exporter.Mesh(name, [exporter.rotate_for_print(name, p) for p in mesh.vertices], mesh.triangles, 4)
            self.assertAlmostEqual(exporter.validate_mesh(rotated), 4, places=10)
        sine, cosine = math.sin(math.pi / 12), math.cos(math.pi / 12)
        for name in ("Slab", "Logo", "CornerBracket"):
            self.assertAlmostEqual(exporter.rotate_for_print(name, (0, -cosine, sine))[2], -1)
        self.assertEqual(exporter.rotate_for_print("Foot", (0, 0, 1)), (0, 0, -1))
        for number in range(1, 5):
            self.assertEqual(exporter.rotate_for_print(f"Cap{number}", (0, 0, 1)), (0, 0, -1))

    def test_group_placement_preserves_logo_registration(self):
        frame = tetrahedron("Slab")
        logo = tetrahedron("Logo")
        logo.vertices = [(x + 7, y + 11, z + 13) for x, y, z in logo.vertices]
        groups = exporter.oriented_groups({"Slab": frame, "Logo": logo}, (("Slab", "Logo"),))
        placed_frame, placed_logo = groups[0]
        expected_offset = exporter.rotate_for_print("Slab", (7, 11, 13))
        for first, second in zip(placed_frame.vertices, placed_logo.vertices):
            for axis in range(3):
                self.assertAlmostEqual(second[axis] - first[axis], expected_offset[axis])
        self.assertAlmostEqual(min(p[2] for mesh in groups[0] for p in mesh.vertices), 0)
        model = xml.fromstring(exporter.model_xml(groups, "test"))
        self.assertEqual(len(model.findall("m:build/m:item", NAMESPACE)), 1)
        self.assertEqual(len(model.findall("m:resources/m:object/m:components/m:component", NAMESPACE)), 2)

    def test_rejects_holes_and_reversed_faces(self):
        for triangles in (tetrahedron().triangles[:-1], [(0, 1, 2)] + tetrahedron().triangles[1:]):
            mesh = tetrahedron()
            mesh.triangles = triangles
            with self.assertRaises(ValueError):
                exporter.validate_mesh(mesh)

    def test_parts_are_named_and_the_lettering_has_its_own_filament(self):
        groups = [[tetrahedron("Slab"), tetrahedron("Logo")], [tetrahedron("BackPlate")]]
        model = xml.fromstring(exporter.model_xml(groups, "test"))
        metadata = {item.attrib["name"]: item.text for item in model.findall("m:metadata", NAMESPACE)}
        # Without this line the slicer does not read the description of the parts at all.
        self.assertTrue(metadata["Application"].startswith("BambuStudio-"))
        parts = xml.fromstring(exporter.parts_xml(groups))
        objects = parts.findall("object")
        self.assertEqual([obj.attrib["id"] for obj in objects], ["3"])  # the object of the two meshes 1 and 2
        described = {part.attrib["id"]: {item.attrib["key"]: item.attrib["value"] for item in part.findall("metadata")}
                     for part in objects[0].findall("part")}
        self.assertEqual(described, {"1": {"name": "Frame", "extruder": "1"}, "2": {"name": "Logo", "extruder": "2"}})
        # The ids are the model's own: the object and its components.
        composite = [obj for obj in model.findall("m:resources/m:object", NAMESPACE) if obj.find("m:components", NAMESPACE) is not None]
        self.assertEqual(composite[0].attrib["id"], "3")
        self.assertEqual([c.attrib["objectid"] for c in composite[0].find("m:components", NAMESPACE)], ["1", "2"])
        with zipfile.ZipFile(BytesIO(exporter.package_3mf(exporter.model_xml(groups, "test"), exporter.parts_xml(groups)))) as archive:
            self.assertIn("Metadata/model_settings.config", archive.namelist())
        # A file of single bodies says nothing of a slicer.
        single = [[tetrahedron("BackPlate")]]
        self.assertIsNone(exporter.parts_xml(single))
        plain = xml.fromstring(exporter.model_xml(single, "test"))
        self.assertNotIn("Application", {item.attrib["name"] for item in plain.findall("m:metadata", NAMESPACE)})

    def test_archive_is_reproducible_and_has_no_settings(self):
        model = exporter.model_xml([[tetrahedron()]], "test")
        first = exporter.package_3mf(model)
        self.assertEqual(first, exporter.package_3mf(model))
        with zipfile.ZipFile(BytesIO(first)) as archive:
            self.assertEqual(len(archive.namelist()), 3)
            for entry in archive.infolist():
                self.assertEqual(entry.date_time, exporter.ZIP_TIMESTAMP)


class RevisionTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        bodies = exporter.load_step_bodies(REVISION / "perinkpheral.step")
        cls.meshes = {name: exporter.tessellate_body(name, bodies[name]) for name in exporter.EXPECTED_SOLIDS}
        cls.temporary = tempfile.TemporaryDirectory()
        cls.output = Path(cls.temporary.name)
        cls.step_sha256 = hashlib.sha256((REVISION / "perinkpheral.step").read_bytes()).hexdigest()
        exporter.export_files(cls.meshes, cls.output, cls.step_sha256)

    @classmethod
    def tearDownClass(cls):
        cls.temporary.cleanup()

    def test_real_step_export_matches_committed_geometry(self):
        observed = set()
        for filename, groups in exporter.FILES:
            model, generated = read_meshes(self.output / filename)
            committed_model, committed = read_meshes(REVISION / filename)
            metadata = {item.attrib["name"]: item.text for item in committed_model.findall("m:metadata", NAMESPACE)}
            self.assertEqual(metadata.get("Description"), f"STEP SHA256: {self.step_sha256}")
            expected_names = {name for group in groups for name in group}
            self.assertEqual(set(generated), expected_names)
            self.assertEqual(set(committed), expected_names)
            self.assertEqual(model.attrib["unit"], "millimeter")
            self.assertEqual(len(model.findall("m:build/m:item", NAMESPACE)), len(groups))
            for candidate in (model, committed_model):
                objects = {obj.attrib["id"]: obj for obj in candidate.findall("m:resources/m:object", NAMESPACE)}
                actual_groups = []
                for item in candidate.findall("m:build/m:item", NAMESPACE):
                    obj = objects[item.attrib["objectid"]]
                    components = obj.findall("m:components/m:component", NAMESPACE)
                    actual_groups.append(tuple(objects[child.attrib["objectid"]].attrib["name"] for child in components)
                                         if components else (obj.attrib["name"],))
                self.assertEqual(tuple(actual_groups), groups)
            observed.update(generated)
            for name, mesh in generated.items():
                expected_volume = self.meshes[name].cad_volume_mm3
                for candidate in (mesh, committed[name]):
                    candidate.cad_volume_mm3 = expected_volume
                    exporter.validate_mesh(candidate)
                for first, second in zip(bounds(mesh), bounds(committed[name])):
                    for coordinate, expected in zip(first, second):
                        self.assertAlmostEqual(coordinate, expected, delta=.01)
            for group in groups:
                self.assertAlmostEqual(min(p[2] for name in group for p in generated[name].vertices), 0, places=6)
        self.assertEqual(observed, set(exporter.EXPECTED_SOLIDS))

    def test_download_archive_contains_exact_individual_files(self):
        filenames = [filename for filename, _ in exporter.FILES]
        with zipfile.ZipFile(REVISION / "perinkpheral-print-3mf.zip") as archive:
            self.assertEqual(archive.namelist(), filenames)
            for filename in filenames:
                self.assertEqual(archive.read(filename), (REVISION / filename).read_bytes())

    def test_repeated_output_is_identical(self):
        before = {path.name: path.read_bytes() for path in self.output.iterdir()}
        exporter.export_files(self.meshes, self.output, self.step_sha256)
        self.assertEqual(before, {path.name: path.read_bytes() for path in self.output.iterdir()})


if __name__ == "__main__":
    unittest.main()
