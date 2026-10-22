#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <time.h>
#include <poll.h>
#include <signal.h>
#include "net.h"

#define MAX_BUF 4096

/*
 * Executes the command in a child process, captures stdout/stderr via pipe,
 * enforces the execution timeout, and prevents zombie processes.
 */
void process_job(int server_fd, const char *cmd, int timeout_sec) {
    int pipe_fd[2];
    if (pipe(pipe_fd) == -1) {
        perror("pipe");
        return;
    }

    pid_t pid = fork();
    if (pid < 0) {
        perror("fork");
        return;
    }

    if (pid == 0) {
        // Child process: redirect outputs and execute command
        close(pipe_fd[0]);
        dup2(pipe_fd[1], STDOUT_FILENO);
        dup2(pipe_fd[1], STDERR_FILENO);
        close(pipe_fd[1]);

        execl("/bin/sh", "sh", "-c", cmd, (char *)NULL);
        perror("execl");
        exit(EXIT_FAILURE);
    }

    // Parent process (Worker node): read output and manage lifecycle
    close(pipe_fd[1]);
    char output[MAX_BUF];
    int out_len = 0;

    struct pollfd pfd;
    pfd.fd = pipe_fd[0];
    pfd.events = POLLIN;

    time_t start_time = time(NULL);
    int status;
    int killed = 0;

    while (1) {
        int time_left = timeout_sec - (time(NULL) - start_time);
        if (time_left <= 0) {
            kill(pid, SIGKILL);
            killed = 1;
            break;
        }

        // Wait for data or timeout
        int ret = poll(&pfd, 1, time_left * 1000);
        
        if (ret > 0 && (pfd.revents & POLLIN)) {
            int bytes = read(pipe_fd[0], output + out_len, sizeof(output) - out_len - 1);
            if (bytes <= 0) break; // EOF reached
            out_len += bytes;
            if (out_len >= sizeof(output) - 1) break; // Buffer full
        } else if (ret == 0) {
            // Timeout reached during poll
            kill(pid, SIGKILL);
            killed = 1;
            break;
        } else {
            break; // Poll error
        }
    }

    output[out_len] = '\0';
    close(pipe_fd[0]);

    // Zombie process prevention: wait for the child to fully terminate
    waitpid(pid, &status, 0);

    // Format and send the result back to the server
    char response[MAX_BUF + 256];
    if (killed) {
        snprintf(response, sizeof(response), "KILLED\n%s\nEOF\n", output);
    } else {
        snprintf(response, sizeof(response), "DONE %d\n%s\nEOF\n", WEXITSTATUS(status), output);
    }
    
    write(server_fd, response, strlen(response));
}

int main(int argc, char **argv) {
    const char *host = "127.0.0.1";
    int port = 5000;

    int fd = client_connect(host, port);
    printf("[Worker] Connected to server at %s:%d\n", host, port);

    char buf[MAX_BUF];
    while (1) {
        // Wait for a job dispatch from the server
        // Expected basic protocol format: "<timeout_in_seconds> <command>"
        if (read_line(fd, buf, sizeof(buf)) <= 0) {
            printf("[Worker] Server disconnected.\n");
            break;
        }

        int timeout;
        char cmd[MAX_BUF];
        
        // Parse the incoming task
        if (sscanf(buf, "%d %[^\n]", &timeout, cmd) == 2) {
            printf("[Worker] Executing: '%s' (Timeout: %ds)\n", cmd, timeout);
            process_job(fd, cmd, timeout);
        } else {
            char *err = "ERROR\nInvalid command format. Expected: <timeout> <cmd>\nEOF\n";
            write(fd, err, strlen(err));
        }
    }

    close(fd);
    return 0;
}
