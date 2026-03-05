#!/usr/bin/env bash
# =============================================================================
# mygit_tests3.sh  –  Comprehensive regression & hardening test suite
# =============================================================================
# Run from the project root:
#   bash scripts/mygit_tests3.sh
#
# Exit code: 0 = all tests passed, non-zero = at least one failure.
# =============================================================================

MYGIT_REL="${1:-./build/mygit}"
MYGIT="$(cd "$(dirname "$MYGIT_REL")" 2>/dev/null && pwd)/$(basename "$MYGIT_REL")"

if [[ ! -x "$MYGIT" ]]; then
    printf 'ERROR: mygit binary not found or not executable: %s\n' "$MYGIT"
    exit 2
fi

# ── helpers ──────────────────────────────────────────────────────────────────
TMPDIR_ROOT=$(mktemp -d)
PASS_FILE="$TMPDIR_ROOT/_pass"; echo 0 > "$PASS_FILE"
FAIL_FILE="$TMPDIR_ROOT/_fail"; echo 0 > "$FAIL_FILE"

cleanup() { rm -rf "$TMPDIR_ROOT"; }
trap cleanup EXIT

green()  { printf '\033[0;32m✓  %s\033[0m\n' "$1"; }
red()    { printf '\033[0;31m✗  %s\033[0m\n' "$1"; }
yellow() { printf '\033[0;33m⚠  %s\033[0m\n' "$1"; }
header() { printf '\n\033[1;34m══ %s\033[0m\n' "$1"; }

pass() { green "$1";  echo $(( $(cat "$PASS_FILE") + 1 )) > "$PASS_FILE"; }
fail() { red   "$1";  echo $(( $(cat "$FAIL_FILE") + 1 )) > "$FAIL_FILE"; }

assert_ok() {
    local desc="$1"; shift
    if "$@" >/dev/null 2>&1; then
        pass "$desc"
    else
        fail "$desc  [cmd: $*]"
    fi
}

assert_fail() {
    local desc="$1"; shift
    if "$@" >/dev/null 2>&1; then
        fail "$desc — expected non-zero exit  [cmd: $*]"
    else
        pass "$desc"
    fi
}

assert_out_contains() {
    local desc="$1"; local pat="$2"; shift 2
    local out; out=$("$@" 2>&1) || true
    if printf '%s' "$out" | grep -qF "$pat"; then
        pass "$desc"
    else
        fail "$desc — expected '$pat' in output, got: $out"
    fi
}

assert_out_not_contains() {
    local desc="$1"; local pat="$2"; shift 2
    local out; out=$("$@" 2>&1) || true
    if printf '%s' "$out" | grep -qF "$pat"; then
        fail "$desc — unexpected '$pat' in output"
    else
        pass "$desc"
    fi
}

assert_file_exists()  {
    if [[ -f "$1" ]]; then pass "file exists: $1"; else fail "file missing: $1"; fi
}
assert_file_missing() {
    if [[ ! -e "$1" ]]; then pass "file absent: $1"; else fail "file should not exist: $1"; fi
}
assert_file_contains() {
    if grep -qF "$2" "$1" 2>/dev/null; then
        pass "file '$1' contains '$2'"
    else
        fail "file '$1' should contain '$2'"
    fi
}

newrepo() {
    local d="$TMPDIR_ROOT/$1"
    mkdir -p "$d"
    (cd "$d" && "$MYGIT" init) >/dev/null 2>&1
    echo "$d"
}

# =============================================================================
# 1. OUTSIDE-REPO ERRORS
# =============================================================================
header "1. Commands outside a repository"

OUTSIDE=$(mktemp -d "$TMPDIR_ROOT/noXXXXXX")
assert_fail "add outside repo"       "$MYGIT" add foo.txt
assert_fail "commit outside repo"    "$MYGIT" commit -m "x"
# status/log/branch/checkout/diff all need cwd override; run from OUTSIDE
(cd "$OUTSIDE" && assert_fail "status outside repo"   "$MYGIT" status)
(cd "$OUTSIDE" && assert_fail "log outside repo"      "$MYGIT" log)
(cd "$OUTSIDE" && assert_fail "branch outside repo"   "$MYGIT" branch)
(cd "$OUTSIDE" && assert_fail "checkout outside repo" "$MYGIT" checkout main)
(cd "$OUTSIDE" && assert_fail "diff outside repo"     "$MYGIT" diff)

# =============================================================================
# 2. INIT
# =============================================================================
header "2. init"

