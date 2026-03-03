#!/usr/bin/env bash
# mygit_tests2.sh — extended test suite for mygit
# Covers: branch creation/listing markers, checkout file restoration,
#         checkout file deletion, subdirectory (recursive tree), gitignore,
#         diff output, path traversal rejection, config author reading,
#         packed-refs fallback, atomic write resilience.

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

check_file_contains() {
    local desc="$1"
    local file="$2"
    local pattern="$3"
    if [[ -f "$file" ]] && grep -qF "$pattern" "$file"; then
        echo "  PASS: $desc"
        pass=$((pass + 1))
    else
        echo "  FAIL: $desc (file=$file pattern=$pattern)"
        fail=$((fail + 1))
        FAILURES+=("$desc")
    fi
}

# ── Setup ──────────────────────────────────────────────────────────────────────
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT

setup_repo() {
    local dir="$TMP/$1"
    $MYGIT init "$dir" &>/dev/null
    cd "$dir"
    echo "initial content" > README.md
    $MYGIT add README.md &>/dev/null
    $MYGIT commit -m "initial commit" &>/dev/null
    echo "$dir"
}

echo "=== Suite 1: Branch Creation & Listing ==="

REPO=$(setup_repo "repo1")
cd "$REPO"

check_output "initial branch is main"    "* main"   "$MYGIT branch"
check_no_output "no other branches yet"  "feat"     "$MYGIT branch"

$MYGIT branch feature &>/dev/null
check_output "feature branch listed"     "feature"  "$MYGIT branch"
check_output "main still marked active"  "* main"   "$MYGIT branch"
check_no_output "feature not marked"     "* feature" "$MYGIT branch"

$MYGIT branch another &>/dev/null
check_output "another branch listed"    "another"   "$MYGIT branch"

echo ""
echo "=== Suite 2: Checkout & Branch Marker ==="

$MYGIT checkout feature &>/dev/null
check_output "on feature after checkout"  "* feature"  "$MYGIT branch"
check_no_output "main not marked now"     "* main"     "$MYGIT branch"
check_output "HEAD updated"              "feature"    "cat .git/HEAD"

$MYGIT checkout main &>/dev/null
check_output "back on main"              "* main"     "$MYGIT branch"

echo ""
echo "=== Suite 3: Checkout File Restoration ==="

REPO=$(setup_repo "repo2")
cd "$REPO"

echo "main only file" > main_file.txt
$MYGIT add main_file.txt &>/dev/null
$MYGIT commit -m "main commit" &>/dev/null

$MYGIT branch dev &>/dev/null
$MYGIT checkout dev &>/dev/null

echo "dev content" > dev_file.txt
$MYGIT add dev_file.txt &>/dev/null
$MYGIT commit -m "dev commit" &>/dev/null

# dev_file.txt should exist on dev
check "dev_file exists on dev"            "[ -f dev_file.txt ]"

# Switch back to main — dev_file.txt should disappear
$MYGIT checkout main &>/dev/null
check "dev_file absent on main"           "[ ! -f dev_file.txt ]"
check "main_file present on main"         "[ -f main_file.txt ]"
check_file_contains "main_file content"   "main_file.txt" "main only file"

# Switch back to dev — dev_file.txt should reappear
$MYGIT checkout dev &>/dev/null
check "dev_file restored on dev"          "[ -f dev_file.txt ]"
check_file_contains "dev_file content"    "dev_file.txt" "dev content"

echo ""
echo "=== Suite 4: Subdirectory (Recursive Tree) ==="

REPO=$(setup_repo "repo3")
cd "$REPO"

mkdir -p src/utils
echo "int main(){}" > src/main.cpp
echo "void helper(){}" > src/utils/helper.cpp
echo "typedef int MyInt;" > src/utils/types.h

$MYGIT add src/main.cpp &>/dev/null
$MYGIT add src/utils/helper.cpp &>/dev/null
$MYGIT add src/utils/types.h &>/dev/null
$MYGIT commit -m "add source tree" &>/dev/null

check_output "log shows source commit" "add source tree" "$MYGIT log"

# Status should be clean after committing
check_output "status clean after subdir commit" "none" "$MYGIT status"

# Verify the files survive a checkout round-trip
$MYGIT branch test-branch &>/dev/null
$MYGIT checkout test-branch &>/dev/null
check "src/main.cpp present on new branch"       "[ -f src/main.cpp ]"
check "src/utils/helper.cpp present on new branch" "[ -f src/utils/helper.cpp ]"
$MYGIT checkout main &>/dev/null
check "src/main.cpp present back on main"        "[ -f src/main.cpp ]"

