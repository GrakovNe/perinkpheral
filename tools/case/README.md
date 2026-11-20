# Case print files

`export_3mf.py` reads the named solids from the case STEP and writes the seven
print files and their ZIP. It runs without Fusion or a slicer.

Use Python 3.12 in a virtual environment, from the repository root:

```sh
python3.12 -m venv build/case-venv
build/case-venv/bin/python -m pip install -r tools/case/requirements.txt
build/case-venv/bin/python tools/case/export_3mf.py \
    hardware/step/rev1/perinkpheral.step --out hardware/step/rev1
```

On Windows, use `build/case-venv/Scripts/python` for the environment's Python.

The source must retain the body names and assembly coordinates from the case
model. The exporter checks all 27 solids: 12 letters in `Logo`, plus the other
15 bodies. Unknown or missing parts are errors, not silently omitted files.

The frame and logo share one placement and remain two parts of one object.
The frame is face down, the back panel outer side down, the base cover top down,
the bottom bottom down, and the bracket and button caps flange down. The six
optional TPU feet are in a separate file, washer down. Every object starts at
Z=0; neighbouring objects are 5 mm apart. The files contain millimetre geometry
and grouping, with no printer, filament or slicing profiles.

The frame's file also names its two parts for slicers of the Bambu Studio family
and gives the lettering the second filament (`Metadata/model_settings.config`):
`Frame` on filament 1, `Logo` on filament 2. Such a slicer reads that only from a
file that claims to come from one, so the model says `Application: BambuStudio-...`.
Keep the lettering a part of the frame's object. Split into an object of its own,
it gets the first layer's elephant foot compensation apart from the frame, and a gap
of about a nozzle opens around every letter (measured with Bambu Studio 2.9,
compensation 0.15 mm: 28 mm2 of the lettering's first layer left uncovered against 7).
Other slicers ignore both the claim and the description.

OCCT meshes the original surfaces with an absolute linear deflection of
0.001 mm and an angular deflection of 2 degrees. No mesh decimation is used.
Closed edges, winding, triangle areas and volume are checked before output. The mesh volume
must agree with the STEP volume within 0.1%. If OCCT cannot mesh an analytic
seam, a pristine copy is converted to an exact rational NURBS representation;
validity, volume and bounds must still agree before it is meshed. ZIP entries use fixed timestamps;
repeated exports with the same CAD dependency produce identical files. Each 3MF
records its source STEP SHA-256, which CI checks to catch stale exports.

Run the tests, including the real revision's STEP and committed meshes:

```sh
build/case-venv/bin/python -m unittest discover -s tools/case -p 'test_*.py'
```

The tests compare geometry, orientation, quantity and registration rather than
requiring identical triangle ordering across operating systems.