INIT_DIR="$TMPDIR_ROOT/init_test"
mkdir -p "$INIT_DIR"
assert_ok "init fresh directory" "$MYGIT" init "$INIT_DIR"
assert_file_exists "$INIT_DIR/.git/HEAD"
assert_file_exists "$INIT_DIR/.git/config"
assert_file_contains "$INIT_DIR/.git/HEAD" "refs/heads/main"
assert_fail "init twice rejected" "$MYGIT" init "$INIT_DIR"

# =============================================================================
# 3. BASIC ADD / COMMIT / LOG
# =============================================================================
header "3. Basic add → commit → log"

R=$(newrepo basic)
cd "$R"
echo "hello world" > hello.txt
assert_ok "add file"                  "$MYGIT" add hello.txt
assert_out_contains "commit exits 0, shows short hash" "[" \
    "$MYGIT" commit -m "Initial commit"
assert_out_contains "log shows 'commit'"     "commit"         "$MYGIT" log
assert_out_contains "log shows message"      "Initial commit" "$MYGIT" log
assert_out_contains "log shows author"       "Unknown Author" "$MYGIT" log

# =============================================================================
# 4. COMMIT EDGE CASES
# =============================================================================
header "4. Commit edge cases"

R=$(newrepo commit_edge)
cd "$R"
assert_fail "commit with empty message rejected"     "$MYGIT" commit -m ""
assert_fail "commit with nothing staged rejected"    "$MYGIT" commit -m "empty"

echo "file" > f.txt
assert_ok "add for author-bracket test" "$MYGIT" add f.txt
GIT_AUTHOR_NAME="Alice <HR>" GIT_AUTHOR_EMAIL="alice@example.com" \
    assert_ok "commit with angle-bracket author name succeeds" \
              "$MYGIT" commit -m "author bracket test"
assert_out_contains "log survives angle-bracket author" "commit" "$MYGIT" log

# =============================================================================
# 5. STATUS ACCURACY
# =============================================================================
header "5. Status accuracy"

R=$(newrepo status_acc)
cd "$R"
echo "line1" > a.txt
echo "line2" > b.txt
"$MYGIT" add a.txt b.txt && "$MYGIT" commit -m "initial" >/dev/null 2>&1
assert_out_contains "status: nothing staged after commit" \
    "(nothing staged)" "$MYGIT" status

echo "new" > c.txt
"$MYGIT" add c.txt
assert_out_contains "status shows staged new file"   "new file:   c.txt"  "$MYGIT" status

echo "modified" > a.txt          # NOT staged
assert_out_contains "status shows not-staged modified" "modified:   a.txt" "$MYGIT" status

rm b.txt                         # NOT staged
assert_out_contains "status shows not-staged deleted"  "deleted:    b.txt" "$MYGIT" status

echo "x" > untracked.txt
assert_out_contains "status shows untracked" "untracked.txt" "$MYGIT" status

# =============================================================================
# 6. SUBDIRECTORY SUPPORT
# =============================================================================
header "6. Subdirectory support"

R=$(newrepo subdir)
cd "$R"
mkdir -p src/utils
echo "main"   > src/main.cpp
echo "util"   > src/utils/helper.cpp
echo "readme" > README.md

"$MYGIT" add README.md src/main.cpp src/utils/helper.cpp
"$MYGIT" commit -m "nested commit" >/dev/null 2>&1
assert_out_contains "log after nested commit" "nested commit" "$MYGIT" log
assert_out_contains "status clean after nested commit" "(nothing staged)" "$MYGIT" status

echo "modified" > src/utils/helper.cpp
diff_out=$("$MYGIT" diff 2>&1)
if printf '%s' "$diff_out" | grep -qF "(no changes)"; then
    fail "diff should see nested file change"
else
    pass "diff sees nested file change"
fi

"$MYGIT" add src/utils/helper.cpp
assert_out_contains "status shows staged modification in subdir" "modified:" "$MYGIT" status
"$MYGIT" commit -m "update helper" >/dev/null 2>&1

"$MYGIT" branch feature >/dev/null 2>&1
"$MYGIT" checkout feature >/dev/null 2>&1
assert_file_exists  "$R/src/utils/helper.cpp"
assert_file_exists  "$R/src/main.cpp"
assert_file_exists  "$R/README.md"

# =============================================================================
# 7. BRANCH LISTING AND CREATION
# =============================================================================
header "7. Branch operations"

