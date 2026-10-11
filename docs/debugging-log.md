# Debugging log

One entry per notable bug or debugger session. Keep it short; these double as
interview talking points.

Template:

```
## YYYY-MM-DD: <one-line title>
**Phase / checkpoint:**
**Symptom:** what I saw (test name, output, crash)
**How I found it:** LLDB commands used, what `frame variable` / `watchpoint` / ASan showed
**Root cause:**
**Fix:**
**Lesson:** the general rule I'd apply next time
```

---

## Phase 1 checkpoint 1: step through a multi-level sweep
Walkthrough: `docs/lldb-checkpoint-1.md`. _(pending)_

## Phase 1 checkpoint 2: planted iterator bug, found with LLDB + ASan
Walkthrough: `docs/lldb-checkpoint-2.md`. Bug lives on branch `checkpoint2-bug`. _(pending)_

## Phase 2 checkpoint 1: two threads side by side
Walkthrough: `docs/lldb-checkpoint-3.md`. _(pending)_

## Phase 2 checkpoint 2: catch the race with the lock removed, confirm with TSan
Walkthrough: `docs/lldb-checkpoint-4.md`. _(pending)_
