# Multi-Client Chat System

A multi-client TCP chat server written in C, built on POSIX sockets, `pthreads`, and inter-process communication (IPC). Multiple clients can log in, join chat rooms, send direct messages, and interact with room history — while a separate forked **logger process** persists every server event to disk over a named pipe (FIFO).

## Features

- **Multi-client TCP server** — one thread per connected client (`pthread_create` + `pthread_detach`), synchronized with a mutex over shared client state.
- **Authentication & roles** — users log in against `data/users.txt` (`username,password,role`), with `admin`, `user`, and `guest` roles. Duplicate logins are rejected (`ALREADY_LOGGED_IN`).
- **Chat rooms** — clients list rooms (`GET_ROOMS`), join one (`ROOM <id>`), and receive that room's persisted history on join. Each room logs its own chat file (`data/chatlog_room<N>.txt`).
- **Admin commands**
  - `/ADDROOM <id> <name>` — create a new room on the fly.
  - `/ban <username>` — ban a user, kicking them immediately if online.
- **User commands**
  - `/who` — list users currently in your room.
  - `/dm <user> <message>` — send a private direct message.
- **Ban list** — banned usernames are stored in `data/banned.txt`; banned users are rejected at login or disconnected on the spot.
- **IPC-based logging (FIFO)** — the server doesn't write logs itself. On startup it creates a named pipe (`/tmp/chat_log_pipe`), forks a dedicated `logger_process`, and streams every event (logins, logouts, room joins, bans) to it. The logger process reads from the pipe and appends timestamped entries to `data/chatlog.txt`, using `fcntl` file locking for safe writes. This is a deliberate demonstration of process separation and IPC, not just logging via a function call.
- **Timestamped messages** — every chat message is stamped with `[HH:MM]` before being broadcast and logged.

## Project Structure

```
.
├── Makefile
├── include/
│   ├── auth.h          # login_user()
│   ├── config.h        # PORT, MAX_CLIENTS, BUFFER_SIZE
│   ├── ipc.h            # FIFO path + ipc_init/send/close API
│   ├── logger.h        # log_message()
│   └── utils.h         # trim_newline()
├── src/
│   ├── server.c         # main server: accept loop, rooms, commands, broadcasting
│   ├── client.c         # interactive client with login + menu
│   ├── auth.c            # credential lookup against data/users.txt
│   ├── ipc.c              # FIFO write-end helpers (server side)
│   ├── logger.c          # thin wrapper that forwards messages into the FIFO
│   └── logger_process.c # standalone process: reads FIFO, writes data/chatlog.txt
└── data/
    ├── users.txt              # username,password,role
    ├── rooms.txt               # room id + name, used by the server
    ├── rooms_config.txt        # room name list
    ├── banned.txt              # banned usernames
    └── chatlog_room0.. txt   # per-room persisted history
```

## Building

Requires `gcc` and a POSIX environment (Linux/WSL/macOS).

```bash
make
```

This builds three binaries into `build/`:

| Binary                | Purpose                                      |
|-----------------------|-----------------------------------------------|
| `build/server`        | The chat server (also forks the logger)      |
| `build/client`        | The interactive chat client                  |
| `build/logger_process`| Log writer — **do not run manually**, the server forks it automatically |

Clean up build artifacts with:

```bash
make clean
```

## Running

The server expects to run from the project root, since it reads/writes relative paths like `data/users.txt` and `data/chatlog_room0.txt`.

**1. Start the server:**

```bash
./build/server
```

You should see:

```
[server] Logger process started (PID <pid>).
Server running on port 8080...
```

**2. Start one or more clients** (in separate terminals):

```bash
./build/client
```

By default the client connects to `127.0.0.1`. To connect to a remote server, set `SERVER_IP` before launching:

```bash
SERVER_IP=192.168.1.10 ./build/client
```

**3. Log in** with credentials from `data/users.txt`, e.g.:

| Username | Password | Role   |
|----------|----------|--------|
| admin    | 1234     | admin  |
| john     | pass     | user   |
| guest    | 0000     | guest  |

**4. Use the menu:**

```
1. Enter Chat
2. Add Room (Admin Only)
3. Ban User (Admin Only)
4. Logout
```

Inside a chat room, type normally to broadcast a message, or use:

```
/who              list users in the current room
/dm <user> <msg>  send a private message
exit              leave the room back to the menu
```

## Configuration

Server-side constants live in `include/config.h`:

```c
#define PORT 8080
#define MAX_CLIENTS 100
#define BUFFER_SIZE 1024
```

The FIFO path used for IPC logging is defined in `include/ipc.h`:

```c
#define PIPE_PATH "/tmp/chat_log_pipe"
```

## Notes & Limitations

- Credentials in `data/users.txt` are stored in **plain text** — this is a learning/demo project, not production-ready auth. Don't reuse real passwords.
- `data/chatlog.txt` (the IPC-logged event log) and the per-room chat logs are written as plain append-only text files.
- The server currently supports a fixed room set defined in `data/rooms.txt`, extendable at runtime via `/ADDROOM`.

## Why the FIFO / logger process?

Most simple socket chat servers log events directly from within the request-handling code. This project instead **forks a dedicated logger process** at server startup and routes all logging through a named pipe. This is intentional: it isolates disk I/O for logging into its own process, and demonstrates classic UNIX IPC (`mkfifo`, `fork`, `execl`, blocking/non-blocking FIFO opens, `fcntl` locking) alongside the socket and threading work in the rest of the project.