R=$(newrepo branch_ops)
cd "$R"
echo "v1" > file.txt
"$MYGIT" add file.txt && "$MYGIT" commit -m "first" >/dev/null 2>&1
assert_out_contains "branch lists main" "main" "$MYGIT" branch
assert_ok "create branch feature" "$MYGIT" branch feature
assert_out_contains "branch lists feature" "feature" "$MYGIT" branch
assert_fail "duplicate branch rejected" "$MYGIT" branch feature
assert_fail "path-traversal branch rejected"  "$MYGIT" branch "../../config"
assert_fail "double-dot branch rejected"      "$MYGIT" branch "foo..bar"
assert_fail "leading-dash branch rejected"    "$MYGIT" branch "-bad"

# =============================================================================
# 8. SWITCHING BRANCHES
# =============================================================================
header "8. Checkout – branch switching"

R=$(newrepo checkout_test)
cd "$R"
echo "v1" > file.txt
"$MYGIT" add file.txt && "$MYGIT" commit -m "commit on main" >/dev/null 2>&1
"$MYGIT" branch dev >/dev/null 2>&1
"$MYGIT" checkout dev >/dev/null 2>&1
assert_out_contains "branch shows * dev after checkout" "* dev" "$MYGIT" branch

echo "dev only" > dev_file.txt
"$MYGIT" add dev_file.txt && "$MYGIT" commit -m "dev commit" >/dev/null 2>&1
"$MYGIT" checkout main >/dev/null 2>&1
assert_file_missing "$R/dev_file.txt"
assert_file_exists  "$R/file.txt"
assert_out_contains "already on main" "Already on" "$MYGIT" checkout main
assert_fail "checkout nonexistent branch fails" "$MYGIT" checkout nonexistent

# =============================================================================
# 9. DIRTY-TREE GUARD
# =============================================================================
header "9. Dirty-tree guard on checkout"

R=$(newrepo dirty_guard)
cd "$R"
echo "original" > important.txt
"$MYGIT" add important.txt && "$MYGIT" commit -m "base commit" >/dev/null 2>&1
"$MYGIT" branch other >/dev/null 2>&1
echo "local change" > important.txt      # modify WITHOUT re-staging

local_out=$("$MYGIT" checkout other 2>&1) || true
if printf '%s' "$local_out" | grep -qiE "overwritten|dirty|changes|local"; then
    pass "checkout rejected with dirty working tree"
else
    fail "checkout should be rejected — got: $local_out"
fi
assert_file_contains "$R/important.txt" "local change"

# =============================================================================
# 10. DIFF – MULTIPLE DISJOINT HUNKS
# =============================================================================
header "10. Diff – multiple disjoint hunks"

R=$(newrepo diff_hunks)
cd "$R"
python3 -c "
for i in range(1, 31):
    print(f'line {i}')
" > multiline.txt
"$MYGIT" add multiline.txt && "$MYGIT" commit -m "base" >/dev/null 2>&1
python3 -c "
lines = open('multiline.txt').readlines()
lines[1]  = 'CHANGED line 2\n'
lines[27] = 'CHANGED line 28\n'
open('multiline.txt', 'w').writelines(lines)
"
diff_out=$("$MYGIT" diff 2>&1)
hunk_count=$(printf '%s\n' "$diff_out" | grep -c '^@@' 2>/dev/null || echo 0)
if [[ "$hunk_count" -ge 2 ]]; then
    pass "diff produces $hunk_count hunks for disjoint changes (expected >=2)"
else
    fail "diff should produce >=2 hunks, got $hunk_count — output: $diff_out"
fi

# =============================================================================
# 11. DIFF – BINARY FILE DETECTION
# =============================================================================
header "11. Diff – binary detection"

R=$(newrepo diff_binary)
cd "$R"
echo "text" > text.txt
"$MYGIT" add text.txt && "$MYGIT" commit -m "init" >/dev/null 2>&1
printf 'hello\x00world' > text.txt
assert_out_contains "binary diff shows 'Binary files'" "Binary files" "$MYGIT" diff

# =============================================================================
# 12. LOG – COMMIT CHAIN INTEGRITY
# =============================================================================
header "12. Log – chain integrity"

R=$(newrepo log_chain)
cd "$R"
for i in 1 2 3 4 5; do
    echo "v$i" > f.txt
    "$MYGIT" add f.txt && "$MYGIT" commit -m "commit $i" >/dev/null 2>&1
