#!/usr/bin/env bash
# One-command local install for Linux and macOS (mirrors scripts/install.ps1).
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

PYTHON="${PYTHON:-python3}"
EDITABLE=0

while [[ $# -gt 0 ]]; do
	case "$1" in
		--python) PYTHON="$2"; shift 2 ;;
		--editable|-e) EDITABLE=1; shift ;;
		*) echo "Unknown arg: $1" >&2; exit 1 ;;
	esac
done

if [[ "$(uname -s)" == "Darwin" ]]; then
	echo "[hyperlite] macOS: needs Xcode CLT + CMake. Optional: brew install libomp"
	if ! command -v cmake >/dev/null 2>&1; then
		echo "cmake not on PATH. Install: brew install cmake" >&2
		exit 1
	fi
	if ! command -v clang++ >/dev/null 2>&1; then
		echo "clang++ not on PATH. Install Xcode Command Line Tools: xcode-select --install" >&2
		exit 1
	fi
	echo "[hyperlite] xcode-select: $(xcode-select -p 2>/dev/null || echo missing)"
	echo "[hyperlite] clang++: $(clang++ --version 2>/dev/null | head -n 1)"
fi

echo "[hyperlite] Using Python: $PYTHON"
if ! "$PYTHON" -c "import sysconfig, pathlib, sys; p=pathlib.Path(sysconfig.get_path('include'))/'Python.h'; sys.exit(0 if p.is_file() else 1)"; then
	echo "Python.h not found for $PYTHON." >&2
	echo "On macOS do not use /usr/bin/python3. Install Homebrew python:" >&2
	echo "  brew install python && python3 -m venv .venv && .venv/bin/pip install ." >&2
	exit 1
fi
"$PYTHON" -m pip install -U pip setuptools wheel

if [[ "$EDITABLE" -eq 1 ]]; then
	"$PYTHON" -m pip install -e . --force-reinstall
else
	"$PYTHON" -m pip install . --force-reinstall
fi

"$PYTHON" -c "import hyperlite; e=hyperlite.Engine(64,64,'cpu','t',present='headless'); print('OK', hyperlite.__file__, e.backend_name())"
