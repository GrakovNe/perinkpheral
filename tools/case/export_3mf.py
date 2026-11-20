#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Export the named perINKpheral STEP bodies as print-oriented 3MF files."""

from __future__ import annotations

import argparse
from collections import Counter
from dataclasses import dataclass
from io import BytesIO
import json
import hashlib
import math
from pathlib import Path
from tempfile import TemporaryDirectory
import xml.etree.ElementTree as xml
import zipfile

LINEAR_DEFLECTION_MM = 0.001
ANGULAR_DEFLECTION_RADIANS = math.radians(2)
MAX_VOLUME_ERROR = 0.001
VERTEX_DECIMALS = 7
PART_GAP_MM = 5.0
MODEL_NAMESPACE = "http://schemas.microsoft.com/3dmanufacturing/core/2015/02"
ZIP_TIMESTAMP = (1980, 1, 1, 0, 0, 0)
EXPECTED_SOLIDS = {
    "Slab": 1, "Logo": 12, "BackPlate": 1, "Foot": 1,
    "FootCover": 1, "CornerBracket": 1,
    **{f"Cap{number}": 1 for number in range(1, 5)},
    **{f"Foot{number}": 1 for number in range(1, 7)},
}
FILES = (
    ("01-frame-with-logo.3mf", (("Slab", "Logo"),)),
    ("02-back-panel.3mf", (("BackPlate",),)),
    ("03-base-cover.3mf", (("Foot",),)),
    ("04-bottom.3mf", (("FootCover",),)),
    ("05-corner-bracket.3mf", (("CornerBracket",),)),
    ("06-button-caps-x4.3mf", tuple((f"Cap{number}",) for number in range(1, 5))),
    ("07-optional-tpu-feet-x6.3mf", tuple((f"Foot{number}",) for number in range(1, 7))),
)


@dataclass
class Mesh:
    name: str
    vertices: list[tuple[float, float, float]]
    triangles: list[tuple[int, int, int]]
    cad_volume_mm3: float = 0.0


def rotate_for_print(name, point):
    x, y, z = point
    sine, cosine = math.sin(math.radians(15)), math.cos(math.radians(15))
    # These are rotations, not reflections: preserve winding and handed parts.
    if name in ("Slab", "Logo", "CornerBracket"):
        return x, -y * sine - z * cosine, y * cosine - z * sine
    if name == "BackPlate":
        return x, y * sine + z * cosine, -y * cosine + z * sine
    if name == "Foot":
        return x, -y, -z
    return x, y, z


def signed_volume(mesh):
    origin = mesh.vertices[0]
    terms = []
    for triangle in mesh.triangles:
        first, second, third = (
            tuple(mesh.vertices[index][axis] - origin[axis] for axis in range(3))
            for index in triangle
        )
        terms.append(
            first[0] * (second[1] * third[2] - second[2] * third[1])
            + first[1] * (second[2] * third[0] - second[0] * third[2])
            + first[2] * (second[0] * third[1] - second[1] * third[0])
        )
    return math.fsum(terms) / 6


def validate_mesh(mesh):
    if not mesh.vertices or not mesh.triangles:
        raise ValueError(f"{mesh.name}: empty mesh")
    if not all(math.isfinite(coordinate) for point in mesh.vertices for coordinate in point):
        raise ValueError(f"{mesh.name}: non-finite vertex")
    edges = Counter()
    directions = Counter()
    for triangle in mesh.triangles:
        if len(set(triangle)) != 3 or not all(0 <= index < len(mesh.vertices) for index in triangle):
            raise ValueError(f"{mesh.name}: invalid triangle {triangle}")
        first, second, third = triangle
        first_edge = tuple(mesh.vertices[second][axis] - mesh.vertices[first][axis] for axis in range(3))
        second_edge = tuple(mesh.vertices[third][axis] - mesh.vertices[first][axis] for axis in range(3))
        cross = (
            first_edge[1] * second_edge[2] - first_edge[2] * second_edge[1],
            first_edge[2] * second_edge[0] - first_edge[0] * second_edge[2],
            first_edge[0] * second_edge[1] - first_edge[1] * second_edge[0],
        )
        if sum(value * value for value in cross) <= 1e-24:
            raise ValueError(f"{mesh.name}: zero-area triangle")
        for start, end in ((first, second), (second, third), (third, first)):
            edge = tuple(sorted((start, end)))
            edges[edge] += 1
            directions[edge] += 1 if start < end else -1
    if any(count != 2 for count in edges.values()) or any(directions.values()):
        raise ValueError(f"{mesh.name}: mesh is not closed with consistent winding")
    volume = signed_volume(mesh)
    if volume <= 0:
        raise ValueError(f"{mesh.name}: inverted or zero-volume mesh")
    if mesh.cad_volume_mm3 and abs(volume / mesh.cad_volume_mm3 - 1) > MAX_VOLUME_ERROR:
        raise ValueError(f"{mesh.name}: mesh volume differs from STEP by more than {MAX_VOLUME_ERROR:.1%}")
    return volume


