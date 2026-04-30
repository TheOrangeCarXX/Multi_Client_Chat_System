/*
 * logger.c
 * Public logging API for the chat server.
 *
 * log_message() no longer writes directly to disk.  Instead it
 * forwards every message through the named pipe (FIFO) via
 * ipc_send_log().  The separate logger_process reads from the other
 * end of the pipe and persists the messages to data/chatlog.txt —
 * demonstrating Inter-Process Communication (IPC) between the server
 * process and the logger process.
 *
 * File locking and timestamping are now handled by logger_process.c
 * so that all disk I/O for logging is isolated in one place.
 */

#include <stdio.h>
#include <string.h>
#include "../include/logger.h"
#include "../include/ipc.h"

/* ------------------------------------------------------------------ */
/* log_message                                                          */
/* Send a log message to the logger process via the named pipe.        */
/* ------------------------------------------------------------------ */
void log_message(char message[])
{
    if (!message || message[0] == '\0')
        return;

    /*
     * ipc_send_log() writes the message into the FIFO.
     * The logger process on the read end will prepend a timestamp
     * and flush it to data/chatlog.txt with file locking.
     */
    ipc_send_log(message);
}