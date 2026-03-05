# MyGit

A production-grade, Git-inspired version control system built from scratch in **C++17**.  
MyGit replicates Git's core object model, staging index, refs, branches, diff, and merge —
then extends it with a binary CRC-protected index, Myers O(N+D) diff, pack-file storage,
and a full 3-way merge engine.

---

## Feature overview

| Category | Details |
|----------|---------|
| **Object model** | `Blob`, `Tree` (recursive subdirectory support), `Commit` — stored as zlib-compressed, SHA-1-addressed loose objects or in a binary pack file |
| **Staging index** | Binary MGIX format with CRC-32 integrity check; backward-compatible with legacy text format; atomic writes |
| **Refs** | Loose refs under `refs/heads/`; symbolic refs (e.g. `HEAD`); circular-ref guard (depth > 10); `packed-refs` fallback handled |
| **Branches** | Create, list (with `*` marker), switch; strict name validation (no `..`, no backslash, no path traversal) |
| **Checkout** | Recursively restores full directory trees; dirty-tree guard; cleans up files absent in target branch |
| **Status** | Accurately compares the staging index against the HEAD commit tree and the working tree |
| **Diff** | Myers O(N+D) unified diff; correct multi-hunk output for disjoint changes; binary-file detection |
| **Merge** | BFS merge-base discovery; fast-forward and 3-way line-level merge; diff3-style conflict markers |
| **rm** | Remove files from index only (`--cached`) or from index + working tree |
| **gc** | Packs all loose objects into a binary pack file with a CRC-protected index; transparent read-through |
| **.gitignore** | Glob support: `*`, `**`, `?`, directory patterns (`build/`), nested directory matching |
| **Security** | Path traversal protection on `add` and branch names; embedded newline/null rejection in index paths |
| **Atomic I/O** | All index/ref/HEAD/pack writes go through `.tmp -> rename` — crash-safe |

---

## Requirements

| Tool | Version |
|------|---------|
| CMake | >= 3.20 |
| C++ compiler | C++17 (tested: AppleClang 15, GCC 12+) |
| OpenSSL | 3.x (`brew install openssl@3` on macOS) |
| zlib | bundled on macOS; `apt install zlib1g-dev` on Ubuntu |

---

## Building

```bash
# macOS / Apple Silicon
cmake -B build \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_OSX_ARCHITECTURES=arm64 \
  -DOPENSSL_ROOT_DIR=$(brew --prefix openssl@3)

# Linux
cmake -B build -DCMAKE_BUILD_TYPE=Release

# Compile
cmake --build build --parallel

# Verify
./build/mygit help
```

---

## Command reference

### `mygit init [path]`
Create a new repository.  Defaults to the current directory.
```bash
mygit init
mygit init /tmp/my-project
```

### `mygit add <file> [files...]`
Stage one or more files.  Files matching `.gitignore` patterns are skipped.
```bash
mygit add README.md
mygit add src/main.cpp src/utils/helper.cpp
```

### `mygit rm [--cached] <file> [files...]`
Remove files from the staging index.  Without `--cached`, also deletes the working-tree copy.
```bash
mygit rm build/output.o        # remove from index and disk
mygit rm --cached secrets.txt  # remove from index only; keep file
```

### `mygit commit -m <message>`
Create a commit from all staged files.  Author name and email are read from `.git/config`:
```ini
[user]
    name  = Alice Smith
    email = alice@example.com
```
```bash
mygit commit -m "feat: add login endpoint"
# [a3f9c12] feat: add login endpoint
```

### `mygit status`
Show staged changes, unstaged changes, and untracked files.

### `mygit log`
Show the commit history from HEAD, walking parent links.

### `mygit diff [file]`
Compare the working tree against the HEAD commit using Myers O(N+D) diff.
```diff
diff --mygit a/src/main.cpp b/src/main.cpp
--- a/src/main.cpp
+++ b/src/main.cpp
@@ -10,6 +10,7 @@
     int x = 1;
-    return x;
+    x += 1;
+    return x;
 }
```

### `mygit branch [name]`
Without arguments, lists branches (`*` marks the current).  With a name, creates a new branch at HEAD.

### `mygit checkout <branch>`
Switch to an existing branch.  Refuses if there are uncommitted changes in the working tree.

### `mygit merge <branch>`
Merge a branch into the current branch.

- **Already up-to-date:** no action.
- **Fast-forward:** advances HEAD to the target tip; updates the working tree.
- **3-way merge:** merges line-level changes from both branches.  Conflicting files receive
  diff3-style conflict markers; the command exits with code 1.

```bash
mygit merge feature-login
# Fast-forward
# or
# CONFLICT: automatic merge failed; fix conflicts and then commit.
#     src/server.cpp
```

To resolve, edit the file to remove the `<<<<<<<` / `=======` / `>>>>>>>` markers,
then `mygit add` it and `mygit commit`.

### `mygit gc`
Pack all loose objects into a binary pack file, then delete the loose files.
Future `load()` and `exists()` calls are transparently routed through the pack.
```bash
mygit gc
# Running garbage collection...
# Packed 47 objects.
# Pack now contains 47 objects total.
```

