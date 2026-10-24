#!/usr/bin/env bash
set -euo pipefail
cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.."
love_binary=${LOVE_BIN:-}
if [[ -z "$love_binary" ]]; then
  if [[ -x artifacts/love12/install/bin/love ]]; then love_binary=./artifacts/love12/install/bin/love
  else love_binary=love; fi
fi
exec "$love_binary" . "$@"
