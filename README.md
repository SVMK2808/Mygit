# MyGit

A lightweight, Git-compatible version control system implemented from scratch in C++17. MyGit replicates the core internals of Git — content-addressable object storage, recursive tree objects, refs, branches, and a staging index — while adding production-quality features like atomic writes, `.gitignore` support, and a unified diff engine.

---

## Features

| Category | Details |
|---|---|
| **Object model** | Blob, Tree (recursive subdirectory support), Commit — stored as zlib-compressed, SHA-1-addressed objects |
| **Index** | Persistent staging area with atomic writes and POSIX `stat()` mtime |
| **Refs** | Loose refs + `packed-refs` fallback, atomic HEAD updates |
| **Branches** | Create, list (with `*` marker), switch |
| **Checkout** | Restores files from target tree; deletes files absent in the target branch |
| **Diff** | LCS-based unified diff between HEAD and working tree |
| **.gitignore** | Full glob support: `*`, `**`, `?`, directory patterns (`build/`) |
| **Config** | Reads `[user] name` / `email` from `.git/config` |
| **Security** | Path traversal protection on `add` |
| **Atomic I/O** | All index/ref/HEAD writes go through a `.tmp` → `rename` pattern |

---

## Requirements

- macOS or Linux
- CMake ≥ 3.20
- C++17-capable compiler (tested with AppleClang 17 / GCC 12+)
- OpenSSL 3 (`brew install openssl@3` on macOS)
- zlib (bundled on macOS; `apt install zlib1g-dev` on Ubuntu)

---

## Building

```bash
# Clone
git clone https://github.com/SVMK2808/Mygit.git
cd Mygit

# Configure (macOS / Apple Silicon)
cmake -B build \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_OSX_ARCHITECTURES=arm64 \
  -DOPENSSL_ROOT_DIR=$(brew --prefix openssl@3)

# Configure (Linux)
cmake -B build -DCMAKE_BUILD_TYPE=Release

# Build
cmake --build build --parallel

# Binary
./build/mygit help
```

---

## Usage

### Initialize a repository

```bash
mygit init [path]
# Initialized empty mygit repository in ./path/.git
```

### Stage files

```bash
mygit add file.txt
mygit add src/main.cpp src/utils/helper.cpp   # subdirectories supported
```

Files matching `.gitignore` patterns are skipped automatically.

### Commit

```bash
mygit commit -m "your commit message"
# [a3f9c12] your commit message
```

Author name and email are read from `.git/config`:

```ini
[user]
    name  = Alice Smith
    email = alice@example.com
```

Or override per-command via environment variables:

```bash
GIT_AUTHOR_NAME="Bob" GIT_AUTHOR_EMAIL="bob@example.com" mygit commit -m "bob's commit"
```

### View history

```bash
mygit log
# commit a3f9c12e...
# Author: Alice Smith <alice@example.com>
# Date:   2026-03-04 12:00:00
#
#     your commit message
```

### Check status

```bash
mygit status
# Changes staged for commit:
#   new file:   src/main.cpp
#
# Changes not staged for commit:
#   modified:   README.md
#
# Untracked files:
#   scratch.txt
```

### Diff working tree against HEAD

```bash
mygit diff [file]
# --- a/poem.txt
# +++ b/poem.txt
# @@ -1,3 +1,4 @@
#  line 1
# -line 2
# +line 2 changed
#  line 3
# +line 4 new
```

### Branches

```bash
mygit branch              # list branches
mygit branch feature      # create branch
mygit checkout feature    # switch branch
```

### .gitignore

Place a `.gitignore` file in the repository root. Supported patterns:

```
*.log           # wildcard extension
build/          # entire directory
src/**/*.o      # recursive glob
debug?.txt      # single character wildcard
```

---

## Project Structure

```
.
├── include/mygit/
│   ├── cli/          command.h
│   ├── core/         object.h, types.h
│   ├── diff/         diff.h
│   ├── index/        index.h
│   ├── objects/      blob.h, tree.h, commit.h
│   ├── refs/         ref_manager.h
│   ├── repository/   repository.h
│   ├── storage/      object_store.h, file_utils.h
│   └── utils/        hash.h, compression.h, gitignore.h
├── src/              implementation (.cpp files mirroring include/)
├── scripts/
│   ├── mygit_tests.sh    primary test suite  (47 tests)
│   └── mygit_tests2.sh   extended test suite (39 tests)
└── CMakeLists.txt
```

---

## Running Tests

```bash
bash scripts/mygit_tests.sh
bash scripts/mygit_tests2.sh
```

All 86 tests pass. Coverage includes: init, add, commit, log, status, branch, checkout, diff, `.gitignore`, subdirectory trees, path traversal protection, config author reading, object store integrity, packed-refs, and multi-branch divergence.

---

## Architecture

```
CLI commands
     │
     ▼
Repository          ← high-level operations (stageFile, createCommit)
  ├── ObjectStore   ← zlib-compressed, SHA-1-addressed object files
  ├── Index         ← staging area (text format, atomic writes)
  └── RefManager    ← loose refs + packed-refs, atomic HEAD
         │
         ▼
    Objects: Blob / Tree / Commit
         │
         ▼
    Utils: Hash (EVP SHA-1), Compression (zlib), GitIgnore, FileUtils
```

---

## Known Limitations

- No remote support (fetch/push/pull)
- No merge or rebase
- No delta compression in the object store
- SHA-1 only (no SHA-256 mode)
- Single `.gitignore` at repo root (no per-directory ignore files)

---

## License

[MIT](LICENSE)
