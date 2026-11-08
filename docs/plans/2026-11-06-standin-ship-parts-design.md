# Hollowmere ship parts

Approved by Kyle in this chat on 2026-11-06. Focused slice of fork issue #115.

The Main Engine role is a squat brass fan cartridge with a glowing amber core;
the radar role is a rounded teal instrument base with a brass compass hoop.
Their broad silhouettes and the lantern barge palette identify them as equipment.
All geometry is authored from Blender primitives; no imported assets or references.

Sources: `art/authoring/ship_parts.py`, `art/blender/ship_parts.blend` and the
existing `art/export_standins.py` exporter under `libs/nectar_assets`.
Bind only soto5 (20 joints) and fuzoku1 (5 joints). Keep skeletons, joint bounds,
collision text, gameplay parameters and animations unchanged.

Validation: asset round trips and collision invariants; reproducible export;
a fixed-camera scenario showing both parts at 640x480, with before/after captures;
existing engine/radar delivery scenarios; complete `ctest -E _disc` and three-OS CI.
The six existing golden views should be unchanged; refresh only if an intentional
visual difference is observed, preserving CI provenance.