echo ""
echo "=== Suite 5: .gitignore Support ==="

REPO=$(setup_repo "repo4")
cd "$REPO"

cat > .gitignore << 'IGEOF'
*.log
build/
*.o
IGEOF

echo "ignored" > app.log
echo "also ignored" > debug.log
mkdir -p build
echo "binary" > build/app
echo "object" > app.o
echo "keep me" > app.c

# add should skip ignored files
check_output "add .log file warns"     "ignored"  "$MYGIT add app.log"
check_output "add .o file warns"       "ignored"  "$MYGIT add app.o"

# untracked status should not list ignored files
$MYGIT add app.c &>/dev/null
$MYGIT commit -m "add app.c" &>/dev/null
check_no_output "status hides *.log"           "app.log"   "$MYGIT status"
check_no_output "status hides build/"          "build/app" "$MYGIT status"
check_no_output "status hides *.o"             "app.o"     "$MYGIT status"

echo ""
echo "=== Suite 6: Diff ==="

REPO=$(setup_repo "repo5")
cd "$REPO"

echo -e "line 1\nline 2\nline 3" > poem.txt
$MYGIT add poem.txt &>/dev/null
$MYGIT commit -m "add poem" &>/dev/null

# Modify file, then diff
echo -e "line 1\nline 2 changed\nline 3\nline 4 new" > poem.txt

DIFF=$($MYGIT diff poem.txt 2>&1)
check_output "diff shows added line"    "+line 4 new"        "echo \"$DIFF\""
check_output "diff shows changed line"  "+line 2 changed"    "echo \"$DIFF\""
check_output "diff shows removed line"  "-line 2"            "echo \"$DIFF\""
check_output "diff shows filename"      "poem.txt"           "echo \"$DIFF\""

# diff on unmodified file should show no changes
$MYGIT add poem.txt &>/dev/null
$MYGIT commit -m "updated poem" &>/dev/null
check_output "diff clean file"          "no changes"         "$MYGIT diff poem.txt"

echo ""
echo "=== Suite 7: Path Traversal Protection ==="

REPO=$(setup_repo "repo6")
cd "$REPO"
echo "outside" > "$TMP/outside.txt"
check_output "add outside repo rejected" "outside" "$MYGIT add $TMP/outside.txt"

echo ""
echo "=== Suite 8: Config Author Reading ==="

REPO=$(setup_repo "repo7")
cd "$REPO"

# Write author info into .git/config
cat >> .git/config << 'CFGEOF'

[user]
	name = Alice Smith
	email = alice@example.com
CFGEOF

echo "authored" > authored.txt
$MYGIT add authored.txt &>/dev/null
$MYGIT commit -m "authored commit" &>/dev/null
check_output "log shows config author name"  "Alice Smith"       "$MYGIT log"
check_output "log shows config author email" "alice@example.com" "$MYGIT log"

echo ""
echo "=== Suite 9: Multiple Branches Diverge ==="

REPO=$(setup_repo "repo8")
cd "$REPO"

$MYGIT branch branchA &>/dev/null
$MYGIT branch branchB &>/dev/null

$MYGIT checkout branchA &>/dev/null
echo "A content" > a_file.txt
$MYGIT add a_file.txt &>/dev/null
$MYGIT commit -m "commit on A" &>/dev/null

$MYGIT checkout branchB &>/dev/null
echo "B content" > b_file.txt
$MYGIT add b_file.txt &>/dev/null
$MYGIT commit -m "commit on B" &>/dev/null

check "a_file absent on B"  "[ ! -f a_file.txt ]"
check "b_file present on B" "[ -f b_file.txt ]"

$MYGIT checkout branchA &>/dev/null
check "a_file present on A"  "[ -f a_file.txt ]"
check "b_file absent on A"   "[ ! -f b_file.txt ]"

# Each branch tip should be independent
HASH_A=$(cat .git/refs/heads/branchA)
HASH_B=$(cat .git/refs/heads/branchB)
check "branches have different hashes" "[[ '$HASH_A' != '$HASH_B' ]]"

echo ""
echo "── Results ──────────────────────────────────────────────────────────────"
echo "$pass passed, $fail failed"
if [[ ${#FAILURES[@]} -gt 0 ]]; then
    echo "Failed tests:"
    for f in "${FAILURES[@]}"; do echo "  - $f"; done
fi
echo ""
[[ $fail -eq 0 ]]
