#!/bin/sh
# Compiles the console's own install.c for the host and runs the tests against a
# throwaway directory. Nothing here touches an SD card, an emulator, or the real
# payload.
#
#   sh test/run.sh [work-dir]

set -e

here=$(cd "$(dirname "$0")" && pwd)
work=${1:-${TMPDIR:-/tmp}/vp-install-test}

rm -rf "$work"
mkdir -p "$work/sd"
work=$(cd "$work" && { pwd -W 2>/dev/null || pwd; })

# The roots go through a generated header rather than -D. MSYS rewrites anything
# on the command line that looks like a path, and it eats the inner quotes of
# -DFOO='"C:/x"', which surfaces as an unterminated-string error pointing at the
# wrong line.
cat > "$work/test_config.h" <<EOF
#pragma once
#define VP_PAYLOAD_ROOT "$work/payload"
#define SD_ROOT         "$work/sd/"
#define SD_ROOT_DIR     "$work/sd"
EOF

gcc -std=c11 -Wall -Wextra -Werror -O1 \
    -I"$here/../source" -I"$work" -include test_config.h \
    "$here/test_install.c" "$here/../source/install.c" \
    -o "$work/test_install"

"$work/test_install"