done
log_out=$("$MYGIT" log 2>&1)
commit_count=$(printf '%s\n' "$log_out" | grep -c '^commit ' || echo 0)
if [[ "$commit_count" -eq 5 ]]; then
    pass "log shows all 5 commits"
else
    fail "log should show 5 commits, got $commit_count"
fi
assert_out_contains "log shows latest message" "commit 5" "$MYGIT" log
assert_out_contains "log shows first message"  "commit 1" "$MYGIT" log

# =============================================================================
# 13. GITIGNORE — NESTED DIRECTORY PATTERN
# =============================================================================
header "13. GitIgnore – nested directory matching"

R=$(newrepo gitignore_test)
cd "$R"
echo "build/" > .gitignore
mkdir -p src/build
echo "artifact" > src/build/out.o
echo "source"   > build_main.c

status_out=$("$MYGIT" status 2>&1)
if printf '%s' "$status_out" | grep -qF "src/build/out.o"; then
    fail "nested build/ file should be ignored"
else
    pass "nested build/ file correctly ignored"
fi
if printf '%s' "$status_out" | grep -qF "build_main.c"; then
    pass "build_main.c correctly shown as untracked"
else
    fail "build_main.c should appear as untracked"
fi

# =============================================================================
# 14. LARGE FILE ROUND-TRIP (10 MB)
# =============================================================================
header "14. Large file round-trip (10 MB)"

R=$(newrepo large_file)
cd "$R"
python3 -c "import sys; sys.stdout.buffer.write(b'A' * 10_485_760)" > big.txt
assert_ok "add 10 MB file"    "$MYGIT" add big.txt
assert_ok "commit 10 MB file" "$MYGIT" commit -m "large"
assert_out_contains "log shows large commit" "large" "$MYGIT" log

# =============================================================================
# 15. CHECKOUT CONTENT ROUND-TRIP
# =============================================================================
header "15. Checkout content round-trip"

R=$(newrepo roundtrip)
cd "$R"
ORIGINAL_CONTENT="the quick brown fox
jumps over the lazy dog"
printf '%s\n' "$ORIGINAL_CONTENT" > fox.txt
"$MYGIT" add fox.txt && "$MYGIT" commit -m "fox" >/dev/null 2>&1
"$MYGIT" branch alt >/dev/null 2>&1
"$MYGIT" checkout alt >/dev/null 2>&1

echo "different content on alt" > fox.txt
"$MYGIT" add fox.txt && "$MYGIT" commit -m "alt fox" >/dev/null 2>&1
"$MYGIT" checkout main >/dev/null 2>&1

restored=$(cat fox.txt)
if [[ "$restored" == "$ORIGINAL_CONTENT" ]]; then
    pass "file content exactly restored after branch round-trip"
else
    fail "content mismatch after checkout — got: '$restored', expected: '$ORIGINAL_CONTENT'"
fi

# =============================================================================
# 16. BRANCH NAME VALIDATION (edge cases)
# =============================================================================
header "16. Branch name validation – edge cases"

R=$(newrepo branch_valid)
cd "$R"
echo "x" > x.txt && "$MYGIT" add x.txt && "$MYGIT" commit -m "x" >/dev/null 2>&1
assert_fail "branch starting with / rejected"  "$MYGIT" branch "/leading"
assert_fail "branch ending with / rejected"    "$MYGIT" branch "trailing/"
assert_fail "branch with // rejected"          "$MYGIT" branch "foo//bar"
assert_fail "empty branch name rejected"       "$MYGIT" branch ""

# =============================================================================
# 17. MULTI-BRANCH COMMIT ISOLATION
# =============================================================================
header "17. Multi-branch commit isolation"

R=$(newrepo multi_branch)
cd "$R"
echo "shared" > shared.txt
"$MYGIT" add shared.txt && "$MYGIT" commit -m "shared base" >/dev/null 2>&1
"$MYGIT" branch branchA >/dev/null 2>&1
"$MYGIT" checkout branchA >/dev/null 2>&1
echo "branchA content" > fileA.txt
"$MYGIT" add fileA.txt && "$MYGIT" commit -m "commit on A" >/dev/null 2>&1
assert_file_exists "$R/fileA.txt"
"$MYGIT" checkout main >/dev/null 2>&1
assert_file_missing "$R/fileA.txt"
assert_file_exists  "$R/shared.txt"
assert_out_not_contains "main log does not contain A's commit" \
    "commit on A" "$MYGIT" log

# =============================================================================
# 18. HELP AND UNKNOWN COMMAND
# =============================================================================
header "18. Help and unknown command"

