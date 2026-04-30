#ifndef IPC_H
#define IPC_H

/*
 * ipc.h
 * Interface for the named-pipe (FIFO) IPC layer.
 *
 * PIPE_PATH is the filesystem path of the FIFO that connects the
 * server process (writer) to the logger process (reader).
 */

#define PIPE_PATH "/tmp/chat_log_pipe"

/*
 * ipc_init()
 *   Create the FIFO if it does not already exist and open the write
 *   end.  Must be called once in the server process before any
 *   ipc_send_log() calls.
 *   Returns 0 on success, -1 on failure.
 */
int  ipc_init(void);

/*
 * ipc_send_log(message)
 *   Write a log message into the FIFO.  The logger process reads it
 *   from the other end and persists it to data/chatlog.txt.
 */
void ipc_send_log(const char *message);

/*
 * ipc_close()
 *   Close the write end of the FIFO.  Call during server shutdown.
 */
void ipc_close(void);

#endif /* IPC_H */