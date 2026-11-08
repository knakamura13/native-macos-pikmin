"""Original Hollowmere equipment, built only from Blender primitives.

Main Engine (soto5): brass fan cartridge, amber hub, four rounded radial vanes.
Radar (fuzoku1): teal pebble base, upright brass compass hoop and amber pointer.
No imported meshes, textures or reference images. Geometry only: existing rigs,
collision spheres, carry scales and motion timing belong to the C++ specs.

Rebuild sources:
  Blender --background --factory-startup --python libs/nectar_assets/art/authoring/ship_parts.py
Then export with art/export_standins.py and rebuild nectar_standin_generator.
"""
import math
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import nectar_art as na


def rounded_box(coll, name, mat, size, loc, rot=(0, 0, 0)):
    ob = na.box(coll, name, mat, size, loc, rot=rot)
    na.bevel(ob, min(size) * 0.22, segments=2)
    return ob


def main():
    na.reset()
    brass = na.material("part_brass", "#C9A04A", gloss=0.4)
    dark = na.material("part_iron", "#3D4248", gloss=0.15)
    teal = na.material("part_teal", "#23808A", gloss=0.25)
    amber = na.material("part_amber", "#FFA630", emit=0.85, gloss=0.3)
    cream = na.material("part_pointer", "#FFE2A0", emit=0.7)

    # The 20-joint rig is preserved, with draw geometry on its root only.
    # A low wide cartridge: its fan is visible from the elevated gameplay camera.
    c = na.model_collection("part_engine", joints=20, ground=True)
    na.sphere(c, "housing", dark, 10.0, (0, 0, 3.0), scale=(1, 1, 0.42), segments=16, rings=6)
    na.torus(c, "cartridge_rim", brass, 9.0, 1.4, (0, 0, 5.0), major_segments=16, minor_segments=6)
    for i in range(4):
        a = math.pi * i / 2
        rounded_box(c, f"vane{i}", brass, (7.0, 3.0, 1.4),
                    (4.4 * math.cos(a), 4.4 * math.sin(a), 5.4), rot=(0, 0, a + 0.25))
        # A short connector on each side makes the equipment read as a cartridge.
        rounded_box(c, f"connector{i}", teal, (3.0, 4.0, 3.0),
                    (10.0 * math.cos(a), 10.0 * math.sin(a), 2.0), rot=(0, 0, a))
    na.sphere(c, "amber_hub", amber, 3.1, (0, 0, 6.0), scale=(1, 1, 0.8), segments=12, rings=6)
    na.torus(c, "hub_collar", brass, 3.25, 0.5, (0, 0, 5.5), major_segments=12, minor_segments=4)

    # Root-bound artwork, five original spec joints. No antenna or dish: a
    # compass hoop around a broad pointer is distinct from the fan cartridge.
    c = na.model_collection("part_radar", joints=5, ground=True)
    na.sphere(c, "instrument_base", teal, 6.5, (0, 0, 2.0), scale=(1, 0.85, 0.5), segments=16, rings=6)
    na.torus(c, "base_band", brass, 5.4, 0.7, (0, 0, 3.5), major_segments=16, minor_segments=5)
    rounded_box(c, "hoop_mount", dark, (5, 3, 3), (0, 0, 4))
    na.torus(c, "compass_hoop", brass, 5.2, 0.8, (0, 0, 9.5),
             rot=(math.pi / 2, 0, 0), major_segments=20, minor_segments=6)
    rounded_box(c, "pointer", cream, (2, 1.8, 7.2), (0, -0.2, 9.5), rot=(0, -0.5, 0))
    na.sphere(c, "pointer_pivot", amber, 1.6, (0, -1.2, 9.5), segments=10, rings=5)
    for i in range(4):
        a = math.pi * i / 2
        na.sphere(c, f"compass_mark{i}", cream, 0.7,
                  (5.2 * math.cos(a), -0.3, 9.5 + 5.2 * math.sin(a)), segments=6, rings=4)
    na.save("ship_parts")


main()