cd "$TMPDIR_ROOT"
assert_ok   "mygit help exits 0"          "$MYGIT" help
assert_fail "unknown command exits non-0" "$MYGIT" frobnicator

# =============================================================================
# 19. mygit rm
# =============================================================================
header "19. mygit rm"

R="$TMPDIR_ROOT/rm_test"
mkdir -p "$R" && cd "$R"
"$MYGIT" init >/dev/null 2>&1
echo "hello" > keep.txt
echo "bye"   > gone.txt
"$MYGIT" add keep.txt gone.txt >/dev/null 2>&1
"$MYGIT" commit -m "initial" >/dev/null 2>&1

# rm outside repo
cd "$TMPDIR_ROOT"
assert_fail "rm outside repo fails" "$MYGIT" rm gone.txt
cd "$R"

# rm non-tracked file
echo "untracked" > untracked.txt
assert_fail "rm non-tracked file fails" "$MYGIT" rm untracked.txt

# rm --cached (removes from index, keeps file)
assert_ok "rm --cached exits 0" "$MYGIT" rm --cached keep.txt
assert_file_exists "$R/keep.txt"
# file should no longer appear as staged (index should be clean for keep.txt)
"$MYGIT" add keep.txt >/dev/null 2>&1   # re-stage for the next test

# rm without --cached (removes from index AND disk)
echo "to_delete" > delete_me.txt
"$MYGIT" add delete_me.txt >/dev/null 2>&1
assert_ok "rm without --cached exits 0" "$MYGIT" rm delete_me.txt
assert_file_missing "disk file removed after rm" "$R/delete_me.txt"

# rm nonexistent file
assert_fail "rm nonexistent file fails" "$MYGIT" rm no_such_file.txt

# =============================================================================
# 20. Binary index round-trip
# =============================================================================
header "20. Binary index round-trip"

R="$TMPDIR_ROOT/bin_idx"
mkdir -p "$R" && cd "$R"
"$MYGIT" init >/dev/null 2>&1
printf 'alpha\nbeta\ngamma\n' > data.txt
"$MYGIT" add data.txt >/dev/null 2>&1
"$MYGIT" commit -m "binary idx test" >/dev/null 2>&1
# Index file should now be binary (starts with MGIX)
IDX_FILE="$R/.git/index"
MAGIC=$(head -c 4 "$IDX_FILE" 2>/dev/null)
if [[ "$MAGIC" == "MGIX" ]]; then
    pass "index file has binary magic MGIX"
else
    fail "index file does not have binary magic (got: $MAGIC)"
fi
# Second add/commit should survive round-trip through binary index
printf 'delta\n' >> data.txt
"$MYGIT" add data.txt >/dev/null 2>&1
assert_ok "commit after binary index round-trip" "$MYGIT" commit -m "second"
assert_out_contains "log shows both commits" "second" "$MYGIT" log

# =============================================================================
# 21. mygit gc (pack objects)
# =============================================================================
header "21. mygit gc"

R="$TMPDIR_ROOT/gc_test"
mkdir -p "$R" && cd "$R"
"$MYGIT" init >/dev/null 2>&1
for i in 1 2 3 4 5; do
    echo "content $i" > "file$i.txt"
    "$MYGIT" add "file$i.txt" >/dev/null 2>&1
    "$MYGIT" commit -m "commit $i" >/dev/null 2>&1
done
# Count loose objects before gc
LOOSE_BEFORE=$(find "$R/.git/objects" -maxdepth 2 -type f \
    ! -name "pack.mgpk" ! -name "pack.mgpk.idx" 2>/dev/null | wc -l | tr -d ' ')
assert_ok "gc exits 0" "$MYGIT" gc
assert_file_exists "$R/.git/objects/pack.mgpk"
assert_file_exists "$R/.git/objects/pack.mgpk.idx"
# Loose objects should be gone (or significantly fewer)
LOOSE_AFTER=$(find "$R/.git/objects" -maxdepth 2 -type f \
    ! -name "pack.mgpk" ! -name "pack.mgpk.idx" 2>/dev/null | wc -l | tr -d ' ')
if [[ "$LOOSE_AFTER" -lt "$LOOSE_BEFORE" ]]; then
    pass "loose objects reduced after gc ($LOOSE_BEFORE -> $LOOSE_AFTER)"
else
    fail "gc did not reduce loose objects ($LOOSE_BEFORE -> $LOOSE_AFTER)"
