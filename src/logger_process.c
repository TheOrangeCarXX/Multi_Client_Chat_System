/*
 * logger_process.c
 * Stand-alone logger process for the chat server.
 *
 * This process is forked by server.c at startup.  It sits on the
 * read end of the named pipe (FIFO) and writes every incoming log
 * line — with a timestamp — to  data/chatlog.txt.
 *
 * File locking (fcntl F_WRLCK) is retained so that if any other
 * process ever writes to the same log file the write remains safe.
 *
 * Compile alongside the server (see Makefile).
 * Do NOT run this binary manually; server.c forks it automatically.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <time.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/types.h>
#include "../include/ipc.h"

/* ------------------------------------------------------------------ */
/* write_log_entry                                                      */
/* Append a timestamped line to data/chatlog.txt with fcntl locking.  */
/* ------------------------------------------------------------------ */
static void write_log_entry(const char *message)
{
    int fd = open("data/chatlog.txt",
                  O_WRONLY | O_CREAT | O_APPEND,
                  0644);
    if (fd < 0)
    {
        perror("logger_process: open chatlog");
        return;
    }

    /* Acquire an exclusive write lock before touching the file. */
    struct flock lock;
    lock.l_type   = F_WRLCK;
    lock.l_whence = SEEK_SET;
    lock.l_start  = 0;
    lock.l_len    = 0;          /* lock entire file */
    fcntl(fd, F_SETLKW, &lock); /* block until lock is granted */

    /* Build the timestamped entry. */
    time_t now     = time(NULL);
    char  *ts      = ctime(&now);
    char   entry[2048];
    snprintf(entry, sizeof(entry),
             "[%.*s] %s",
             (int)(strlen(ts) - 1),   /* strip trailing '\n' from ctime */
             ts,
             message);

    write(fd, entry, strlen(entry));

    /* Release the lock. */
    lock.l_type = F_UNLCK;
    fcntl(fd, F_SETLK, &lock);

    close(fd);
}

/* ------------------------------------------------------------------ */
/* main                                                                 */
/* Open the FIFO for reading and loop forever, writing every line      */
/* received from the server into the log file.                         */
/* ------------------------------------------------------------------ */
int main(void)
{
    /* Ignore SIGPIPE — harmless if the server end closes first. */
    signal(SIGPIPE, SIG_IGN);

    printf("[logger_process] Starting. Reading from %s\n", PIPE_PATH);
    fflush(stdout);

    /*
     * Open in blocking read mode.
     * This call blocks until the server (writer) opens its end,
     * which is fine because server.c calls ipc_init() right after
     * forking us.
     */
    int pipe_fd = open(PIPE_PATH, O_RDONLY);
    if (pipe_fd < 0)
    {
        perror("logger_process: open FIFO");
        exit(EXIT_FAILURE);
    }

    printf("[logger_process] Connected to FIFO. Waiting for log messages...\n");
    fflush(stdout);

    char   buf[2048];
    char   line[2048];
    int    line_pos = 0;
    ssize_t n;

    /*
     * Read raw bytes from the pipe and reassemble complete lines
     * (terminated by '\n') before writing each one to the log file.
     * This handles the case where a single write() on the server side
     * is split across multiple read() calls here.
     */
    while ((n = read(pipe_fd, buf, sizeof(buf) - 1)) > 0)
    {
        buf[n] = '\0';

        for (int i = 0; i < n; i++)
        {
            if (buf[i] == '\n')
            {
                line[line_pos] = '\n';
                line[line_pos + 1] = '\0';
                write_log_entry(line);
                line_pos = 0;
            }
            else
            {
                if (line_pos < (int)sizeof(line) - 2)
                    line[line_pos++] = buf[i];
            }
        }
    }

    /* Server closed the write end — flush any partial line. */
    if (line_pos > 0)
    {
        line[line_pos++] = '\n';
        line[line_pos]   = '\0';
        write_log_entry(line);
    }

    close(pipe_fd);
    printf("[logger_process] FIFO closed. Exiting.\n");
    return 0;
}