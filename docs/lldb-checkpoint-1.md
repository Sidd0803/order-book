# LLDB checkpoint 1: step through a multi-level sweep

Goal: watch the matching loop consume three ask levels one order at a time,
and see `bids_`, `asks_`, and `index_` change under you.

Build type matters: use `build/debug` (`-O0 -g`). In an optimized build the
locals you want to inspect will be "optimized out".

## Setup

```
cmake --build build/debug
lldb build/debug/ob_cli
```

Inside lldb (the `(lldb)` prompt is implied):

```
breakpoint set --name OrderBook::match
run examples/sweep.txt
```

The program runs until the first time `match()` is entered, which happens on
the very first ADD (even though the book is empty and nothing crosses).
Continue until the interesting call:

```
c
c
c
c
```

You are now inside `match()` for order 5 (`ADD 5 B 103 25`), the buy that
sweeps 101, 102, and part of 103. Confirm with:

```
frame variable taker
```

You should see `id = 5, side = Buy, price = 103, qty = 25`.

## Walk the outer loop

```
n
```

Press `n` repeatedly and watch the line marker move. Useful commands as you go:

```
frame variable                 # every local in this frame
p level_price                  # the level we are about to try
p crosses                      # did the taker reach it?
p opposite.size()              # number of ask levels remaining
p taker.qty                    # remaining taker quantity
```

`opposite` is a `std::map`; printing the whole thing is noisy. Ask for the
pieces you care about:

```
p opposite.begin()->first              # best ask price
p opposite.begin()->second.size()      # orders at that level
p opposite.begin()->second.front()     # the maker about to be hit
```

## Watch a variable instead of stepping

A watchpoint stops execution whenever a memory location changes, no matter
where in the code the write happens:

```
watchpoint set variable taker.qty
c
```

Each `c` now runs to the next time `taker.qty` is written and shows old and
new values. You should see 25 -> 15 -> 5 -> 0. Delete it when done:

```
watchpoint delete
```

## See the book from inside the debugger

`dump()` is a const method, so you can call it from any stopped frame where
`this` is an OrderBook:

```
p this->dump()
```

Do this before and after `opposite.erase(level_it)` to see a level disappear.
Also check the index shrinking as makers fill:

```
p index_.size()
```

## Step into the inner loop

Set a breakpoint on the trade push and run to it:

```
breakpoint set --file order_book.cpp --line <line of trades.push_back>
c
frame variable maker fill
```

Expect three stops: maker 1 fill 10, maker 2 fill 10, maker 3 fill 5.

## Conditional breakpoints

Make the match breakpoint fire only for the sweeping order:

```
breakpoint list
breakpoint modify -c 'taker.id == 5' 1
```

Then `run` again and it skips straight to the interesting call.

## What to notice (write a few lines in debugging-log.md)

- `opposite.begin()` is re-read every outer iteration. After `erase`, the old
  `level_it` is garbage; the loop never touches it again. This is the
  iterator-invalidation rule that checkpoint 2 breaks on purpose.
- `maker` is a reference into the list. `pop_front()` destroys exactly the node
  it refers to, so it is used for the last time *before* the pop.
- `index_` shrinks by one each time a maker fully fills, and `rest()` is never
  called for order 5 because `taker.qty` reaches 0.

Quick reference: `n` step over, `s` step into, `finish` run to return,
`bt` backtrace, `up`/`down` change frame, `frame variable` locals,
`p expr` evaluate, `watchpoint set variable X`, `c` continue, `q` quit.
