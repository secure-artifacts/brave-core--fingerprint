#!/bin/bash
set -euo pipefail

# brave 的 devEngines 强制 node >=24.16.0 <25.0.0 且包管理器为 pnpm >=11.9.0。
# 系统默认 node 是 v26、homebrew pnpm 是 9.x，都不满足，所以这里锁定 nvm 的 24.16.0
# 并直接调用底层构建脚本，绕过包管理器的 devEngines 校验。
export PATH="$HOME/.nvm/versions/node/v24.16.0/bin:$PATH"

DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
if [ -f "$DIR/src/brave/package.json" ]; then
  BRAVE="$DIR/src/brave"
elif [ -f "$DIR/../package.json" ]; then
  BRAVE="$(cd "$DIR/.." && pwd)"
else
  echo "未找到 brave-core 目录，请把本脚本放在 gclient 根目录或 src/brave/workspace/ 下" >&2
  exit 1
fi

cd "$BRAVE"
export PATH="$BRAVE/node_modules/.bin:$PATH"

LOG="$BRAVE/../../build-$(date +%Y%m%d-%H%M%S).log"
echo "日志: $LOG"

# 这些 --gn 参数必须显式传：FingerprintStaticPGO 是自定义 build config，
# 不走 brave 的 Release 分支，默认会把 dcheck_always_on 打开、关掉 PGO 和 stripping。
# 缺任何一个都会与 2026-08-20 的发布构建配置不一致，导致对象缓存全部失效并重新全量编译。
node ./build/commands/scripts/commands.js build FingerprintStaticPGO \
  --target_arch=arm64 \
  --gn chrome_pgo_phase:2 \
  --gn use_thin_lto:true \
  --gn enable_stripping:true \
  --gn enable_dsyms:true \
  --gn optimize_for_size:false \
  --gn symbol_level:1 \
  --gn v8_symbol_level:0 \
  --gn use_partition_alloc_as_malloc:true \
  --gn dcheck_always_on:false \
  --gn enable_expensive_dchecks:false \
  --gn enable_updater:false \
  2>&1 | tee "$LOG"

echo "产物: $BRAVE/../out/FingerprintStaticPGO_arm64/Fingerprint Browser.app"
