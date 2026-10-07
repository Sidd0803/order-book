# order_book

A C++20 limit order book built as a learning project. See `SPEC.md` for the
phases and `CLAUDE.md` for working conventions.

## Setup

### macOS
```
xcode-select --install     # clang++ and lldb
brew install cmake
```

### Windows (recommended: WSL2 + Ubuntu)
The build uses Clang/GCC-style flags and LLDB, and Phase 2 needs
ThreadSanitizer, which has no native Windows build. WSL gives you the same
toolchain as macOS/Linux:

```
wsl --install -d Ubuntu          # from an admin PowerShell, then reboot
# inside Ubuntu:
sudo apt update && sudo apt install -y build-essential clang lldb cmake git
git clone <repo-url> && cd order_book
```

Keep the clone inside the WSL filesystem (e.g. `~/order_book`), not under
`/mnt/c/...`; builds there are much slower. In VS Code, the "WSL" extension
opens the folder directly inside Ubuntu.

Native MSVC also compiles the project (the CMake file guards the flags), but
only AddressSanitizer is available there and the LLDB checkpoints assume
Clang/LLDB.

### Linux
```
sudo apt install -y build-essential clang lldb cmake
```

## Build and test
```
cmake -S . -B build/debug -DCMAKE_BUILD_TYPE=Debug
cmake --build build/debug
ctest --test-dir build/debug --output-on-failure

# sanitizers
cmake -S . -B build/asan -DCMAKE_BUILD_TYPE=Debug -DOB_SANITIZE=address,undefined
cmake -S . -B build/tsan -DCMAKE_BUILD_TYPE=Debug -DOB_SANITIZE=thread

# CLI
./build/debug/ob_cli examples/sweep.txt
```

The first configure downloads Catch2 via CMake FetchContent, so it needs
network access once.
