#!/usr/bin/env bash
# One-command local install for Linux (mirrors scripts/install.ps1).
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

echo "[hyperlite] Using Python: $PYTHON"
"$PYTHON" -m pip install -U pip setuptools wheel

if [[ "$EDITABLE" -eq 1 ]]; then
	"$PYTHON" -m pip install -e . --force-reinstall
else
	"$PYTHON" -m pip install . --force-reinstall
fi

"$PYTHON" -c "import hyperlite; e=hyperlite.Engine(64,64,'cpu','t',present='headless'); print('OK', hyperlite.__file__, e.backend_name())"
