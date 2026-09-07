#!/usr/bin/env bash
# Local development environment for this Brave Core checkout.
# Usage from Git Bash:
#   cd /c/Users/tomgr/Desktop/brave-core--fingerprint
#   source ./dev-env.sh
#   pnpm run help
#
# Brave build scripts expect this repo at C:\Users\tomgr\src\brave.
# A Windows junction has been created there pointing back to this Desktop checkout,
# so commands run through that junction while files remain on Desktop.

export BRAVE_CORE_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
export BRAVE_RUN_DIR="/c/Users/tomgr/src/brave"
export BRAVE_NODE_DIR="$BRAVE_CORE_ROOT/.devtools/node-v24.16.0-win-x64"
export PATH="$BRAVE_NODE_DIR:$PATH"
export vs2022_install="C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools"
unset PYTHONPATH

pnpm() {
  (cd "$BRAVE_RUN_DIR" && "$BRAVE_NODE_DIR/node" "$BRAVE_NODE_DIR/node_modules/pnpm/bin/pnpm.cjs" "$@")
}

brave-cd() {
  cd "$BRAVE_RUN_DIR"
}

node --version
pnpm --version
