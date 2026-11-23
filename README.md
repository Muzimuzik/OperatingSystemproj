# 🚀 Project: Distributed Job Runner (Operating Systems)

## 👥 Team Details
* **Group:** Group 4
* **Team Leader:** Ligay Stanislav
* **Task ID Prefix:** `T04-` (e.g., `T04-1`, `T04-2`)
* 📌 Current tasks and progress are tracked in [`TODO.md`](TODO.md)

---

## 🛠️ Project Components & Ownership

Each team member owns specific source files and is responsible for their implementation, error handling, and live code defense:

| Component | Owned Files | Description & Responsibilities | Owner |
| :--- | :--- | :--- | :--- |
| **Scheduler and Policy** | `src/server.c`<br>`src/scheduler.c`<br>`src/scheduler.h` | Core TCP server (`poll()`), multi-client handling (8+ clients), scheduling policy (**SJF** — Shortest Job First based on timeout), job dispatching to workers, and graceful `SIGTERM` shutdown. | **Ligay Stanislav** |
| **Worker and Limits** | `src/worker.c` | Worker node logic: `fork()`, `/bin/sh -c` execution, process groups (`setpgid`), output capture via `pipe()` and `dup2()`, timeout enforcement (`SIGKILL`), slot limit (\(K\)), and zombie prevention. | **Li Andrey** |
| **Durability and Recovery** | `src/journal_store.c`<br>`src/journal_store.h`<br>`src/journal.h` | Task state machine (`QUEUED`, `RUNNING`, `DONE`, `FAILED`, `CANCELLED`, `TIMED_OUT`), `cancel` processing, write-ahead journaling (`jobs.log` with `fsync`), output persistence (`out/`), and crash recovery after `SIGKILL`. | **Tsoy Lev** |
| **Protocol and Client** | `src/client.c`<br>`src/protocol.h`<br>`src/net.h` | Text-based TCP wire protocol specification, safe socket I/O helpers, and CLI client (`submit`, `status`, `cancel`, `fetch`). | **Abdurakhmonov Usman** |
| **Operations, Testing & Report** | `Makefile`<br>`tests/`<br>`report.pdf` | Build automation (`make`), Git repository structure, automated test scripts (§5.4 & crash recovery tests), and the final 4-page PDF report. | **Abdurakhmonov Usman** *(Reserved for 5th member if joined)* |

---

## 📁 Directory Structure

```text
.
├── src/
│   ├── server.c          # Main server loop and network multiplexing
│   ├── scheduler.c       # SJF scheduling policy and worker queue logic
│   ├── scheduler.h
│   ├── journal_store.c   # Crash recovery, state transitions, and out/ storage
│   ├── journal_store.h
│   ├── journal.h         # Provided append-only journal helper (unmodified)
│   ├── worker.c          # Remote job executor with process group & timeout control
│   ├── client.c          # Command-line interface for users
│   ├── protocol.h        # Shared TCP message formats and constants
│   └── net.h             # Socket helper functions (read_line, write_all)
├── tests/                # Automated validation, load, and crash-recovery scripts
├── out/                  # Runtime directory for persisted job stdout/stderr
├── Makefile              # Build automation script located at the repository root
└── README.md             # Project documentation, team roles, and run instructions
```

---

## 🚀 Building and Running

### Compilation
To compile all modules and build the system, run the following command in the repository root directory:

```bash
make
```

Upon successful compilation, three distinct binary executables will be generated in the root folder:
* `./server`
* `./worker`
* `./client`

> ⚠️ **Important Note:** Do not create any directories named `server`, `worker`, or `client` inside the root directory. Doing so will cause targets to conflict within the `Makefile`, leading to `gcc` compilation errors.

### Running the System (Two VMs / Hosts Setup)

1. **Start the Server (VM 1):**
   ```bash
   ./server --port 9000
   ```
2. **Start the Worker (VM 2 — using VM 1's LAN IP from `ip a`):**
   ```bash
   ./worker --host <SERVER_IP> --port 9000 --slots 2
   ```
3. **Submit and Manage Jobs via Client:**
   ```bash
   ./client --host <SERVER_IP> --port 9000 submit --timeout 10 "echo hello && sleep 2"
   ./client --host <SERVER_IP> --port 9000 status T04-1
   ./client --host <SERVER_IP> --port 9000 fetch T04-1
   ./client --host <SERVER_IP> --port 9000 cancel T04-1
   ```