### `.gitignore`
```
*.log           # wildcard extension
build/          # entire directory
src/**/*.o      # recursive glob
debug?.txt      # single-character wildcard
!important.log  # negation (un-ignore)
```

---

## Running tests

```bash
# Comprehensive integration suite (94 assertions)
bash tests/integration/mygit_tests3.sh

# Earlier partial suites (kept for reference)
bash tests/integration/mygit_tests.sh
bash tests/integration/mygit_tests2.sh
```

All 94 assertions pass.  Coverage: init, add, rm, commit, log, status, branch, checkout,
diff (multi-hunk, binary), merge (fast-forward, clean 3-way, conflict detection), gc
(pack creation, transparent reads), binary index round-trip, `.gitignore` nested matching,
large-file (10 MB) round-trip, path-traversal protection.

---

## Project structure

```
.
├── include/mygit/
│   ├── cli/        command.h            — CLI result type + command declarations
│   ├── core/       object.h, types.h    — base Object; IndexEntry, TreeEntry, ...
│   ├── diff/       diff.h               — FileDiff, Hunk, computeDiff, computeEditScript
│   ├── index/      index.h              — MGIX binary index + legacy text fallback
│   ├── merge/      merge.h              — findMergeBase, merge3Way, MergeResult
│   ├── objects/    blob.h, tree.h, commit.h
│   ├── refs/       ref_manager.h        — branch/HEAD resolution; circular-ref guard
│   ├── repository/ repository.h         — high-level ops; flattenTree, hasDirtyWorkingTree
│   ├── storage/    object_store.h, file_utils.h
│   └── utils/      hash.h, compression.h, gitignore.h
│
├── src/            — implementation (.cpp mirrors include/)
│   ├── cli/        add, rm, commit, status, log, branch, checkout, diff, merge, gc, init
│   ├── diff/       Myers O(N+D) edit-script + hunk builder
│   ├── merge/      BFS LCA + 3-way line merge
│   ├── objects/    blob.cpp, tree.cpp, commit.cpp
│   ├── refs/       ref_manager.cpp
│   ├── repository/ repository.cpp
│   ├── storage/    loose + MGPK pack storage
│   └── utils/      hash.cpp, compression.cpp, gitignore.cpp
│
├── tests/
│   └── integration/
│       ├── mygit_tests3.sh   — canonical suite (24 sections, 94 assertions) <- run this
│       ├── mygit_tests.sh    — earlier partial suite
│       └── mygit_tests2.sh   — earlier partial suite
│
├── docs/
│   └── BUILD_JOURNEY.md      — detailed design decisions and implementation narrative
│
├── DEVLOG.md                  — sprint-by-sprint bug-fix and feature audit log
└── CMakeLists.txt
```

---

## Architecture

```
CLI layer  (src/cli/*.cpp)
     |  parse args, format output, handle errors
     v
Repository  (repository.cpp)
  +-- stageFile()           hash blob, update index
  +-- createCommit()        build tree, write commit, advance HEAD
  +-- flattenTree()         recursive {path->hash} from a tree object
  +-- getCommittedFiles()   flattenTree(HEAD commit tree)
  +-- hasDirtyWorkingTree() diff index vs working tree (size then hash)
       |
       +-- ObjectStore ------ loose files + MGPK pack (transparent to callers)
       +-- Index       ------ MGIX binary format with CRC-32
       +-- RefManager  ------ loose refs; symbolic ref resolution; circular guard
             |
             v
        Blob / Tree / Commit  (serialize / deserialize)
             |
             v
        Hash (OpenSSL EVP SHA-1), Compression (zlib), GitIgnore, FileUtils

Diff layer   computeEditScript() — Myers O(N+D) with compact backtrace snapshots
             computeDiff()       — hunk builder

Merge layer  findMergeBase()     — BFS LCA on commit DAG
             merge3Way()         — walk base; combine / conflict-mark overlapping hunks
```

---

## On-disk formats

### Loose object  (`objects/ab/cdef...`)
```
<type> <byte-length>\0<payload>     (zlib-compressed)
```

### MGPK pack file  (`objects/pack.mgpk`)
```
"MGPK" | uint32 version | uint32 count
  [ char[40] hash | uint32 data_len | zlib-payload ] x count
uint32 CRC-32
```

### MGPI pack index  (`objects/pack.mgpk.idx`)
```
"MGPI" | uint32 version | uint32 count
  [ char[40] hash | uint64 offset_in_pack ] x count  (sorted by hash)
uint32 CRC-32
```

### MGIX staging index  (`.git/index`)
```
"MGIX" | uint32 version | uint32 count | uint32 reserved
  [ char[40] hash | int64 mtime | uint64 file_size | uint16 path_len | path ] x count
uint32 CRC-32
```

---

## Known limitations / remaining backlog

- Merge commits record only one parent; the second parent is not yet stored in the commit object.
- No `mygit stash`, `mygit tag`, `mygit remote`, `mygit fetch`, `mygit push`.
- Pack files store objects verbatim (no delta compression); pack rebuilt from scratch on every `gc`.
- Single `.gitignore` at repository root (no per-directory ignore files).
- SHA-1 only; no SHA-256 object addressing.

---

## License

[MIT](LICENSE)