def load_step_bodies(path):
    from OCP.IFSelect import IFSelect_RetDone
    from OCP.Interface import Interface_Static
    from OCP.STEPCAFControl import STEPCAFControl_Reader
    from OCP.TCollection import TCollection_ExtendedString
    from OCP.TDataStd import TDataStd_Name
    from OCP.TDF import TDF_AttributeIterator, TDF_ChildIterator, TDF_LabelSequence
    from OCP.TDocStd import TDocStd_Document
    from OCP.TopAbs import TopAbs_SOLID
    from OCP.TopExp import TopExp
    from OCP.TopTools import TopTools_IndexedMapOfShape
    from OCP.XCAFDoc import XCAFDoc_DocumentTool

    reader = STEPCAFControl_Reader()
    reader.SetNameMode(True)
    # Fusion names solids within a component, rather than separate products.
    if not Interface_Static.SetIVal_s("read.stepcaf.subshapes.name", 1):
        raise ValueError("CAD library cannot import STEP body names")
    if reader.ReadFile(str(path)) != IFSelect_RetDone:
        raise ValueError(f"Cannot read STEP: {path}")
    reader.Reader().SetSystemLengthUnit(1.0)
    document = TDocStd_Document(TCollection_ExtendedString("XmlXCAF"))
    if not reader.Transfer(document):
        raise ValueError(f"Cannot transfer STEP shapes: {path}")
    shape_tool = XCAFDoc_DocumentTool.ShapeTool_s(document.Main())
    roots = TDF_LabelSequence()
    shape_tool.GetFreeShapes(roots)
    all_solids = TopTools_IndexedMapOfShape()
    named_solids = TopTools_IndexedMapOfShape()
    bodies = {}
    for index in range(1, roots.Length() + 1):
        shape = shape_tool.GetShape_s(roots.Value(index))
        if not shape.IsNull():
            TopExp.MapShapes_s(shape, TopAbs_SOLID, all_solids)

    def visit(label):
        shape = shape_tool.GetShape_s(label)
        if not shape.IsNull() and shape.ShapeType() == TopAbs_SOLID:
            attributes = TDF_AttributeIterator(label)
            name = ""
            while attributes.More():
                attribute = attributes.Value()
                if isinstance(attribute, TDataStd_Name):
                    name = attribute.Get().ToExtString()
                attributes.Next()
            if not all_solids.Contains(shape):
                raise ValueError(f"{name}: export STEP bodies in assembly coordinates")
            if named_solids.Contains(shape):
                raise ValueError(f"{name}: repeated named STEP solid")
            named_solids.Add(shape)
            bodies.setdefault(name, []).append(shape)
            return
        children = TDF_ChildIterator(label)
        while children.More():
            visit(children.Value())
            children.Next()

    for index in range(1, roots.Length() + 1):
        visit(roots.Value(index))
    counts = {name: len(shapes) for name, shapes in bodies.items()}
    if counts != EXPECTED_SOLIDS or named_solids.Extent() != all_solids.Extent():
        raise ValueError(f"Expected the 16 named case bodies (12 Logo solids); found {counts}")
    return bodies


