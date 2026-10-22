# TODO make list

## Architectural Overview
    * **Client (`./client`)** — A lightweight console dashboard that allows users to submit tasks, monitor active execution states, fetch logs, and cancel jobs.
    * **Server (`./server`)** — The central dispatch and orchestration center responsible for managing concurrent client connections, queuing tasks, implementing scheduling policies, and coordinating workers.
    * **Worker (`./worker`)** — An isolated execution node that connects to the server, receives binary commands, runs them safely in subprocesses, intercepts their output streams, and reports execution statuses.

- [ ] Finish writing `server.c`
- [x] Finish writing `worker.c`

---

## 🗂️ Detailed Technical Specifications & Implementation Guide

### 🎛️ 1. Client (`./client`) — Control Panel
#### Purpose
Enables users to issue commands to the server and manage computational tasks directly from the console interface.
#### Core Functionality & Commands to Implement
- **`submit "<cmd>" <timeout>`** — Transmits a raw binary execution command to the server along with maximum execution time constraints, returning a unique tracking identifier (e.g., `T4-1`).
- **`status <id>`** — Queries the central server for the state of a targeted task identifier, displaying `QUEUED`, `RUNNING`, `DONE`, or `CANCELLED`.
- **`fetch <id>`** — Polls the server to retrieve and print out the complete standard output history (`stdout` and `stderr`) of a finished job.
- **`cancel <id>`** — Instructs the server to forcefully abort a pending or active running job.

---

### 🖥️ 2. Server (`./server`) — Dispatch Center
#### Purpose
Acts as the main orchestrator to handle concurrent input streams, balance incoming task schedules, and manage state data tables safely.
#### Implementation Requirements & Logic
- **High Concurrency TCP Engine**
  * Use non-blocking sockets, `select()`, `poll()`, or multi-threading to handle **at least 8 client connections simultaneously** without lag or blocking.
- **Task Queue & State Table Management**
  * Maintain an in-memory queue (`queue`) where incoming submissions are saved with an initial status of `QUEUED`.
  * Track active lifecycles inside a structured global task state table.
- **Scheduling Algorithmic Policy**
  * Write a scheduling policy subsystem (`policy`) that monitors when a remote execution node frees up.
  * Dynamically extract the next operational item using FIFO (First-In, First-Out) or priority metrics, then dispatch it over the network to the available worker.
- **Aggregator Node Interface**
  * Listen for inbound reports from execution workers to transition records into terminal statuses (`DONE` or `KILLED`).

---

### ⚙️ 3. Worker (`./worker`) — Execution Node
#### Purpose
Isolated execution instances that safely run arbitrary commands, handle timeouts, and manage subprocess pipelines.
#### Implementation Requirements & Logic
- **Process & Output Control**
  * Use `fork()` and `exec()` to run shell commands in an isolated environment.
  * Establish a `pipe()` and use `dup2()` to capture both `stdout` and `stderr`.
- **Resource Constraints & Cleanup**
  * Enforce maximum execution times using `poll()` or signals; terminate runaway tasks with `SIGKILL`.
  * Prevent zombie processes by strictly reaping terminated children using `waitpid()`.

---

### 🛠️ 4. Build, Automation, and Testing
#### Purpose
Maintains continuous code stability, uniform builds, and standard architectural documentation for the workspace.
#### Deliverables & Tasks
- **Structured Compilation Layout** — Write a comprehensive `Makefile` detailing target recipes for client, server, and worker binaries.
- **Automated Quality Checks** — Integrate explicit autotests validating boundary conditions, concurrency limits, and timeout crashes.
- **Technical Summary Record** — Prepare a detailed 4-page project brief exported to PDF, mapping design benchmarks and runtime results.