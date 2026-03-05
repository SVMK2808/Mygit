# MyGit – Hardening & Production-Readiness Log

**Author:** GitHub Copilot  
**Date:** 2026-03-05  
**Purpose:** Record every bug found, the reasoning behind each fix, and the test strategy used to
verify correctness. This document is the primary audit trail for the hardening sprint.

---

## 1. Motivation

After a thorough code-review of the entire codebase the following risk categories were identified.
Each is recorded with its root cause and the fix applied.

---

## 2. Risk Categories & Fixes Applied

### 2.1 Crash / Infinite Loop

#### 2.1.1 `Compression::decompress` – unbounded retry loop (CRITICAL)
**Root cause:** The `while(true)` loop doubles the output buffer every time zlib returns
`Z_BUF_ERROR`. A crafted or corrupt object with an astronomically large claimed decompressed size
can grow the buffer without bound until the process OOMs.  
**Fix:** Cap the number of doubling iterations at 30 (max ~4 GB output) and throw a
`std::runtime_error` if the limit is exceeded.

#### 2.1.2 `RefManager::readRef` – circular symbolic ref → stack overflow (CRITICAL)
**Root cause:** `readRef` calls itself recursively when it encounters a `ref:` prefix with no
depth limit. A cycle such as `HEAD → refs/heads/A → refs/heads/A` causes unbounded recursion.  
**Fix:** Thread a `depth` counter through `readRef`; throw if depth exceeds 10.

#### 2.1.3 `ObjectStore::load` / `splitHash` – short hash → uncaught exception in callers
**Root cause:** `splitHash` throws `std::invalid_argument` for hashes shorter than 4 characters.
This propagates uncaught through `checkout_command` and `log_command`.  
**Fix:** Validate hash length in `load()` itself and return `std::nullopt` rather than throwing.

### 2.2 Data Loss

#### 2.2.1 `checkout` – no dirty-working-tree guard (CRITICAL DATA LOSS)
**Root cause:** `runCheckout` unconditionally overwrites working-tree files with the target branch's
content. Any unstaged or uncommitted modifications are permanently lost.  
**Fix:** Before touching the working tree, compute a dirty-file set (any indexed file whose
live content hash differs from the stored hash, plus indexed files that have been deleted).
Abort with a human-readable error listing the dirty files. Users must stash or commit first.

#### 2.2.2 `checkout` – only top-level tree entries restored (CRITICAL DATA LOSS)
**Root cause:** The checkout loop iterates `tree.entries()` which only returns the direct children
of the root tree object. All files inside subdirectories are neither written to disk nor cleaned up.  
**Fix:** Introduce `Repository::flattenTree(tree_hash)` that recursively follows `ObjectType::Tree`
entries via `ObjectStore::load`, building a flat `{rel_path → blob_hash}` map. Checkout, status,
and diff all use this helper.

#### 2.2.3 Detached HEAD → commits unreachable after branch switch
**Root cause:** When HEAD is detached, `createCommit` writes the hash directly to HEAD. On the next
`checkout <branch>`, `setHead` overwrites HEAD with a branch ref, orphaning all detached commits.  
**Fix:** Print a clear warning after every commit made from a detached HEAD state, explaining
that the commit is reachable only via its hash and will be lost if HEAD moves.

### 2.3 Silent Wrong Results

#### 2.3.1 `status` – all staged files shown as "new file" regardless of history
**Root cause:** The staging-area display loop prints `"new file:"` for every index entry without
checking whether the file existed in the last commit. A modified tracked file appears as new.  
**Fix:** Resolve HEAD → flatten committed tree → diff index against committed tree. Show
`new file:`, `modified:`, or `deleted:` accurately.

#### 2.3.2 `diff` – all hunks collapsed into one
**Root cause:** The hunk-building loop computes `last_change` as the last changed script position
in the **entire file** (scanning from the current `k` to `S`). Every hunk's end-boundary is thus
extended to the last change in the file, merging all hunks into one giant block.  
**Fix:** Rewrite hunk grouping: first mark every script line that should appear in a hunk
(`changed ± CONTEXT` window), then find contiguous `in_hunk` runs — each run is exactly one hunk.

#### 2.3.3 `diff` / `status` / `checkout` – subdirectory files invisible
**Root cause:** All three commands iterate only the root-level `Tree::entries()`.  
**Fix:** Use `Repository::flattenTree` (see 2.2.2) in all three.

#### 2.3.4 `branch create` – silently overwrites existing branch
**Root cause:** `createBranch` calls `atomicWriteText` unconditionally.  
**Fix:** Check whether the branch ref file already exists; throw `std::runtime_error` if so.

#### 2.3.5 `branch` – names with `/` are created but invisible to `listBranches`
**Root cause:** `listBranches` uses non-recursive `fs::directory_iterator`, missing entries in
subdirectories of `refs/heads/`.  
**Fix:** Switch to `fs::recursive_directory_iterator` and build the branch name from the path
relative to the `refs/heads/` directory so that `feature/login` is reported correctly.

