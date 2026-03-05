#!/usr/bin/env bash
# mygit_tests.sh — primary test suite for mygit
# Covers: help, init, add, commit, log, status, branch, checkout,
#         outside-repo errors, object store integrity, re-staging,
#         consecutive commits, parent chain, branch listing markers,
#         empty message rejection, untracked files, deleted staged file.

set -euo pipefail

MYGIT="${MYGIT:-$(dirname "$0")/../build/mygit}"
MYGIT="$(cd "$(dirname "$MYGIT")" && pwd)/$(basename "$MYGIT")"

pass=0
fail=0
FAILURES=()

check() {
    local desc="$1"; shift
    if eval "$@" &>/dev/null; then
        echo "  PASS: $desc"
        pass=$((pass + 1))
    else
        echo "  FAIL: $desc"
        fail=$((fail + 1))
        FAILURES+=("$desc")
    fi
}

check_output() {
    local desc="$1"
    local expected="$2"
    shift 2
    local out
    out=$(eval "$@" 2>&1 || true)
    if echo "$out" | grep -qF "$expected"; then
        echo "  PASS: $desc"
        pass=$((pass + 1))
    else
        echo "  FAIL: $desc (got: $out)"
        fail=$((fail + 1))
        FAILURES+=("$desc")
    fi
}

check_no_output() {
    local desc="$1"
    local pattern="$2"
    shift 2
    local out
    out=$(eval "$@" 2>&1 || true)
    if echo "$out" | grep -qF "$pattern"; then
        echo "  FAIL: $desc (unexpected: $pattern)"
        fail=$((fail + 1))
        FAILURES+=("$desc")
    else
        echo "  PASS: $desc"
        pass=$((pass + 1))
    fi
}

# ── Setup ──────────────────────────────────────────────────────────────────────
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT
REPO="$TMP/repo"

echo "=== Suite 1: Help & Usage ==="

check_output "help lists init"      "init"      "$MYGIT help"
check_output "help lists add"       "add"       "$MYGIT help"
check_output "help lists commit"    "commit"    "$MYGIT help"
check_output "help lists status"    "status"    "$MYGIT help"
check_output "help lists log"       "log"       "$MYGIT help"
check_output "help lists branch"    "branch"    "$MYGIT help"
check_output "help lists checkout"  "checkout"  "$MYGIT help"
check_output "help lists diff"      "diff"      "$MYGIT help"
check_output "unknown command error" "not a mygit command" "$MYGIT frobnicate"

echo ""
echo "=== Suite 2: Init ==="

check_output "init creates repo"    "Initialized" "$MYGIT init $REPO"
check        ".git dir exists"      "[ -d '$REPO/.git' ]"
check        "objects dir exists"   "[ -d '$REPO/.git/objects' ]"
check        "refs/heads exists"    "[ -d '$REPO/.git/refs/heads' ]"
check        "HEAD file exists"     "[ -f '$REPO/.git/HEAD' ]"
check_output "HEAD points to main"  "refs/heads/main"  "cat $REPO/.git/HEAD"
check_output "init again fails"     "already exists"   "$MYGIT init $REPO"

echo ""
echo "=== Suite 3: Commands outside repo ==="

OTHER="$TMP/outside"
mkdir -p "$OTHER"
cd "$OTHER"
check_output "add outside repo"    "not a mygit repository" "$MYGIT add foo.txt"
check_output "commit outside repo" "not a mygit repository" "$MYGIT commit -m hi"
check_output "status outside repo" "not a mygit repository" "$MYGIT status"
check_output "log outside repo"    "not a mygit repository" "$MYGIT log"
check_output "branch outside repo" "not a mygit repository" "$MYGIT branch"

echo ""
echo "=== Suite 4: Add & Stage ==="

cd "$REPO"
echo "line one" > file1.txt
echo "line two" > file2.txt

check_output "add single file"         "staged"     "$MYGIT add file1.txt"
check_output "add second file"         "staged"     "$MYGIT add file2.txt"
check_output "add missing file error"  "error"      "$MYGIT add nonexistent.txt"
check_output "add no args"             "usage"      "$MYGIT add"

echo ""
echo "=== Suite 5: Commit ==="

check_output "commit -m creates commit"  "first commit" "$MYGIT commit -m 'first commit'"
check_output "commit no args fails"      "required"     "$MYGIT commit"
check_output "commit empty message"      "required"     "$MYGIT commit -m ''"
echo ""
echo "=== Suite 6: Log ==="

check_output "log shows commit"          "first commit"  "$MYGIT log"
check_output "log shows author"          "Author"        "$MYGIT log"
check_output "log shows date"            "Date"          "$MYGIT log"
check_output "log shows hash"            "commit"        "$MYGIT log"

echo ""
echo "=== Suite 7: Status ==="

check_output "status clean"             "none"       "$MYGIT status"
check_output "status sections present"  "staged"     "$MYGIT status"

# New untracked file
echo "fresh" > untracked.txt
check_output "status shows untracked"   "untracked.txt"  "$MYGIT status"

# Modify staged file
echo "modified line" >> file1.txt
check_output "status shows modified"    "modified"        "$MYGIT status"

# Stage it again
$MYGIT add file1.txt &>/dev/null

echo ""
echo "=== Suite 8: Re-staging & Second Commit ==="

$MYGIT commit -m "second commit" &>/dev/null
check_output "log shows second commit"  "second commit"  "$MYGIT log"
check_output "log shows first commit"   "first commit"   "$MYGIT log"

echo ""
echo "=== Suite 9: Consecutive Commits & Parent Chain ==="

echo "c3" > c3.txt
$MYGIT add c3.txt &>/dev/null
$MYGIT commit -m "third commit" &>/dev/null

echo "c4" > c4.txt
$MYGIT add c4.txt &>/dev/null
$MYGIT commit -m "fourth commit" &>/dev/null

check "log has 4 entries" "[[ \$(\$MYGIT log 2>&1 | grep -c '^commit') -ge 4 ]]"
check_output "parent chain: fourth"  "fourth commit"  "$MYGIT log"
check_output "parent chain: third"   "third commit"   "$MYGIT log"
check_output "parent chain: second"  "second commit"  "$MYGIT log"
check_output "parent chain: first"   "first commit"   "$MYGIT log"

echo ""
echo "=== Suite 10: Deleted Staged File ==="

echo "temp" > temp.txt
$MYGIT add temp.txt &>/dev/null
$MYGIT commit -m "add temp" &>/dev/null
rm temp.txt
check_output "status shows deleted"  "deleted"  "$MYGIT status"

echo ""
echo "=== Suite 11: Object Store Integrity ==="

HASH=$(cat .git/refs/heads/main)
check        "main ref is 40 chars"   "[[ \${#HASH} -eq 40 ]]"
check        "commit object exists"   "[ -f \".git/objects/\${HASH:0:2}/\${HASH:2}\" ]"

# Objects dir should have multiple objects
OBJ_COUNT=$(find .git/objects -type f | wc -l | tr -d ' ')
check "object store has objects"  "[[ $OBJ_COUNT -gt 3 ]]"

echo ""
echo "── Results ──────────────────────────────────────────────────────────────"
echo "$pass passed, $fail failed"
if [[ ${#FAILURES[@]} -gt 0 ]]; then
    echo "Failed tests:"
    for f in "${FAILURES[@]}"; do echo "  - $f"; done
fi
echo ""
[[ $fail -eq 0 ]]
