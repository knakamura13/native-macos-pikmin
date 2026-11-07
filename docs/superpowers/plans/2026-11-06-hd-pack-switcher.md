# HD Pack Switcher Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Independently select and remove installed HD texture variants.

**Architecture:** Header-only filesystem catalog shared by loader, settings CLI and launcher. Stable folder IDs preserve legacy settings; managed variant installation avoids overwriting the other resolution.

**Tech Stack:** C++17, std::filesystem, SDL2, Dear ImGui, CMake/CTest.

## Global Constraints
- Apply choices on next game start; no live GPU-cache replacement.
- Deletion must never follow symlinks or leave Load/Textures.
- Every interactive control has a hand cursor; toast is bounded to two lines.
- No ISOs or saves are modified.

### Task 1: Catalog and safety
Files: pc_port/mods/texture_pack_catalog.h and texture_pack_catalog_test.cpp.
- [x] Write fixture tests: distinct variants, legacy marker naming and case-insensitive selection, missing selections, exact-size removal, traversal/root/ancestor/descendant symlink rejection.
- [x] Compile with `c++ -std=c++17 games/pikmin1/pc_port/mods/texture_pack_catalog_test.cpp -o /workspace/pack-catalog-test`; observe failure before implementation.
- [x] Implement catalog, selection resolver and validated deletion; rerun executable for PASS.

### Task 2: Installation and game selection
Files: gl/pc_texpack.cpp; mods/pc_hd_model_convert.{cpp,h}; settings/pc_settings.cpp; launcher/texture_pack_download.{cpp,h}; launcher_main.cpp.
- [x] Use catalog IDs for discovery and loader paths; retain GameID values.
- [x] Add optional reserved variant ID to ZIP installer and CLI; reject unsafe entries, stage writes separately, commit only a complete pack.
- [x] Pass selected variant from downloader; never remove the other installed resolution.
- [x] Give in-game menu explicit Off plus friendly names/sizes, with next-start help.
- [x] Add CLI deletion with success/error, active-choice reset and catalog JSON metadata.

### Task 3: Launcher and verification
Files: launcher/settings_model.{h,cpp}; launcher_gui.cpp; toast_layout.h; CMakeLists.txt.
- [x] Parse metadata while accepting legacy string-only dumps.
- [x] Add Delete buttons, path/size tooltip and confirmation; display operation result as toast.
- [x] Add width-aware bounded two-line toast layout and tests.
- [x] Configure/build game and launcher; run CTest and disposable CLI checks.
- [x] Render launcher at narrow/wide sizes; inspect images and hand cursors.
- [x] Review diff and record untested platforms; commit changes. Publication requires the owner’s authorization, not this plan.
