# TODO make list
- [ ] Finish writing `server.c`
- [ ] Finish writing `worker.c`

---

## For Worker
* Make sure that worker connect to the server via TCP
* **Make execute with `fork()` and `exec()`**
    * Worker make child proccess from `fork()`
    * In child procces should gather task to other group procces(hint: `setpgid(0, 0)`)
* Output interception