def tessellate_body(name, shapes):
    from OCP.BRep import BRep_Tool
    from OCP.Bnd import Bnd_Box
    from OCP.BRepBndLib import BRepBndLib
    from OCP.BRepBuilderAPI import BRepBuilderAPI_Copy, BRepBuilderAPI_NurbsConvert
    from OCP.BRepCheck import BRepCheck_Analyzer
    from OCP.BRepGProp import BRepGProp
    from OCP.BRepMesh import BRepMesh_IncrementalMesh
    from OCP.BRepTools import BRepTools
    from OCP.GProp import GProp_GProps
    from OCP.TopAbs import TopAbs_FACE, TopAbs_REVERSED
    from OCP.TopExp import TopExp_Explorer
    from OCP.TopLoc import TopLoc_Location
    from OCP.TopoDS import TopoDS

    vertices, triangles, vertex_ids = [], [], {}
    cad_volume = 0.0
    for shape in shapes:
        if not BRepCheck_Analyzer(shape).IsValid():
            raise ValueError(f"{name}: invalid STEP solid")
        properties = GProp_GProps()
        BRepGProp.VolumeProperties_s(shape, properties, Eps=1e-9)
        cad_volume += properties.Mass()
        # A failed OCCT mesh can update edge tolerances; preserve a pristine fallback.
        source_copy = BRepBuilderAPI_Copy(shape, True, False)
        BRepTools.Clean_s(shape)
        mesher = BRepMesh_IncrementalMesh(
            shape, LINEAR_DEFLECTION_MM, False, ANGULAR_DEFLECTION_RADIANS, False
        )
        if not mesher.IsDone():
            raise ValueError(f"{name}: tessellation failed")
        faces = TopExp_Explorer(shape, TopAbs_FACE)
        missing_face = False
        while faces.More():
            face = TopoDS.Face_s(faces.Current())
            missing_face |= BRep_Tool.Triangulation_s(face, TopLoc_Location()) is None
            faces.Next()
        if missing_face:
            # OCCT can reject a periodic cone seam. Rational NURBS represent
            # the same surface exactly, avoiding that analytic-face mesh path.
            conversion = BRepBuilderAPI_NurbsConvert(source_copy.Shape(), True)
            converted = conversion.Shape()
            if converted.IsNull() or not BRepCheck_Analyzer(converted).IsValid():
                raise ValueError(f"{name}: exact surface conversion failed")
            converted_properties = GProp_GProps()
            BRepGProp.VolumeProperties_s(converted, converted_properties, Eps=1e-9)
            if abs(converted_properties.Mass() - properties.Mass()) > max(.00001, properties.Mass() * .000001):
                raise ValueError(f"{name}: surface conversion changed volume")
            original_box, converted_box = Bnd_Box(), Bnd_Box()
            BRepBndLib.AddOptimal_s(shape, original_box, False, False)
            BRepBndLib.AddOptimal_s(converted, converted_box, False, False)
            if max(abs(first - second) for first, second in zip(original_box.Get(), converted_box.Get())) > .00001:
                raise ValueError(f"{name}: surface conversion changed bounds")
            shape = converted
            BRepTools.Clean_s(shape)
            mesher = BRepMesh_IncrementalMesh(
                shape, LINEAR_DEFLECTION_MM, False, ANGULAR_DEFLECTION_RADIANS, False
            )
            if not mesher.IsDone():
                raise ValueError(f"{name}: tessellation failed after exact surface conversion")
        faces = TopExp_Explorer(shape, TopAbs_FACE)
        while faces.More():
            face = TopoDS.Face_s(faces.Current())
            location = TopLoc_Location()
            triangulation = BRep_Tool.Triangulation_s(face, location)
            if triangulation is None:
                raise ValueError(f"{name}: face has no triangulation")
            node_ids = []
            for index in range(1, triangulation.NbNodes() + 1):
                point = triangulation.Node(index).Transformed(location.Transformation())
                # Adjacent faces share an edge, but OCCT stores their nodes separately.
                coordinate = tuple(round(value, VERTEX_DECIMALS) for value in (point.X(), point.Y(), point.Z()))
                if coordinate not in vertex_ids:
                    vertex_ids[coordinate] = len(vertices)
                    vertices.append(coordinate)
                node_ids.append(vertex_ids[coordinate])
            for index in range(1, triangulation.NbTriangles() + 1):
                triangle = [node_ids[node - 1] for node in triangulation.Triangle(index).Get()]
                if face.Orientation() == TopAbs_REVERSED:
                    triangle[1], triangle[2] = triangle[2], triangle[1]
                triangles.append(tuple(triangle))
            faces.Next()
    mesh = Mesh(name, vertices, triangles, cad_volume)
    validate_mesh(mesh)
    return mesh


