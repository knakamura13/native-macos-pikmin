# Consistent menu input implementation plan

**Goal:** Apply the approved Enter/Space/click accept and Escape/Backspace cancel behavior throughout Pikmin 1 menus, preserving gameplay bindings.

**Architecture:** Sample independent menu actions alongside the gameplay pad. Legacy menus use explicit menu queries; port dialogs use the same action state. Desktop pointer events feed the existing menu hit tests, with a single-consumer action/tap and release gating across dialogs. Arrow keys and WASD navigate. Shift has no menu action. Text entry retains editing keys.

**Tech stack:** C++, SDL2, CMake/CTest, native macOS game build.

## 1. Input regression coverage
- Add `games/pikmin1/pc_port/pc_menu_input_test.cpp` covering accept/cancel equivalence, one action per press, short clicks, modal transitions, navigation, and physical Start independent of keyboard accept.
- Run a standalone compiler invocation first and observe missing behavior; add the smallest shared state implementation and rerun.
- Register the test and implementation in `games/pikmin1/CMakeLists.txt`.

## 2. Window and legacy menu integration
- Add the SDL sampling/desktop pointer adapter in `pc_window.cpp` and `pc_menu_input.cpp`.
- Add explicit menu queries to `include/Controller.h`; retain ordinary gameplay queries.
- Migrate game menu queries in `src/plugPikiOgawa`, `src/plugPikiYamashita`, and the title controller to the menu queries.
- Reuse existing tap hit tests on desktop; fix title, Captain, file-select and world-map transitions, and keep hardware Start as world-map pause.

## 3. Port dialogs and documentation
- Route accept/cancel helpers and modal release gates in `settings/pc_settings.cpp` through shared actions.
- Update `games/pikmin1/README.md` with menu controls and distinguish gameplay bindings.

## 4. Verification and delivery
- Build and run focused CTest targets plus the full game in the isolated worktree.
- Launch a scratch binary with isolated configuration/save data; exercise the startup-to-file-select flow and map if a scratch save is available. Record limitations explicitly.
- Review the diff, remove temporary instrumentation, commit the focused change, and report the branch, checks and remaining runtime coverage.
