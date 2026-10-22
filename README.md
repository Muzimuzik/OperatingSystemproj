# Project: Distributed Job Runner (Operating Systems)

## 👥 Team Details
* **Group:** Group 4
* **Team Leader:** Ligay Stanislav (Лигай Станислав)

> 🚀 **Current tasks and work in current file [`TODO.md`](./TODO.md)**

---

## 🛠 Project Components & Ownership

| Component | Description | Owner |
| :--- | :--- | :--- |
| **`Scheduler and Policy`** | Server-side task queue management, scheduling policy implementation (SJF / Priority / FIFO), and job dispatching to workers. | **Ligay Stanislav** |
| **`Worker and Limits`** | Worker logic: `fork()`, `exec()`, output redirection via `pipe` and `dup2()`, timeout handling, and zombie process prevention. | **Li Andrey** |
| **`Durability and Recovery`** | Task state table (`QUEUED`, `RUNNING`, `DONE`, `CANCELLED`), `cancel` command processing, and crash resilience. | **Tsoy Lev** |
| **`Protocol and Client`** | Network protocol specification (`protocol.h`) and client utility implementation (`submit`, `status`, `cancel`, `fetch`). | **Abdurakhmonov Usman** |
| **`Operations, Testing & Report`** | Project build automation (`Makefile`), Git repository structure, automated test scripts (§5.4), and the final 4-page PDF report. | **Abdurakhmonov Usman** |

---

## 📁 Directory Structure

* `src/` — Main source directory containing C source files (`server.c`, `worker.c`, `client.c`, `protocol.h`, `net.h`).
* `tests/` — Automated validation, integration, and load testing scripts.
* `Makefile` — Build automation script located at the repository root.
* `README.md` — Project documentation, team roles, and build/run instructions.

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

> ⚠️ **Important Note:** Do **not** create any directories named `server`, `worker`, or `client` inside the root directory. Doing so will cause targets to conflict within the `Makefile`, leading to `gcc` compilation errors.

