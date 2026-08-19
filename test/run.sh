#!/bin/sh
# Compiles the console's own source for the host and runs the tests. Nothing
# here touches an SD card, an emulator, or a real Hotswap folder, and the
# mk7-mod tree is only ever read.
#
#   sh test/run.sh [work-dir]
#
# Both suites need the ROM extract. That is not a shortcoming of the tests - the
# app's whole job is assembling the track out of the player's own game, so a run
# without a game would be exercising a code path that does not ship. The suite
# skips loudly rather than passing on nothing.
#
#   MK7_MOD_ROOT=/path/to/mk7-mod sh test/run.sh

set -e

here=$(cd "$(dirname "$0")" && pwd)
work=${1:-${TMPDIR:-/tmp}/vp-test}
mk7mod=${MK7_MOD_ROOT:-"$here/../../../fuck u/mk7-mod"}

if [ ! -d "$mk7mod/extract/romfs/Course" ]; then
    echo "SKIPPED: no ROM extract at $mk7mod"
    echo "         set MK7_MOD_ROOT to the mk7-mod directory to run the tests."
    exit 0
fi

rm -rf "$work"
mkdir -p "$work/sd" "$work/ref" "$work/out"
work=$(cd "$work" && { pwd -W 2>/dev/null || pwd; })
mk7abs=$(cd "$mk7mod" && { pwd -W 2>/dev/null || pwd; })
payload=$(cd "$here/../romfs/mod" && { pwd -W 2>/dev/null || pwd; })

python "$here/prep_reference.py" "$mk7abs" "$work/ref"
echo

# The roots go through a generated header rather than -D. MSYS rewrites anything
# on the command line that looks like a path, and it eats the inner quotes of
# -DFOO='"C:/x"', which surfaces as an unterminated-string error pointing at the
# wrong line.
#
# The payload root is the real one, and the game root is the real extract. The
# assembly is the whole product; a stand-in on either side would prove nothing
# about what actually installs.
cat > "$work/test_config.h" <<EOF
#pragma once
#define VP_PAYLOAD_ROOT "$payload"
#define GAME_ROMFS      "$mk7abs/extract/romfs"
#define REF_DIR         "$work/ref"
#define OUT_DIR         "$work/out"
#define SD_ROOT         "$work/sd/"
#define SD_ROOT_DIR     "$work/sd"
EOF

ENGINE="$here/../source/patch.c $here/../source/sarc.c \
        $here/../source/msbt.c $here/../source/szs.c $here/../source/gamefs.c"

build() {
    # shellcheck disable=SC2086  # ENGINE is a deliberate word-split file list
    gcc -std=c11 -Wall -Wextra -Werror -O2 \
        -I"$here/../source" -I"$work" -include test_config.h \
        "$1" $ENGINE ${2:-} -o "$work/$3"
}

build "$here/test_patch.c" "" test_patch
build "$here/test_install.c" "$here/../source/install.c" test_install

# The front end's button logic has no engine behind it at all.
gcc -std=c11 -Wall -Wextra -Werror -O2 -I"$here/../source" \
    "$here/test_uimodel.c" "$here/../source/uimodel.c" -o "$work/test_uimodel"

"$work/test_uimodel"
echo
"$work/test_patch"
echo
"$work/test_install"
echo
python "$here/check_output.py" "$mk7abs" "$work/out" "$work/ref"
