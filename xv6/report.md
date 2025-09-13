# xv6 Scheduler Implementation Report

## Part A – System Call: `getreadcount`

### A.1: System Call Design and Implementation

A new system call, `getreadcount()`, was implemented to track and return the cumulative number of bytes read by the `read()` system call across **all processes** since system boot.

**Implementation Highlights:**

* Introduced a global kernel variable `uint64 read_byte_count` to store the total bytes read.
* Modified the existing `sys_read()` implementation to increment this counter by the number of bytes actually read on each successful read operation.
* Implemented overflow handling implicitly by allowing natural wrap-around using unsigned integer arithmetic.
* Implemented the `sys_getreadcount()` system call to return the current value of this counter to user programs.
* Added the appropriate system call number, prototypes, and declarations in the system call tables and header files.

This design ensures accurate system-wide tracking of data read operations without impacting the correctness or performance of the `read()` syscall.

### A.2: User-Level Verification Program

A user-level program `readcount.c` was developed to validate the functionality of `getreadcount()`.

**Program Workflow:**

1. Calls `getreadcount()` and prints the initial counter value.
2. Opens and reads 100 bytes from a file (e.g., `README`).
3. Calls `getreadcount()` again and prints the updated value.
4. Confirms that the counter increased by the number of bytes read.

**Outcome:**
The observed increase in the counter value verifies the correct behavior of the `getreadcount()` system call.

---

## Part B – Scheduler Implementations

### B.1: Overview

The default **Round Robin** scheduler in xv6 was extended to support two additional scheduling policies:

* **First Come First Serve (FCFS)**
* **Completely Fair Scheduler (CFS)**

The active scheduler is selected at compile time using flags:

* `make qemu SCHEDULER=FCFS` — for FCFS
* `make qemu SCHEDULER=CFS` — for CFS
* No flag — defaults to Round Robin

For evaluation and correctness verification, the kernel was configured to run on **a single CPU**. Running on multiple CPUs can produce interleaved and inconsistent terminal output due to concurrent scheduling decisions.

---

### B.2: First Come First Serve (FCFS) Scheduler

**Characteristics:**

* Non-preemptive scheduling policy.
* Processes are scheduled strictly based on their creation time.
* A process runs until it terminates or blocks (no time slicing).

**Implementation Details:**

* Added `creation_time` to `struct proc` and initialized it in `allocproc()`.
* Modified the `scheduler()` loop to scan all `RUNNABLE` processes and select the one with the earliest `creation_time`.
* Implemented the logic under a preprocessor flag (`SCHED_FCFS`) for compile-time selection.
* Added basic debug logs to track scheduling decisions.

This implementation effectively mimics a batch processing scheduler but is prone to the convoy effect, where long-running jobs block short ones.

---

### B.3: Completely Fair Scheduler (CFS)

#### B.3.1: Priority and Weight Mechanism

* Introduced a `nice` field in `struct proc`, ranging from **-20 (highest priority)** to **+19 (lowest priority)**. Default: 0.
* Implemented a `calculate_weight_from_priority()` helper that maps `nice` values to weights using an integer approximation of the Linux weight model:

  * Base weight at nice = 0: 1024
  * Positive nice → lower weight (less CPU share)
  * Negative nice → higher weight (more CPU share)

#### B.3.2: Virtual Runtime (vRuntime) Tracking

* Added `virtual_runtime` to each process to track its CPU usage in a weighted manner.
* On every timer tick, a process’s `virtual_runtime` is incremented by:

  delta = (1024 * PRECISION) / weight


#### B.3.3: Scheduling Logic

* At each scheduling decision, the scheduler scans all `RUNNABLE` processes and selects the one with the **lowest `virtual_runtime`**.
* This ensures that processes that have run less recently are favored, achieving fairness.
* All locking is handled carefully to avoid race conditions during selection.

#### B.3.4: Time Slice Allocation

* Implemented **dynamic time slicing** based on system load:

  * Target latency: 48 ticks
  * `slice = target_latency / runnable_count`
  * Minimum enforced slice: 3 ticks
* Once a process consumes its allocated slice, it yields to allow other processes to run.

**Preemption** occurs naturally as the time slice expires, ensuring fair CPU distribution across processes.

---

### B.4: Debug Logging

The CFS scheduler includes detailed debug logs to trace scheduling behavior. Example:

[Scheduler Tick]
PID: 7  | vRuntime: 0
PID: 8  | vRuntime: 0
PID: 9  | vRuntime: 9000
PID: 10 | vRuntime: 9000
PID: 11 | vRuntime: 0
PID: 12 | vRuntime: 0
PID: 13 | vRuntime: 0
--> Scheduling PID 7 (lowest vRuntime)

[Scheduler Tick]
PID: 8  | vRuntime: 0
PID: 9  | vRuntime: 9000
PID: 10 | vRuntime: 9000
PID: 11 | vRuntime: 0
PID: 12 | vRuntime: 0
PID: 13 | vRuntime: 0
--> Scheduling PID 8 (lowest vRuntime)

[Scheduler Tick]
PID: 9  | vRuntime: 9000
PID: 10 | vRuntime: 9000
PID: 11 | vRuntime: 0
PID: 12 | vRuntime: 0


This log shows:

* All runnable processes with their current `vRuntime`
* The chosen process with the lowest `vRuntime`

This visibility was crucial for validating the correctness of the CFS scheduling logic.

---

## Part C – Scheduler Performance Evaluation

**Test Setup:**

* System configured to use a single CPU.
* Used a custom `schedulertest` utility to measure average waiting time, running time, fairness, and throughput.

**Results:**

| Scheduler   | Avg Waiting Time | Avg Running Time       | Fairness             | Throughput |
| ----------- | ---------------- | ---------------------- | -------------------- | ---------- |
| Round Robin | Moderate         | Balanced               | Good                 | Good       |
| FCFS        | High variance    | Depends on arrival     | Poor (convoy effect) | Lower      |
| CFS         | Low variance     | Proportional to weight | Excellent            | High       |

**Analysis:**

* **Round Robin:** Simple and fair, good for interactive workloads.
* **FCFS:** Suffers from poor responsiveness when long tasks block short ones.
* **CFS:** Achieves high fairness and responsiveness; adapts to workload dynamically.

---

## Part D – Challenges and Solutions

| Challenge                  | Description                                    | Solution                                                     |
| -------------------------- | ---------------------------------------------- | ------------------------------------------------------------ |
| Integer weight calculation | Floating-point is unavailable in kernel        | Used iterative integer multiplication with 4/5 or 5/4 ratios |
| vRuntime precision         | Integer math caused loss of granularity        | Introduced scaling factor (1000) to preserve precision       |
| Locking during scheduling  | Risk of deadlocks or races                     | Applied careful lock acquire/release patterns                |
| Preemption tuning          | Balancing fairness vs. context-switch overhead | Implemented dynamic time slices with a minimum threshold     |

---

## Conclusion

This project successfully extended xv6 with:

* A **new system call (`getreadcount`)** for tracking system-wide bytes read
* A **First Come First Serve (FCFS)** non-preemptive scheduler
* A **Completely Fair Scheduler (CFS)** with support for `nice` priorities, weight-based scheduling, dynamic time slicing, and vruntime tracking

Among the three, **CFS demonstrated superior fairness, responsiveness, and adaptability** to varying workloads.
The inclusion of detailed scheduler logs enabled effective testing and verification of correctness.

All implementations followed proper kernel programming practices, including safe locking, careful resource management, and compile-time configurability. Running on a single CPU ensured consistent, non-interleaved output during evaluation.
