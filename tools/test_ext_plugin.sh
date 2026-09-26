#!/bin/bash
# SPDX-License-Identifier: GPL-3.0-or-later
# ===========================================================================
#  µnleashed BBS
#  Electronic freedom on a microcontroller.
# ===========================================================================
#
# File:         tools/test_ext_plugin.sh
# Module:       Tools / the test of plugins kept in their own repositories (1.2.0)
#
# Purpose:      Proves the whole path a plugin in a repository of its own
#               takes into a board's firmware, on the host build: a git
#               repository made from tools/testplugin (the "hello" plugin),
#               locked at its commit, fetched by tools/plugins.py, built into
#               host/bbs_host_ext with EXT=hello, and seen starting. Then the
#               refusals: a release will not take a local path, a moved
#               checkout is caught, and a plugin that needs a newer core
#               fails to compile with the sentence UNLEASHED_PLUGIN_API gives.
#
#               The ESP32 side of the same path (tools/pio_plugins.py and
#               src/CMakeLists.txt) is proved by building an environment with
#               custom_ext_plugins = hello; see PLUGINS.md.
#
# Usage:        tools/test_ext_plugin.sh        (in WSL or on Linux)
#
# Copyright 2026 - Robert Mech
# License:      GNU General Public License v3 or later
# SPDX-License-Identifier: GPL-3.0-or-later
# ===========================================================================
set -u
PROJ=$(cd "$(dirname "$0")/.." && pwd)
TMP=$(mktemp -d /tmp/extplugin.XXXXXX)
PASS=0
FAIL=0
check() {                                   # check "what" command...
    local what=$1; shift
    if "$@" >/dev/null 2>&1; then echo "  PASS  $what"; PASS=$((PASS + 1));
    else echo "  FAIL  $what"; FAIL=$((FAIL + 1)); fi
}
cleanup() {
    rm -rf "$TMP" "$PROJ/ext/hello" "$PROJ/host/bbs_host_ext"
    (cd "$PROJ/host" && make -s ext-gen/ext_plugins.h >/dev/null 2>&1)   # back to none
}
trap cleanup EXIT

echo "Plugins in their own repositories"

# A repository of the plugin, one commit, locked.
cp -r "$PROJ/tools/testplugin" "$TMP/hello"
git -C "$TMP/hello" init -q
git -C "$TMP/hello" add -A
git -C "$TMP/hello" -c user.name=test -c user.email=test@example.invalid commit -qm "hello 1.0.0"
COMMIT=$(git -C "$TMP/hello" rev-parse HEAD)
echo "hello  $TMP/hello  $COMMIT" > "$TMP/plugins.lock"
cd "$PROJ"

check "fetch takes the locked commit into ext/hello" \
    python3 tools/plugins.py fetch hello --lock "$TMP/plugins.lock"
check "ext/hello is at exactly that commit" \
    test "$(git -C ext/hello rev-parse HEAD)" = "$COMMIT"
check "a release refuses a plugin from a local path" \
    bash -c "! python3 tools/plugins.py fetch hello --lock '$TMP/plugins.lock' --release"
echo "hello  $TMP/hello  0000000000000000000000000000000000000000" > "$TMP/bad.lock"
check "a commit the repository does not have is refused" \
    bash -c "! python3 tools/plugins.py fetch hello --lock '$TMP/bad.lock'"
python3 tools/plugins.py fetch hello --lock "$TMP/plugins.lock" >/dev/null

cd "$PROJ/host"
check "the host board builds with it (make EXT=hello bbs_host_ext)" make -s EXT=hello bbs_host_ext
check "the descriptor is in the image" bash -c "nm bbs_host_ext | grep -q kHelloPlugin"
check "ext_plugins.h names it" grep -q "X(kHelloPlugin)" ext-gen/ext_plugins.h

# Start it on a data folder of its own and read the console.
DATA=$TMP/data
mkdir -p "$DATA/user"
cp -r "$PROJ/data/." "$DATA/"
rm -f "$DATA/system.cfg" "$DATA/users.txt"
printf 'tz = UTC0\nsysop_password = testsysop\n' > "$DATA/user/system.cfg"
PORT=$((7400 + RANDOM % 400))
timeout 4 ./bbs_host_ext "$DATA" "$PORT" > "$TMP/log" 2>&1
check "it starts with the board" grep -q "hello: an external plugin, started" "$TMP/log"
grep -q "hello: an external plugin, started" "$TMP/log" || tail -20 "$TMP/log"
check "and the plugin table has room for it" bash -c "! grep -q 'more plugins than' '$TMP/log'"

# PF_CORE on a plugin from its own repository buys it nothing: no flash.
HELLO="$PROJ/ext/hello/bbs/hello.cpp"
sed -i 's/"1.0.0", 0, 0, PF_ON,/"1.0.0", 0, 4096, PF_ON | PF_CORE,/' "$HELLO"
make -s EXT=hello bbs_host_ext > /dev/null 2>&1
timeout 4 ./bbs_host_ext "$DATA" "$PORT" > "$TMP/log" 2>&1
check "one that claims PF_CORE still gets no flash" grep -q "hello wants storage on the board's flash" "$TMP/log"
sed -i 's/"1.0.0", 0, 4096, PF_ON | PF_CORE,/"1.0.0", 0, 0, PF_ON,/' "$HELLO"

# A name a shipped plugin has is refused: its section and folder are taken.
sed -i 's/{ "hello", /{ "chat", /' "$HELLO"
make -s EXT=hello bbs_host_ext > /dev/null 2>&1
timeout 4 ./bbs_host_ext "$DATA" "$PORT" > "$TMP/log" 2>&1
check "one named like a shipped plugin is not started" grep -q "a second plugin called chat" "$TMP/log"
sed -i 's/{ "chat", /{ "hello", /' "$HELLO"

# A plugin written for a newer core fails to compile, and says why.
sed -i 's/UNLEASHED_PLUGIN_API(1, 0)/UNLEASHED_PLUGIN_API(1, 99)/' "$PROJ/ext/hello/bbs/hello.cpp"
make -s EXT=hello bbs_host_ext > "$TMP/err" 2>&1
check "a plugin that needs a newer core does not build, and says why" grep -q 'needs a newer unleashed core' "$TMP/err"
sed -i 's/^api *= *1.0/api = 1.99/' "$PROJ/ext/hello/unleashed-plugin.ini"
cd "$PROJ"
check "and tools/plugins.py says so before a build" \
    bash -c "python3 tools/plugins.py check hello --lock '$TMP/plugins.lock' 2>&1 | grep -q 'update the core'"

echo
echo "$PASS passed, $FAIL failed"
[ "$FAIL" -eq 0 ]
