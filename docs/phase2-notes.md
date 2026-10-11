# Phase 2 notes: concurrency

Read this alongside the code. Every file mentioned has comments that say the
same things at the exact line where they apply.

## The pieces

| File | Role |
|---|---|
| `include/ob/command.hpp` | `Command`: a small value type carrying one add or cancel. Copyable, no pointers, safe to move between threads. |
| `include/ob/blocking_queue.hpp` | `BlockingQueue<T>`: mutex + condition variable FIFO. All of 2a's synchronisation lives here. |
| `include/ob/engine.hpp`, `src/engine.cpp` | `Engine` (2a): producers call `submit()`, one consumer thread owns the `OrderBook`. `apply()` is the shared "one command onto one book" function. |
| `include/ob/locked_book.hpp` | `LockedBook` (2b): the same API as `OrderBook`, every call under one `std::mutex`. |
| `include/ob/order_gen.hpp` | `OrderGenerator`: seeded, deterministic command stream. Each generator owns an id range. |
| `src/sim_main.cpp` | `ob_sim`: drives all three paths with real threads. Use it for the LLDB checkpoints. |
| `tests/test_concurrency.cpp` | determinism test, stress tests with conservation checks, queue tests. |

## The four things to be able to explain

### What a data race is
Two threads access the same memory location, at least one of them writes, and
nothing orders the two accesses. In C++ this is undefined behaviour, not
"sometimes wrong". The compiler is allowed to assume it never happens and
optimise accordingly, so the failure can look like anything: a corrupted
container, a lost update, a crash far from the cause, or nothing at all until
the machine is under load.

Concrete example here: build with `-DOB_RACE_DEMO=ON` and run the LockedBook
stress test. Four threads call `add()` and `cancel()` on the same book with no
lock. TSan reports races inside `unordered_map` (the index) and `std::map`
(the levels): one thread is rehashing while another is inserting.

### Why a mutex fixes it
A mutex makes a region of code mutually exclusive: only one thread can be
inside at a time, the others wait. That gives two guarantees. Exclusion: the
interleaving that corrupts the container can't happen. Visibility: unlocking
publishes every write made inside the region, and locking afterwards sees them.
That second part matters as much as the first. Without it, thread B could hold
the lock yet still read stale values thread A wrote.

`LockedBook` uses one mutex for everything. Correct, simple, and from the
book's point of view indistinguishable from single-threaded use.

### What a condition variable is for
Waiting for a *condition* to become true without burning CPU. The consumer in
`BlockingQueue::pop()` wants "queue not empty". It could loop on the lock
checking `empty()`, but that is a spin: a core at 100% doing nothing. Instead
it calls `cv.wait(lock, pred)`, which atomically releases the mutex and sleeps.
A producer pushes and calls `notify_one()`, the consumer wakes, re-acquires
the mutex, re-checks the predicate, and proceeds.

Two rules that are easy to get wrong and are both handled in the code:
always wait in a loop (spurious wakeups are legal), and always check the
predicate under the lock (otherwise a push can slip into the gap between
"check" and "sleep" and its notify is lost forever).

### Coarse vs fine locking
Coarse (what `LockedBook` does): one lock around everything. Easy to reason
about, impossible to deadlock with itself, zero parallelism inside the book.
Four threads calling it are effectively taking turns, which is why `ob_sim
--mode locked --producers 4` is *slower* than `--producers 1`: the same work
plus contention.

Fine: separate locks per side, per level, or lock-free structures. More
parallelism, but now you have lock ordering (take bids before asks, always),
deadlock risk, and matching that must hold both sides' locks anyway. For an
order book, where every add may touch both sides, the win is small and the
complexity is large. This is why real matching engines usually pick the 2a
shape instead: one thread owns the book, and the parallelism lives in the
pipeline around it (parsing, risk checks, publishing).

## Why 2a needs no lock on the book

`Engine::book_` is private, and `run()` is the only function that reads or
writes it. `run()` executes on exactly one thread. Producers interact with the
Engine solely through `submit()`, which touches `queue_`, not `book_`. All
cross-thread traffic funnels through `BlockingQueue`, whose mutex orders it.
Then `stop()` joins the consumer, and `std::thread::join()` guarantees that
everything the joined thread did is visible to the caller. So the main thread
can read `book()` afterwards with no lock and no race.

This is the "single writer" pattern: don't share, hand off. It is the
simplest correct concurrent design and often the fastest.

## What the tests prove

- **Determinism** (`Engine with one producer matches...`): one producer plus a
  FIFO queue preserves order, so the engine's final book, stats, and complete
  trade log equal the single-threaded run. Byte for byte.
- **Conservation** (both stress tests): with N threads the interleaving is
  random, so we can't compare to a reference. Instead we check an identity
  that must hold for *every* interleaving: every accepted add is exactly one
  of resting, cancelled, or fully filled, and the trade log accounts for all
  traded quantity. Lost or duplicated commands break the identity.
- **TSan clean**: the sanitizer instruments every memory access and checks
  that conflicting accesses are ordered by a lock or join. Silence means no
  race was *observed*; it's not a proof, but with the stress test's volume it
  is strong evidence.

## Things to try with ob_sim

```
./build/debug/ob_sim --mode sequential --orders 200000 --seed 7
./build/debug/ob_sim --mode engine --producers 1 --orders 200000 --seed 7   # same fingerprint
./build/debug/ob_sim --mode engine --producers 4 --orders 200000 --seed 7   # different every run
./build/debug/ob_sim --mode locked --producers 4 --orders 200000 --seed 7   # slower than 1 producer
```
