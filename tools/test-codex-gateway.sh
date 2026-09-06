#!/usr/bin/env bash
set -euo pipefail
repo_root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
gateway_python="${VIBE_GATEWAY_PYTHON:-python3}"
cd "${repo_root}"
"${gateway_python}" - <<'PY'
import sys
if sys.version_info < (3, 11):
    raise SystemExit("Codex gateway tests require Python 3.11 or newer")
try:
    import aiohttp
    import aiortc
    import av
except ImportError:
    raise SystemExit("Install services/codex_gateway/requirements.txt in a venv, then set VIBE_GATEWAY_PYTHON to its Python executable") from None
PY
PYTHONDONTWRITEBYTECODE=1 "${gateway_python}" -m unittest discover \
    -s services/codex_gateway/tests -p 'test_*.py' -v
