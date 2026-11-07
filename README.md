# Shell, xv6 Schedulers and Reliable UDP

Three systems programs written in C for the Operating Systems and Networks course
at IIIT Hyderabad (Monsoon 2025).

| Folder | What it is |
|---|---|
| `shell/` | A Unix-style shell built on POSIX system calls |
| `networking/` | A TCP-like reliable transport built on top of UDP |
| `xv6/` | The xv6 RISC-V kernel with a new system call and new schedulers |

## Shell

Handles pipes, input and output redirection, sequential and background commands,
and job control.

Built-in commands:

- `hop` changes directory, and `reveal` lists files
- `log` keeps a command history
- `activities` lists the processes started by the shell
- `ping <pid> <signal>` sends a signal to a process
- `fg` and `bg` move jobs between foreground and background

Ctrl-C and Ctrl-Z go to the foreground process group, not the shell itself.

```
cd shell
make all
./shell.out
```

## Reliable UDP

UDP does not guarantee delivery or ordering, so this adds both in user space. Each
packet carries a small header (`networking/sham.h`) with a sequence number, an
acknowledgement number, SYN/ACK/FIN flags and a window size.

- Three-way handshake to open a connection and FIN exchange to close it
- Sliding window with flow control from the receiver's advertised window
- Retransmission after a 500 ms timeout
- File transfer, checked with an MD5 hash on the receiving side
- A two-way chat mode
- An optional loss rate, to test recovery from dropped packets

```
cd networking
make all
./server.out <port> [--chat] [loss_rate]
./client.out <server_ip> <port> <input_file> <output_file> [loss_rate]
./client.out <server_ip> <port> --chat [loss_rate]
```

Building needs OpenSSL (`libssl-dev` on Ubuntu).

## xv6

- `getreadcount()`: a system call that returns the total bytes read since boot.
  `readcount.c` is the user program that tests it.
- Schedulers, chosen at compile time: Round Robin (default), FCFS, CFS and MLFQ.
- CFS uses nice values, weights, virtual runtime and dynamic time slices.

The full modified kernel is in `xv6/xv6-riscv_Modified.zip`, and
`xv6/xv6_modifications.patch` is the same work as a diff. Design notes are in
`xv6/report.md`.

```
unzip xv6/xv6-riscv_Modified.zip
cd xv6-riscv_Modified
make clean
make qemu SCHEDULER=CFS CPUS=1
```

`SCHEDULER` can be `RR`, `FCFS`, `CFS` or `MLFQ`. Always run `make clean` when
changing it. Use one CPU, because the scheduler logs and timing comparisons assume it.
