/*
 * ipc.c
 * Named-pipe (FIFO) helpers used by the chat server.
 *
 * The server side opens the FIFO for writing and pushes log
 * messages into it.  A separate logger process (logger_process.c)
 * opens the same FIFO for reading and persists every message to
 * the log file — demonstrating true IPC between two processes.
 *
 * FIFO path  : /tmp/chat_log_pipe
 * Direction  : server  ──write──▶  FIFO  ──read──▶  logger process
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include "../include/ipc.h"

/* File descriptor kept open for the lifetime of the server process. */
static int pipe_fd = -1;

/* ------------------------------------------------------------------ */
/* ipc_init                                                             */
/* Create the FIFO if it does not exist, then open it for writing.     */
/* Returns 0 on success, -1 on failure.                                */
/* ------------------------------------------------------------------ */
int ipc_init(void)
{
    /* Create the FIFO — ignore EEXIST (already there from a previous
       run) but treat every other error as fatal.                      */
    if (mkfifo(PIPE_PATH, 0666) < 0)
    {
        perror("mkfifo");
        /* If it already exists that is fine — carry on. */
    }

    /*
     * Open with O_RDWR so that the open() call returns immediately
     * even when the reader (logger process) has not connected yet.
     * A pure O_WRONLY would block until a reader appears.
     */
    pipe_fd = open(PIPE_PATH, O_RDWR | O_NONBLOCK);
    if (pipe_fd < 0)
    {
        perror("ipc_init: open FIFO");
        return -1;
    }

    return 0;
}

/* ------------------------------------------------------------------ */
/* ipc_send_log                                                         */
/* Write a single log message into the FIFO.                           */
/* The logger process on the other end reads and persists it.          */
/* ------------------------------------------------------------------ */
void ipc_send_log(const char *message)
{
    if (pipe_fd < 0)
        return;                         /* IPC not initialised        */

    if (!message || message[0] == '\0')
        return;

    /* Ensure the message ends with a newline so the reader can use
       a simple line-oriented loop.                                    */
    char buf[2048];
    int n = snprintf(buf, sizeof(buf),
                     "%s%s",
                     message,
                     (message[strlen(message) - 1] == '\n') ? "" : "\n");

    if (write(pipe_fd, buf, n) < 0)
        perror("ipc_send_log: write");
}

/* ------------------------------------------------------------------ */
/* ipc_close                                                            */
/* Release the FIFO file descriptor.  Called during server shutdown.   */
/* ------------------------------------------------------------------ */
void ipc_close(void)
{
    if (pipe_fd >= 0)
    {
        close(pipe_fd);
        pipe_fd = -1;
    }
}