def oriented_groups(meshes, groups):
    positioned = []
    cursor_x = PART_GAP_MM
    for names in groups:
        rotated = {name: [rotate_for_print(name, point) for point in meshes[name].vertices] for name in names}
        minimum = [min(point[axis] for points in rotated.values() for point in points) for axis in range(3)]
        maximum = [max(point[axis] for points in rotated.values() for point in points) for axis in range(3)]
        group = []
        for name in names:
            vertices = [tuple(
                point[axis] - minimum[axis] + (cursor_x, PART_GAP_MM, 0)[axis]
                for axis in range(3)
            ) for point in rotated[name]]
            group.append(Mesh(name, vertices, meshes[name].triangles, meshes[name].cad_volume_mm3))
        positioned.append(group)
        cursor_x += maximum[0] - minimum[0] + PART_GAP_MM
    return positioned


# A slicer of the Bambu Studio family reads its own description of an object's parts - their
# names and which filament each takes - only from a file that says it came from one. Without it
# the parts arrive nameless on one filament, and the lettering split into an object of its own
# to colour it gets the first layer's elephant foot compensation apart from the frame: a gap of
# a nozzle around every letter. Other slicers skip both the claim and the description.
SLICER_APPLICATION = "BambuStudio-02.00.00.00"
PART_FILAMENTS = {"Slab": ("Frame", 1), "Logo": ("Logo", 2)}


def model_xml(groups, title, step_sha256=None):
    xml.register_namespace("", MODEL_NAMESPACE)
    def element(name):
        return f"{{{MODEL_NAMESPACE}}}{name}"
    model = xml.Element(element("model"), {"unit": "millimeter", "{http://www.w3.org/XML/1998/namespace}lang": "en-US"})
    if any(len(group) > 1 for group in groups):
        xml.SubElement(model, element("metadata"), {"name": "Application"}).text = SLICER_APPLICATION
        xml.SubElement(model, element("metadata"), {"name": "BambuStudio:3mfVersion"}).text = "1"
    xml.SubElement(model, element("metadata"), {"name": "Title"}).text = title
    if step_sha256:
        xml.SubElement(model, element("metadata"), {"name": "Description"}).text = f"STEP SHA256: {step_sha256}"
    resources = xml.SubElement(model, element("resources"))
    build = xml.SubElement(model, element("build"))
    next_id = 1
    for group in groups:
        object_ids = []
        for mesh in group:
            object_id = next_id
            next_id += 1
            obj = xml.SubElement(resources, element("object"), {"id": str(object_id), "name": mesh.name, "type": "model"})
            mesh_element = xml.SubElement(obj, element("mesh"))
            vertices = xml.SubElement(mesh_element, element("vertices"))
            for point in mesh.vertices:
                xml.SubElement(vertices, element("vertex"), dict(zip(("x", "y", "z"), (f"{value:.9f}" for value in point))))
            triangles = xml.SubElement(mesh_element, element("triangles"))
            for triangle in mesh.triangles:
                xml.SubElement(triangles, element("triangle"), dict(zip(("v1", "v2", "v3"), map(str, triangle))))
            object_ids.append(object_id)
        if len(object_ids) > 1:
            object_id = next_id
            next_id += 1
            obj = xml.SubElement(resources, element("object"), {"id": str(object_id), "name": "Frame with logo", "type": "model"})
            components = xml.SubElement(obj, element("components"))
            for child_id in object_ids:
                xml.SubElement(components, element("component"), {"objectid": str(child_id)})
        xml.SubElement(build, element("item"), {"objectid": str(object_id)})
    return xml.tostring(model, encoding="utf-8", xml_declaration=True)


