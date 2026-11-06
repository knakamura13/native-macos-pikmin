# Stand-in Ship Parts Implementation Plan

> Execute in this chat using superpowers:executing-plans; Kyle authorized implementation and a PR.

**Goal:** Give the engine and radar roles distinct original art with reproducible sources and gameplay captures.

**Architecture:** Add two collections in one Blender source and bind the exported meshes to the existing model specs. Use the existing stand-in generator and unlit pellet draw. A separate showcase scenario positions the real pellet objects and a fixed camera without changing gameplay.

**Tech Stack:** Blender 5.2.3 Python, C++17, CMake/CTest, existing PNG verifier and GitHub Actions.

### Task 1: Asset invariants and baseline
- Build the unmodified scenario tree in `build/ship-parts` with scenarios ON, JAudio OFF, launcher OFF.
- Run asset tests and record baseline captures/hash logs.
- In `libs/nectar_assets/tests/mod_writer_test.cpp`, require coloured engine/radar meshes while retaining spec parents, bounds and collision text. Run and observe failure on the primitive models.

### Task 2: Author and export
- Create `libs/nectar_assets/art/authoring/ship_parts.py`: brass fan cartridge, amber core, teal instrument base and open compass hoop, soft bevelled geometry. Save `blender/ship_parts.blend`.
- Run the existing exporter to regenerate `art/export/` and add two bindings in `src/standin_art.cpp`.
- Run `check_export.py` and the asset tests. Document rebuild and design in art README/direction.

### Task 3: Capture and ship
- Add `scenario_ship_parts_showcase.cpp`, factory/list/CMake registration and a PNG check using existing capture/verification tooling. Show both real objects at gameplay distance; verify nonblank and palette regions.
- Capture the same scene with base assets and new assets; attach before/after images as PR evidence outside code diff.
- Run full `ctest -E _disc`, compare existing state hashes, inspect existing goldens. Push without force and open fork PR against `knakamura/disc-free-gameplay-develop`, with limited acceptance claims and AI disclaimer.
- Monitor three-OS CI and export checks, fix failures within scope, and report any unmet acceptance honestly.
