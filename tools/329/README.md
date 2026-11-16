# #329 ASan harness (scratch, not committed)

- asan-harness.patch: fork #207 wall-bomb sweep (WALL_BOMB_SWEEP=1, from
  ../wall-bomb-probe) + ASan support: AyuStack arena poisoning (ayuStack.cpp)
  and plain-calloc operator new under ASan (sysNew.cpp). Needs
  games/pikmin1/pc_port/pc_wall_bomb_sweep.h (copy here).
- run_asan.sh <outdir> [stage=2 Forest Navel] [timeout]: throwaway save,
  disc assets symlinked read-only, --coop-direct, infinite day, audio dummy.
- Build: cmake -S games/pikmin1 -B build-asan -DCMAKE_BUILD_TYPE=RelWithDebInfo
  -DPIKMIN_BUILD_LAUNCHER=OFF -DPIKMIN_BUILD_TESTS=OFF -DPIKMIN_DEBUG_KEYS=ON
  C/CXX flags "-fsanitize=address -fno-omit-frame-pointer -g", linker -fsanitize=address.
- runs/r2, runs/r4-unfixed: heap-buffer-overflow WRITE at soundMgr.cpp:695
  (SeSystem::createEvent) after ~800 bombs / ~134 s.
- runs/r3-fix, runs/r5-fix: fixed build, full 3120 bombs, no ASan report.
