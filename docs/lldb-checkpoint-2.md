# LLDB checkpoint 2: find a planted iterator bug

Goal: reproduce a real memory bug, find it with the debugger and ASan, and
only then read the code. The bug is on the branch `checkpoint2-bug`.

## Rules

1. Do not diff the branch against main until you have found the bug.
2. Form a hypothesis from the debugger output before reading `order_book.cpp`.
3. Write up what you found in `docs/debugging-log.md` using the template.

## Reproduce

```
git switch checkpoint2-bug
cmake --build build/debug
ctest --test-dir build/debug
```

Some tests fail. Pick one failing test and run it on its own with Catch2's
verbose reporter to see which assertion breaks and what value it saw:

```
./build/debug/ob_tests "<test name>" -s
```

Note which operations the test did before the assertion. That is your
reproduction recipe.

## Hunt with ASan first

Build the sanitized tree and run the same test:

```
cmake --build build/asan
./build/asan/ob_tests "<test name>"
```

AddressSanitizer reports use-after-free and heap-buffer-overflow with two
stack traces: where the bad access happened, and where the memory was freed.
Read both. The freed-at trace tells you which operation destroyed the object;
the access trace tells you who touched it afterward.

Questions to answer from the report alone:
- What type of object was freed? (ASan prints the allocation size; a
  `std::list` node for `Order` is about 48 bytes on this platform.)
- Which function freed it, and which function read it afterward?
- Are they the same function?

## Hunt with LLDB

Run the failing test under lldb and stop at the ASan report:

```
lldb ./build/asan/ob_tests
run "<test name>"
```

ASan traps into the debugger at the moment of the bad access. Then:

```
bt                   # which line in order_book.cpp is executing?
frame select <N>     # jump to the OrderBook frame
frame variable       # what do the locals look like? anything that should be valid but isn't?
```

Now try the plain Debug build (no ASan). Set a breakpoint on the function ASan
blamed and step through with `n`, printing the suspicious variable before and
after each line. The moment you see a value that should not have changed, you
have found the line.

## Fix

Fix it on the branch, rebuild both trees, confirm all tests pass under Debug
and ASan, and compare with main:

```
git diff main -- src/order_book.cpp
```

Your fix should match main in spirit. Then switch back:

```
git switch main
```

## What to write in the log

Symptom, how you found it (which tool first, what it showed), root cause in
one sentence, the general rule. The general rule here is one of the most
common C++ bugs there is, and interviewers love it.