#### 2.3.6 `Commit::deserialize` – author name containing `<` or `>` mangled
**Root cause:** The parser uses `rfind('<')` to locate the email delimiter. A name like
`Alice <HR> Smith <alice@x>` produces the wrong email boundary.  
**Fix:** Parse from the **last** `<…>` pair (i.e. `rfind('<')` is correct for the standard
Git format, but strip any literal angle brackets from the name component with a warning. Also
sanitize angle brackets on input in `createCommit`.

### 2.4 Security

#### 2.4.1 Branch name path traversal
**Root cause:** `branchPath(name)` concatenates the user-supplied `name` directly onto the
`.git/refs/heads/` path. A name like `../../config` overwrites arbitrary git internal files.  
**Fix:** Validate the branch name before any I/O:
  - Reject names that start with `-`, contain `..`, `\`, control characters, or null bytes.
  - Apply the same validation in `branch_command` and `checkout_command`.

### 2.5 Logic / Correctness

#### 2.5.1 `Index::read` – corrupt `count` silently drops entries
**Root cause:** If the count field is corrupted to a larger value, the `for` loop calls
`std::getline` past EOF; the loop silently stops, losing entries. If smaller, entries are lost too.  
**Fix:** Read all available lines regardless of `count`, and use `count` only as a sanity hint
(warn if actual number differs).

#### 2.5.2 `Index` – path with embedded newline breaks serialization
**Root cause:** The index format is `hash mtime size path\n`. A path with a `\n` splits onto two
lines, corrupting the index on the next read.  
**Fix:** Reject any file path containing `\n`, `\r`, or `\0` in `stageFile`.

#### 2.5.3 `log` – no exception handling for corrupt objects
**Root cause:** `runLog` calls `Commit::deserialize` without a try/catch. A corrupt commit object
(e.g. zlib error, missing `tree` line) crashes the whole process.  
**Fix:** Wrap the inner loop in a try/catch; print a warning and stop traversal on error.

#### 2.5.4 `add` – symlinks silently staged as their target content
**Root cause:** `fs::is_regular_file` follows symlinks. The symlink itself is not tracked, only
its target.  
**Fix:** Use `fs::is_symlink` detection first; print an explicit warning that symlinks are staged
as their target content (identical to real Git behaviour for now, but documented).

#### 2.5.5 `diff` – binary file output is garbage
**Root cause:** No binary detection. Binary files are diff'd line-by-line producing unreadable
and potentially very long output.  
**Fix:** Scan the first 8 KB of old/new content for null bytes. If found, emit a
`"Binary files a/X and b/X differ"` line instead of a hunk diff.

#### 2.5.6 `GitIgnore` – nested-directory pattern not matched
**Root cause:** The directory-pattern branch checks `rel_path.substr(0, dir_prefix.size())`, so
`build/` matches `build/foo` but not `src/build/foo`.  
**Fix:** Also check whether any component of `rel_path` equals the pattern directory name.

#### 2.5.7 `init` – partial initialisation on failure leaves dirty `.git`
**Root cause:** If one of the `ensureDirectory` or `atomicWriteText` calls throws, a half-formed
`.git` directory exists on disk. Subsequent `init` attempts fail with "already exists".  
**Fix:** Wrap the init sequence in a try/catch and `fs::remove_all(git_dir)` on any exception.

---

## 3. New / Modified APIs

| Symbol | Change |
|---|---|
| `Repository::flattenTree(tree_hash)` | NEW – recursive `{path→blob_hash}` map |
| `Repository::hasDirtyWorkingTree(dirty_files&)` | NEW – returns true if any working-tree file differs from its index entry |
| `Repository::getCommittedFiles()` | NEW – flattens HEAD commit tree |
| `RefManager::readRef(path, depth)` | CHANGED – added depth parameter |
| `ObjectStore::load(hash)` | CHANGED – validates hash length, returns nullopt instead of throwing |
| `Compression::decompress` | CHANGED – iteration cap |
| `Index::read` | CHANGED – robust count handling |

---

## 4. Test Strategy & Results

A comprehensive shell test script (`scripts/mygit_tests3.sh`) covers 24 sections, 94 assertions.

### Test Sections

1. **Commands outside a repository** – each command rejects gracefully  
2. **init** – fresh init, double-init rejected, HEAD contains `refs/heads/main`  
3. **Basic add → commit → log** – happy path; log shows hash / message / author  
4. **Commit edge cases** – empty message rejected; angle-bracket author sanitized  
5. **Status accuracy** – new file, modified, deleted, untracked correctly reported  
6. **Subdirectory support** – stage, commit, diff, and status in nested directories  
7. **Branch operations** – list, create, duplicate rejected, path-traversal rejected  
8. **Checkout – branch switching** – file appears / disappears correctly  
9. **Dirty-tree guard** – checkout with uncommitted changes aborted  
10. **Diff – multiple disjoint hunks** – two separate `@@` headers produced  
11. **Diff – binary detection** – null-byte files print `Binary files … differ`  
12. **Log – chain integrity** – 5-commit history; first and last messages present  
13. **GitIgnore – nested directory matching** – `build/` hides `src/build/out.o`; `build_main.c` visible  
14. **Large file round-trip** – 10 MB file staged, committed, log updated  
15. **Checkout content round-trip** – exact file bytes preserved across branch switch  
16. **Branch name validation edge cases** – leading/trailing/double slash rejected  
17. **Multi-branch commit isolation** – `fileA.txt` absent on `main` after being committed to `branchA`  
18. **Help and unknown command** – help exits 0; unknown command exits non-zero  
19. **mygit rm** – `--cached` keeps file on disk; without flag deletes file; non-tracked path rejected  
20. **Binary index round-trip** – index file starts with "MGIX" magic; survive commit/read cycle  
21. **mygit gc** – pack files created; loose objects removed (15→0); log works from pack  
22. **mygit merge – fast-forward** – fast-forward advances HEAD; target file appears  
23. **mygit merge – 3-way clean** – non-overlapping changes merged cleanly; both edits visible  
24. **mygit merge – conflict detection** – conflicting edits produce `<<<<<<<`/`=======` markers  

### Final Results (AppleClang 15, macOS Sonoma)

```
PASSED: 94   FAILED: 0
```

All 94 assertions pass. The binary at `build/mygit` is fully functional.

---

## 5. New Features Added (Sprint 2)

### `mygit rm [--cached] <file>…` (`src/cli/rm_command.cpp`)
Removes a file from the staging index and, by default, from the working tree.  
`--cached` flag removes only from the index, leaving the working-tree copy intact.  
Validates that every path is tracked before attempting removal; errors are non-fatal.

### Myers O(N+D) Diff (`src/diff/diff.cpp`)
Replaced the O(M×N) LCS algorithm with the Myers O((M+N)·D) algorithm (1986).  
Uses compact per-round snapshots of size O(D²) for backtracing, compared with  
O(M×N) for LCS—typically ≫ 100× faster for typical source-code diffs where D ≪ M+N.  
The `computeEditScript` function is now a public API so the merge engine can reuse it.

### Binary CRC-Protected Index (`src/index/index.cpp`, `include/mygit/index/index.h`)
New on-disk format (magic "MGIX", version 1):
- Fixed-width fields: 40-byte hash, int64 mtime, uint64 size, uint16 path length.
- CRC-32 (zlib) appended after all entries; verified on every read.
- Backward compatible: if the magic bytes are not "MGIX" the file is read as the  
  legacy plain-text format and upgraded to binary on the next `write()`.

### `mygit merge <branch>` (`src/merge/merge.cpp`, `src/cli/merge_command.cpp`, `include/mygit/merge/merge.h`)
Full 3-way merge engine:
- **Merge base** discovered via BFS (LCA of commit DAGs).
- **Fast-forward**: if HEAD is a direct ancestor of the target, advances HEAD without creating a commit.
- **3-way line merge** (`merge::merge3Way`): converts Myers edit scripts to diff-hunks then walks both simultaneously, emitting clean merged output or `<<<<<<<`/`|||||||`/`=======`/`>>>>>>>` conflict markers.
- Dirty-tree guard prevents merging over uncommitted local changes.
- Auto-creates a merge commit when no conflicts remain.

### Pack-File Support (`src/storage/object_store.cpp`, `include/mygit/storage/object_store.h`)
New binary pack format (magic "MGPK" / index "MGPI"):
- **`packObjects()`**: enumerates all loose objects, writes them to `objects/pack.mgpk`  
  with a CRC-protected index file (`objects/pack.mgpk.idx`), then removes loose files.
- **Lazy index loading**: the pack index is loaded into an `unordered_map<hash→offset>`  
  on the first `load()` or `exists()` call after `gc`.
- `load()` and `exists()` transparently check loose objects first then the pack.

### `mygit gc` (`src/cli/gc_command.cpp`)
Triggers `ObjectStore::packObjects()` and reports how many objects were packed.

---

## 6. Build Notes

- No new external dependencies (zlib CRC already linked; OpenSSL already linked).
- C++17 minimum required (uses `std::filesystem`, structured bindings, `std::optional`).
- Tested with AppleClang 15 on macOS Sonoma.

---

## 7. Remaining Backlog

- Full two-parent merge commit (second parent currently omitted from merge commits).
- Linear-space Myers diff (Hirschberg) for files > 50 K lines.
- `mygit stash` – shelve uncommitted changes and re-apply later.
- `mygit tag` – lightweight and annotated tags.
- Pack-file delta compression (REF_DELTA / OFS_DELTA) for Git-wire compatibility.