def parts_xml(groups):
    """The slicer's description of the objects made of several parts; None when there is none."""
    config = xml.Element("config")
    next_id = 1
    for group in groups:
        part_ids = list(range(next_id, next_id + len(group)))
        next_id += len(group)
        if len(group) < 2:
            continue
        obj = xml.SubElement(config, "object", {"id": str(next_id)})
        next_id += 1
        xml.SubElement(obj, "metadata", {"key": "name", "value": "Frame with logo"})
        xml.SubElement(obj, "metadata", {"key": "extruder", "value": "1"})
        for part_id, mesh in zip(part_ids, group):
            name, filament = PART_FILAMENTS[mesh.name]
            part = xml.SubElement(obj, "part", {"id": str(part_id), "subtype": "normal_part"})
            xml.SubElement(part, "metadata", {"key": "name", "value": name})
            xml.SubElement(part, "metadata", {"key": "extruder", "value": str(filament)})
    if len(config) == 0:
        return None
    return xml.tostring(config, encoding="utf-8", xml_declaration=True)


def zip_bytes(entries):
    output = BytesIO()
    with zipfile.ZipFile(output, "w") as archive:
        for name, content in entries:
            info = zipfile.ZipInfo(name, ZIP_TIMESTAMP)
            info.compress_type = zipfile.ZIP_DEFLATED
            info.create_system = 3
            info.external_attr = 0o100644 << 16
            archive.writestr(info, content, compresslevel=9)
    return output.getvalue()


def package_3mf(model, parts=None):
    content_types = b'''<?xml version="1.0" encoding="UTF-8"?><Types xmlns="http://schemas.openxmlformats.org/package/2006/content-types"><Default Extension="rels" ContentType="application/vnd.openxmlformats-package.relationships+xml"/><Default Extension="model" ContentType="application/vnd.ms-package.3dmanufacturing-3dmodel+xml"/></Types>'''
    relationships = b'''<?xml version="1.0" encoding="UTF-8"?><Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships"><Relationship Target="/3D/3dmodel.model" Id="rel0" Type="http://schemas.microsoft.com/3dmanufacturing/2013/01/3dmodel"/></Relationships>'''
    entries = [("[Content_Types].xml", content_types), ("_rels/.rels", relationships), ("3D/3dmodel.model", model)]
    if parts is not None:
        entries.append(("Metadata/model_settings.config", parts))
    return zip_bytes(entries)


def export_files(meshes, output, step_sha256=None):
    files = []
    for filename, groups in FILES:
        oriented = oriented_groups(meshes, groups)
        model = model_xml(oriented, filename.removesuffix(".3mf"), step_sha256)
        files.append((filename, package_3mf(model, parts_xml(oriented))))
    files.append(("perinkpheral-print-3mf.zip", zip_bytes(files)))
    output.mkdir(parents=True, exist_ok=True)
    with TemporaryDirectory(prefix=".case-export-", dir=output) as temporary:
        for filename, content in files:
            Path(temporary, filename).write_bytes(content)
        for filename, _ in files:
            Path(temporary, filename).replace(output / filename)
    return [filename for filename, _ in files]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("step", type=Path, help="perINKpheral STEP in assembly coordinates, with original body names")
    parser.add_argument("--out", type=Path, required=True, help="directory for the seven 3MF files and download ZIP")
    args = parser.parse_args()
    try:
        bodies = load_step_bodies(args.step)
        meshes = {name: tessellate_body(name, bodies[name]) for name in EXPECTED_SOLIDS}
        filenames = export_files(meshes, args.out, hashlib.sha256(args.step.read_bytes()).hexdigest())
    except ImportError as error:
        parser.exit(1, f"Missing CAD dependency: {error}. Install tools/case/requirements.txt.\n")
    except (ValueError, RuntimeError, OSError) as error:
        parser.exit(1, f"Export failed: {error}\n")
    print(json.dumps({"files": filenames, "mesh_tolerance_mm": LINEAR_DEFLECTION_MM, "bodies": [
        {"name": mesh.name, "triangles": len(mesh.triangles), "cad_volume_mm3": mesh.cad_volume_mm3,
         "mesh_volume_mm3": signed_volume(mesh)} for mesh in meshes.values()
    ]}, indent=2))


if __name__ == "__main__":
    main()
