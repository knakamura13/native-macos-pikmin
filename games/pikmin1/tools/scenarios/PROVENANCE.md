# Provenance of the disc-free scenario fixtures (#190)

The public CI fixtures are the generated stand-in tree and the code that writes it:
`libs/nectar_assets/src/standin_*.cpp`, the headers in
`libs/nectar_assets/include/nectar_assets`, `standin_generator.cpp` and the
`standin_*_stage.cpp` files in this folder. Each value in them is one of:

- **Source default**: a value the game source sets (a parameter's constructor
  default, a file path, pane tag, pane type or event ID the code names). Cited by
  file and function where it is used.
- **Authored**: a deterministic value chosen for the fixture (round numbers on an
  own grid), with a comment stating the code contract it keeps.

Nothing measured or copied from a retail disc is a fixture payload. Exact checks
against retail data are opt-in local lanes (last section). The README's
"Fidelity rule (#190)" states the policy.

## Fixture input classes

| Class | Where | Source | Disposition |
|---|---|---|---|
| Cinema `.cin` paths | `standin_cinema.cpp` | Code literals (`moviePlayer.cpp` `movieTitles` and translation tables) | Kept |
| Cinema key event types | `standin_cinema.cpp` | Code enums (`Interface.h` `MOVIECMD_*`, `demoEvent.cpp` `DemoEvent*`, `ogTutorial.h` `TUT_*`) | Kept: only the keys the game logic acts on |
| Cinema `.dsk` scene names | `standin_cinema.cpp` | Authored (`cinemas/standin/demoNN_a.dsk`) | Replaced retail scene names |
| Cinema frames, scene lengths, loop windows | `standin_cinema.cpp` | Authored on a 10-frame grid; each movie's comment states its contract (key inside a cut, key order, loop last) | Replaced retail-shaped schedules (demo17, 20, 26, 36, 46, 47, 73, 77, 78); `CleanupDayEnd` dropped from demo46 (no code path needs it) |
| Discovery cone frames (demo04/05/06) | `standin_cinema.cpp` | Tied to the stand-in Onion boot profile's `KEY_Action0` | Contract documented; values follow that profile (Onion boot item, see below) |
| Cinema cameras and lights | `standin_cinema.cpp` `standInScene` | Authored | Kept |
| `CinePlayerFlags` per movie | `standin_cinema.cpp` | NonGameMovie where the code needs time stopped (results backdrops); Localized where a movie is placed at a target; HideNavi/NaviNoAI authored | Kept |
| Layout file names | `standin_ui.cpp` | Code literals (`"screen/blo/<name>.blo"`, `bloFile_*` tables) | Kept |
| Pane tags and types | `standin_ui.cpp` | Code `search(.., true)` calls, cited per layout | Kept |
| HUD and Y-menu pane geometry | `standin_layout.h`, `standin_ui.cpp` | Authored on a 16-pixel grid; constraints from code (play10 drawn at x 0, play_day at the wide offset, play_day's y 300 split, `SunMove` between `stat` and `goal`, `ogDrawLR` arrow pivots) | Replaced; relations checked by `static_assert`, `checkStandInHudLayout`, `blo_writer_test` |
| Other layout geometry, text, colours | `standin_ui.cpp` | Authored; course names are game source literals (`DrawWorldMap.h`, `game.cpp`) | Kept |
| Texture names | `standin_textures.cpp` | Code literals (`loadTexExp` call sites, `effectMgr.cpp`, `DrawContainer.h`) | Kept |
| Particle names | `standin_pcr_names.cpp` | Code literals (`effectMgr.cpp`, `effectMgr2D.cpp`), drift-tested | Kept |
| Particle payload and version tag | `pcr.h`, `standin_ui.cpp` `inertPcrParticle` | Authored (inert particle, tag `"0-00"`; the port does not check the tag) | Tag replaced |
| Audio sequence archive | `standin_audio.cpp` | The game's BARC table compiled in (`src/jaudio/pikiseq.c`); no samples or banks | Kept |
| Parameter files (`parms/gamePrms.bin`, `aiconst.bin`, `demo.bin`, `cunit.bin`, `naviMgr.bin`, `pikiMgr.bin`) | `standin_generator.cpp` | Empty tagged lists: every parameter is its source default | Kept; `check_synthetic_parameters.py` enforces |
| `parms/key.bin` | `standin_generator.cpp` | Authored: six button masks, the Classic bindings (source default except extract on A) | Kept |
| `stages/stages.ini` entries | `standin_generator.cpp` | `practice.ini`, `stage1.ini` are code literals (`game.cpp` `setStage`); `course2.ini`..`course4.ini` authored | Retail-only names replaced |
| Stage `.ini` text | `standin_generator.cpp`, `standin_objects_stage.cpp` | `map_file` and `day_multiply 1.0`, the source default | Kept |
| Tutorial stage (`stages/practice/`) | `standin_practice_stage.{h,cpp}` | Authored placements, facings, push distance, route grid; object names and the push box's pushers and speed are code names and source defaults | Kept |
| Onion stage and world-map courses | `standin_objects_stage.{h,cpp}` | Authored positions; the `next` ownership gate is the game's (`GenObjectItem::birth`) | Kept |
| Open allowlist | `standin-open-allowlist.txt` | Policy file; paths are code literals or code-composed names | Kept; comments state what the chain needs, not retail contents |
| Golden screenshots | `golden/<os>/*.png` | Renders of the stand-in tree (outputs, not inputs) | macOS `boot_smoke_tick100.png` regenerated locally for the authored HUD; Linux and Windows `boot_smoke_tick100.png` need a hosted refresh (`refresh_goldens.sh`) |
| Synthetic throw expectations | `check_throw_flight.py` `SYNTHETIC_EXPECTED` | The port's own results on the authored fixtures | Kept (public CI profile) |
| Models, model collision, object animation profiles, world models, `tekipara`, sandbox, Pikmin rig | `standin_models.cpp`, `standin_objects.cpp`, `standin_object_profiles.cpp`, `standin_anims.*`, `standin_world*.cpp`, `standin_sandbox.cpp`, `piki_rig.h` | Other #190 workers | Filled in at integration from their reports |
| Onion boot profile and the frames tied to it | `standin_object_profiles.cpp`; demo04/05/06 cone frames; `check_onion_boot_negative.py` frame 194; `cinema_writer_test` demo05 `+240` relation | Lead's Onion boot item | Open until that profile is authored |

## Local lanes (opt-in; they read user-supplied data and skip, or do nothing, without it)

The CTest entries carry the `game-assets` label and `SKIP_RETURN_CODE 77`; the
`NECTAR_DISC_DATA` halves run inside the public writer tests only when the variable is set.

| Lane | Input | Notes |
|---|---|---|
| `scenario_<name>_<rate>_disc`, `scenario_onion_day3*_disc`, `scenario_*_onion_boot_*_disc` | `PIKMIN_SCENARIO_DATA_DIR` | The scenarios on a real install |
| `scenario_throw_fidelity_disc` (`run_disc_throw_fidelity.py`, `check_throw_flight.py --profile disc`) | `PIKMIN_SCENARIO_DATA_DIR` | Exact throw calibration |
| `check_ballistic.py --disc-reference`, `run_disc_ballistic_reference.py` | a disc run's logs | Not run by the public workflow |
| `scenario_standin_manifest` (`nectar_standin_generator --manifest-from-env`) | `PIKMIN_BOOT_MANIFEST`, made locally with `make_boot_manifest.py` | The tree against a boot open-log; `kManifestAllowlist` gives each tolerated miss a reason |
| `nectar_blo_writer_test`, `nectar_pcr_writer_test` disc halves | `NECTAR_DISC_DATA` | Tag-table and parser checks on the user's files |
| `nectar_mod_writer_test`, `nectar_anim_writer_test`, `nectar_standin_objects_test` manifest argument | a local boot manifest passed by hand | No longer passed by CTest |

Local-lane tools that store no disc data and may ship with a local-only note:
`make_boot_manifest.py`, `measure_onion_boot.py`, `run_disc_throw_fidelity.py`,
`run_disc_ballistic_reference.py`, `record_rng_provenance.py`.

## Excluded from the upstream package

Not deleted from this branch; the lead builds the package without them.

| Path | Why |
|---|---|
| `games/pikmin1/tools/scenarios/boot-manifest.txt` | A retail boot open-log (file-name table); the manifest lane generates one locally |
| `games/pikmin1/tools/scenarios/evidence/ballistic-gc-2026-11-06.json` | Retail executable traces, disc image hashes and offsets; read only by `check_ballistic.py --disc-reference`, which then needs the file supplied locally |
| `games/pikmin1/tools/scenarios/evidence/ballistic-hash-impact-205b7e393.csv` | Fork-specific generated build artifact |
| `games/pikmin1/tools/scenarios/standin-onion-boot.md` | Disc paths, hashes and per-frame retail measurements (still linked from the README `*_onion_boot` row and `coverage/milestones.md`; replace those links with the authored Onion boot note) |
| `games/pikmin1/tools/scenarios/standin-aiconst.md` | Table of retail parameter values |
| `games/pikmin1/tools/scenarios/standin-piki-parms.md` | Table of retail parameter values, personal decision record |
| `games/pikmin1/tools/scenarios/verification-log.md` | Retail parameter values, personal decision record (still cited by `scenario_container_menu.cpp`, `pc_scenario.cpp`, `standin-audio.md`, `standin-overlay-experiment.md`, `standin-texture-experiments.md`) |
| `games/pikmin1/tools/scenarios/standin-k-investigation.md` | Retail run tables and gate fits (still cited by `scenario_throw_release.cpp` and `standin-particle-rng.md`) |
| `games/pikmin1/tools/scenarios/ballistic-motion.md` | Retail executable hash and measured peaks, unless rewritten to the synthetic contract (ballistic item) |
| `games/pikmin1/tools/scenarios/check_onion_boot_negative.py` | Patches the retail route-effect frame 194, until the Onion boot profile is authored |
| `games/pikmin1/pc_port/scenarios/scenario_onion_day3.cpp` `Disc` rows (retail stage placements) | Read them from the user's `default.gen` at run time instead |
| `libs/nectar_assets/art/`, `.github/workflows/standin-art-export.yml` | Already deleted on this branch; keep deleted |
| This branch's commit history | Earlier commits hold the retail-derived values; package from a squashed tree |

## Docs still to revise at integration

- `standin-objects.md`, sections "What the disc was read for" through "World
  models": they describe the pre-#190 object, world and animation fixtures.
- `README.md`, the `blue_onion_boot` / `yellow_onion_boot` / `red_onion_boot` row
  and the `onion_skip` notes: follow the authored Onion boot profile.
- `scenario_onion_skip.cpp` header ("the measured Onion boot", "the disc's
  360-frame duration"): follows the Onion boot profile.
- `scenario_tutorial_day1.cpp` `kEngineCarriers` (20): equals the stand-in `ust5`
  carrier count in `pelMgr.bin` (`standin_objects.cpp`); change both together.
