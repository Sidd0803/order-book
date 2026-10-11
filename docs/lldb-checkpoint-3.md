# LLDB checkpoint 3 (Phase 2, #1): two threads side by side

Goal: stop a producer and the consumer at the same moment and look at both.

```
cmake --build build/debug
lldb build/debug/ob_sim
```

## Break where the consumer mutates the book

```
breakpoint set --name ob::Engine::run
breakpoint set --file engine.cpp --line 47     # the apply(book_, *cmd, ...) call
run --mode engine --producers 2 --orders 20000
```

First stop is run() starting on the consumer thread. Continue once to reach
the apply() line: the consumer is about to mutate shared state.

```
c
thread list
```

You will see the main thread (waiting in join), one or two producer threads
(in submit, or already finished), and the consumer, marked with `*` as the
selected thread. Note each thread's current function in the listing.

## See everything at once

```
bt all
```

One backtrace per thread. Read them top to bottom and answer: which thread is
inside `BlockingQueue::push`? Which is inside `pop`? Which is in `join`? This
single picture is the producer/consumer design.

## Switch threads

```
thread select 2          # pick a producer's number from `thread list`
frame variable           # that producer's locals: its generator, loop index
up                       # walk up its stack into main's lambda
thread select 1          # back to the main thread; it's blocked in join()
```

Then return to the consumer and look at what it's about to apply:

```
thread select <consumer number>
frame variable cmd
p *cmd
p book_.order_count()
p queue_.size()          # how far ahead the producers are
```

## Watch the queue depth move

Put a breakpoint inside pop() and continue a few times, printing
`items_.size()` each stop. Producers are faster than the consumer, so the
depth tends to climb. This is the unbounded-queue tradeoff from the notes.

## What to notice

- Only the consumer's backtrace ever contains `OrderBook::add` or `cancel`.
  Producers never reach the book. That is why it needs no lock.
- A producer stopped inside `push` and the consumer stopped inside `pop` are
  both inside `BlockingQueue`; the mutex is what prevents them being inside
  the deque at the same time.
- `thread list` shows thread states. A thread blocked on a mutex or condvar
  will be in `__psynch_cvwait` or `__psynch_mutexwait` at the top of its
  stack. That is what "waiting" looks like from the debugger.

Log a few lines in `docs/debugging-log.md`.
