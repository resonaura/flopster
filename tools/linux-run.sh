#!/usr/bin/env bash
# =============================================================================
#  Flopster — Linux Quick Run
#  Rebuilds (if needed), installs Standalone, then launches it immediately.
#  Useful for rapid test cycles without opening a DAW.
#
#  Usage: tools/linux-run.sh [--rebuild] [--full] [--help]
#
#  --full   Also install VST3 (default: standalone only)
#
#  by Shiru & Resonaura
# =============================================================================
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

RED='\033[0;31m'; GRN='\033[0;32m'; YLW='\033[1;33m'
CYN='\033[0;36m'; BLD='\033[1m'; NC='\033[0m'

info() { echo -e "${CYN}  → $*${NC}"; }
ok()   { echo -e "${GRN}  ✅  $*${NC}"; }
die()  { echo -e "${RED}  ❌  $*${NC}" >&2; exit 1; }

REBUILD=0
FULL=0

for arg in "$@"; do
  case "$arg" in
    --rebuild|-r) REBUILD=1 ;;
    --full|-f)    FULL=1 ;;
    --help|-h)
      echo ""
      echo "  Usage: $0 [--rebuild] [--full]"
      echo ""
      echo "    --rebuild   Force a clean rebuild before installing and running"
      echo "    --full      Also install VST3 (default: standalone only)"
      echo "    --help      Show this help"
      echo ""
      exit 0
      ;;
    *) echo -e "${YLW}  ⚠️   Unknown flag: $arg (ignored)${NC}" ;;
  esac
done

echo ""
echo -e "${BLD}${CYN}╔══════════════════════════════════════════════╗${NC}"
echo -e "${BLD}${CYN}║   🚀  Flopster — Rebuild & Run               ║${NC}"
echo -e "${BLD}${CYN}║       by Shiru & Resonaura                   ║${NC}"
echo -e "${BLD}${CYN}╚══════════════════════════════════════════════╝${NC}"
echo ""

BUILD_ARGS=()
[[ "$REBUILD" -eq 1 ]] && BUILD_ARGS+=(--rebuild)

# ── Step 1: Build ─────────────────────────────────────────────────────────────
info "Building..."
bash "$SCRIPT_DIR/linux-build.sh" ${BUILD_ARGS[@]+"${BUILD_ARGS[@]}"}

# ── Step 2: Install ───────────────────────────────────────────────────────────
if [[ "$FULL" -eq 1 ]]; then
  info "Installing VST3 + Standalone..."
  bash "$SCRIPT_DIR/linux-install.sh" ${BUILD_ARGS[@]+"${BUILD_ARGS[@]}"}
else
  info "Installing Standalone..."
  bash "$SCRIPT_DIR/linux-install.sh" --only standalone ${BUILD_ARGS[@]+"${BUILD_ARGS[@]}"}
fi

# ── Step 3: Launch ────────────────────────────────────────────────────────────
BUILD="$ROOT/build"
APP="$BUILD/Flopster_artefacts/Release/Standalone/Flopster"

if [ ! -f "$APP" ]; then
  die "Flopster binary not found at $APP — install step may have failed."
fi

# Kill any existing instance so we always get a fresh launch
if pgrep -x "Flopster" &>/dev/null; then
  info "Killing existing Flopster instance..."
  pkill -x "Flopster" || true
  sleep 0.5
fi

ok "Launching $APP"
echo ""
"$APP" &
