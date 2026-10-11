# LLDB checkpoint 4 (Phase 2, #2): catch the race, then confirm with TSan

Goal: see the broken state with your own eyes in the debugger, then watch
TSan name the same race.

## Build without the lock

```
cmake -S . -B build/race -DCMAKE_BUILD_TYPE=Debug -DOB_RACE_DEMO=ON
cmake --build build/race
```

This compiles `LockedBook` with a `NoLock` that locks nothing. Code shape
identical, mutex never taken.

## Reproduce

```
./build/race/ob_sim --mode locked --producers 4 --orders 400000
```

Run it several times. Possible outcomes: a crash (segfault or abort inside
`std::map` or `unordered_map`), a hang, or a run that "works" with a resting
count that doesn't add up. All three are the same bug. If it keeps working,
raise `--orders` or `--producers`; the race needs the threads to collide.

## Catch it in LLDB

```
lldb build/race/ob_sim
run --mode locked --producers 4 --orders 400000
```

When it crashes, lldb stops at the faulting instruction:

```
bt                       # a crash deep in libc++ map/hash internals
thread list              # which thread crashed? what are the others doing?
bt all                   # at least one other thread is ALSO inside OrderBook::add/cancel
```

That last observation is the race made visible: two threads simultaneously
inside a data structure that assumes one caller.

If it doesn't crash, break inside the book and inspect state by hand:

```
breakpoint set --name ob::OrderBook::cancel
run --mode locked --producers 4 --orders 400000
thread list
bt all
```

With several threads inside add/cancel at once, look at `index_.size()` and
`bids_.size()` across stops and compare with what `dump()` says. Mismatches
are a corrupted container mid-flight.

## Confirm with TSan

```
cmake -S . -B build/tsan-race -DCMAKE_BUILD_TYPE=Debug -DOB_SANITIZE=thread -DOB_RACE_DEMO=ON
cmake --build build/tsan-race
./build/tsan-race/ob_tests "[locked]"
```

TSan prints `WARNING: ThreadSanitizer: data race` with two stacks: the
write and the conflicting read or write, each tagged with its thread, plus
where each thread was created. Match the frames to what you saw in lldb.

## Then fix it

Rebuild with the option OFF (or just use `build/tsan`, which never had it):

```
./build/tsan/ob_tests "[locked]"
```

Silence. Same code, one `lock_guard` per call.

## Log it

Two entries: what the debugger showed (threads, stacks, state) and what TSan
showed (the two access sites). Finish with the one-sentence root cause and
the rule: shared mutable state needs a happens-before relationship between
every pair of conflicting accesses, and a mutex is the simplest way to get
one.
