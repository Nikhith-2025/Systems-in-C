# xv6 notes

## getreadcount

`getreadcount()` returns the number of bytes read by `read()` across all processes
since boot. A global `uint64 read_byte_count` is updated in `sys_read()` by the number
of bytes actually read. If it overflows, it wraps around.

`readcount.c` tests it: print the counter, read 100 bytes from a file, then print the
counter again and check that it went up by 100.

## Schedulers

The scheduler is chosen at compile time, for example `make qemu SCHEDULER=CFS`.
With no flag, xv6 uses Round Robin. Testing was done on one CPU, since several CPUs
can run processes at once and make the order harder to check.

### FCFS

Non-preemptive. `struct proc` gets a `creation_time`, set in `allocproc()`, and
`scheduler()` picks the RUNNABLE process with the earliest one. It runs until it exits
or blocks. Long jobs hold up short ones behind them (the convoy effect).

### CFS

- Each process has a `nice` value from -20 (highest priority) to 19 (lowest), default 0.
- `calculate_weight_from_priority()` turns nice into a weight, with 1024 at nice 0.
  There is no floating point in the kernel, so it multiplies by 4/5 or 5/4 repeatedly.
- `virtual_runtime` grows on every tick by `(1024 * PRECISION) / weight`. A scaling
  factor of 1000 keeps precision with integer maths.
- The scheduler picks the RUNNABLE process with the lowest `virtual_runtime`.
- The time slice is `48 / runnable_count` ticks, with a minimum of 3 ticks.

Debug logs print every runnable process and the one chosen:

```
[Scheduler Tick]
PID: 7  | vRuntime: 0
PID: 8  | vRuntime: 0
PID: 9  | vRuntime: 9000
--> Scheduling PID 7 (lowest vRuntime)
```

## Comparison

Measured with `schedulertest` on one CPU. These are observations from those runs, not
exact figures.

| Scheduler | Waiting time | Fairness |
|---|---|---|
| Round Robin | Moderate | Good |
| FCFS | Varies a lot | Poor, because of the convoy effect |
| CFS | Low variance | Best of the three |

## Problems along the way

- Floating point is not available in the kernel, so weights use integer ratios.
- Integer division lost vRuntime precision, which is why the scaling factor exists.
- Picking the next process touches every process lock, so lock order needed care.
- Time slices that are too small waste time on context switches, so there is a minimum.