fi
# Verify objects still readable after gc (log should work)
assert_out_contains "log works after gc" "commit 5" "$MYGIT" log
assert_out_contains "log shows first commit after gc" "commit 1" "$MYGIT" log

# =============================================================================
# 22. mygit merge – fast-forward
# =============================================================================
header "22. mygit merge – fast-forward"

R="$TMPDIR_ROOT/merge_ff"
mkdir -p "$R" && cd "$R"
"$MYGIT" init >/dev/null 2>&1
echo "base" > base.txt
"$MYGIT" add base.txt >/dev/null 2>&1
"$MYGIT" commit -m "base commit" >/dev/null 2>&1
# Create a feature branch with more commits
"$MYGIT" branch feature >/dev/null 2>&1
"$MYGIT" checkout feature >/dev/null 2>&1
echo "feature line" > feature.txt
"$MYGIT" add feature.txt >/dev/null 2>&1
"$MYGIT" commit -m "feature commit" >/dev/null 2>&1
# Back to main; feature is strictly ahead → fast-forward
"$MYGIT" checkout main >/dev/null 2>&1
assert_ok "fast-forward merge exits 0" "$MYGIT" merge feature
assert_file_exists "$R/feature.txt"
assert_out_contains "log shows merged feature commit" "feature commit" "$MYGIT" log

# =============================================================================
# 23. mygit merge – 3-way clean
# =============================================================================
header "23. mygit merge – 3-way clean"

R="$TMPDIR_ROOT/merge_3way"
mkdir -p "$R" && cd "$R"
"$MYGIT" init >/dev/null 2>&1
printf 'line1\nline2\nline3\n' > shared.txt
"$MYGIT" add shared.txt >/dev/null 2>&1
"$MYGIT" commit -m "base" >/dev/null 2>&1
# branchA: changes first line
"$MYGIT" branch branchA >/dev/null 2>&1
"$MYGIT" checkout branchA >/dev/null 2>&1
printf 'LINE1\nline2\nline3\n' > shared.txt
"$MYGIT" add shared.txt >/dev/null 2>&1
"$MYGIT" commit -m "A changes line1" >/dev/null 2>&1
# main: changes last line (non-overlapping)
"$MYGIT" checkout main >/dev/null 2>&1
printf 'line1\nline2\nLINE3\n' > shared.txt
"$MYGIT" add shared.txt >/dev/null 2>&1
"$MYGIT" commit -m "main changes line3" >/dev/null 2>&1
# Merge branchA into main – should be clean
assert_ok "3-way clean merge exits 0" "$MYGIT" merge branchA
# Result: line1 from A, line3 from main, line2 unchanged
assert_file_contains "$R/shared.txt" "LINE1"
assert_file_contains "$R/shared.txt" "LINE3"

# =============================================================================
# 24. mygit merge – conflict detection
# =============================================================================
header "24. mygit merge – conflict detection"

R="$TMPDIR_ROOT/merge_conflict"
mkdir -p "$R" && cd "$R"
"$MYGIT" init >/dev/null 2>&1
printf 'original line\n' > conflict.txt
"$MYGIT" add conflict.txt >/dev/null 2>&1
"$MYGIT" commit -m "base" >/dev/null 2>&1
# branchB: modify the line
"$MYGIT" branch branchB >/dev/null 2>&1
"$MYGIT" checkout branchB >/dev/null 2>&1
printf 'branch B version\n' > conflict.txt
"$MYGIT" add conflict.txt >/dev/null 2>&1
"$MYGIT" commit -m "B changes line" >/dev/null 2>&1
# main: also modify the same line differently
"$MYGIT" checkout main >/dev/null 2>&1
printf 'main version\n' > conflict.txt
"$MYGIT" add conflict.txt >/dev/null 2>&1
"$MYGIT" commit -m "main changes line" >/dev/null 2>&1
# Merge branchB → should report conflict
assert_fail "conflicting merge exits non-0" "$MYGIT" merge branchB
assert_file_contains "$R/conflict.txt" "<<<<<<"
assert_file_contains "$R/conflict.txt" "======="

# =============================================================================
# SUMMARY
# =============================================================================
PASS=$(cat "$PASS_FILE")
FAIL=$(cat "$FAIL_FILE")
printf '\n%s\n' "════════════════════════════════════════"
printf '  PASSED: %d   FAILED: %d\n' "$PASS" "$FAIL"
printf '%s\n' "════════════════════════════════════════"

if [[ "$FAIL" -gt 0 ]]; then
    exit 1
fi
exit 0


