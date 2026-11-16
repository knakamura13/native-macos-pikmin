#!/bin/bash
# #329 harness launcher: ASan build + fork #207 wall-bomb sweep on one stage.
# Usage: run_asan.sh <outdir> [stageIndex=2 (Forest Navel)] [timeoutSec=1500]
# Throwaway save + settings copy; disc assets are symlinked read-only.
S="$1"; N="${2:-2}"; T="${3:-1500}"
BIN="${BIN:-$HOME/OpenNectar-builds.noindex/329-asan/build-asan/bin/nectar}"
ASSETS="$HOME/.local/share/pikmin-native/assets"
W="$S/work"; mkdir -p "$W/save"
ln -sfn "$ASSETS" "$W/assets"
if [ ! -f "$W/pikmin_settings.conf" ]; then
  sed -e 's/^windowWidth = .*/windowWidth = 960/' -e 's/^windowHeight = .*/windowHeight = 540/' \
      -e 's/^displayMode = .*/displayMode = 0/' -e 's/^infiniteDay = .*/infiniteDay = 1/' \
      -e 's/^renderScale = .*/renderScale = 1/' -e 's/^hideOlimarText = .*/hideOlimarText = 1/' \
      "$HOME/.local/share/pikmin-native/pikmin_settings.conf" > "$W/pikmin_settings.conf"
fi
cd "$W"
export ASAN_OPTIONS="halt_on_error=1:abort_on_error=0:log_path=$S/asan:detect_leaks=0:alloc_dealloc_mismatch=0:detect_container_overflow=0:malloc_context_size=40:handle_segv=1:handle_sigbus=1:symbolize=1:print_module_map=1${ASAN_EXTRA:+:$ASAN_EXTRA}"
SDL_AUDIODRIVER=dummy NECTAR_SAVE_DIR="$W/save" OPEN_NECTAR_NO_DISCORD=1 \
WALL_BOMB_SWEEP=1 WALL_BOMB_SWEEP_STAGE="$N" WALL_BOMB_SWEEP_OUT="$S" \
  "$BIN" --coop-direct > "$W/stdout.log" 2> "$W/stderr.log" &
PID=$!
echo "$PID" > "$S/pid"
echo "started pid $PID at $(date)"
for ((i=0;i<T;i++)); do
  if ! kill -0 $PID 2>/dev/null; then wait $PID; echo "exited rc=$? after ${i}s at $(date)"; exit 0; fi
  sleep 1
done
echo "timeout after ${T}s; killing our pid $PID"; kill $PID; sleep 3; kill -9 $PID 2>/dev/null; exit 1
