#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <time.h>
#include "../include/auth.h"
#include "../include/logger.h"
#include "../include/ipc.h"
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>

#define PORT 8080
#define MAX_CLIENTS 100
#define BUFFER_SIZE 1024

#define MAX_USERS 100
char active_users[MAX_USERS][50];
int active_count = 0;

int client_sockets[MAX_CLIENTS];
int client_count = 0;

/* store username per client slot for DM and /who */
char client_usernames[MAX_CLIENTS][50];

#define MAX_ROOMS 5
int client_rooms[MAX_CLIENTS];

pthread_mutex_t lock;

/* ------------------------------------------------------------------ */
/* Timestamp helper — writes "[HH:MM] " into buf (must be >= 10 bytes) */
/* ------------------------------------------------------------------ */
void get_timestamp(char *buf, int buf_size)
{
    time_t now = time(NULL);
    struct tm *t = localtime(&now);
    snprintf(buf, buf_size, "[%02d:%02d] ", t->tm_hour, t->tm_min);
}

/* ------------------------------------------------------------------ */
/* Ban helpers                                                          */
/* ------------------------------------------------------------------ */
int is_banned(const char *username)
{
    FILE *fp = fopen("data/banned.txt", "r");
    if (!fp) return 0;

    char line[100];
    while (fgets(line, sizeof(line), fp))
    {
        line[strcspn(line, "\n")] = '\0';
        if (strcmp(line, username) == 0)
        {
            fclose(fp);
            return 1;
        }
    }
    fclose(fp);
    return 0;
}

void ban_user(const char *username)
{
    FILE *fp = fopen("data/banned.txt", "a");
    if (fp)
    {
        fprintf(fp, "%s\n", username);
        fclose(fp);
    }
}

/* ------------------------------------------------------------------ */
/* Broadcast message to everyone in the same room except sender        */
/* ------------------------------------------------------------------ */
void broadcast_message(char *message, int sender_socket, int room)
{
    pthread_mutex_lock(&lock);

    for (int i = 0; i < client_count; i++)
    {
        if (client_sockets[i] != sender_socket &&
            client_rooms[i] == room)
        {
            send(client_sockets[i], message, strlen(message), 0);
        }
    }

    pthread_mutex_unlock(&lock);
}

/* ------------------------------------------------------------------ */
/* Remove client from the global arrays                                */
/* ------------------------------------------------------------------ */
void remove_client(int socket)
{
    pthread_mutex_lock(&lock);

    for (int i = 0; i < client_count; i++)
    {
        if (client_sockets[i] == socket)
        {
            for (int j = i; j < client_count - 1; j++)
            {
                client_sockets[j] = client_sockets[j + 1];
                client_rooms[j]   = client_rooms[j + 1];
                strcpy(client_usernames[j], client_usernames[j + 1]);
            }
            client_count--;
            break;
        }
    }

    pthread_mutex_unlock(&lock);
}

/* ------------------------------------------------------------------ */
/* Active users list                                                    */
/* ------------------------------------------------------------------ */
int is_user_logged_in(char username[])
{
    for (int i = 0; i < active_count; i++)
    {
        if (strcmp(active_users[i], username) == 0)
            return 1;
    }
    return 0;
}

void add_user(char username[])
{
    strcpy(active_users[active_count++], username);
}

void remove_user(char username[])
{
    for (int i = 0; i < active_count; i++)
    {
        if (strcmp(active_users[i], username) == 0)
        {
            for (int j = i; j < active_count - 1; j++)
                strcpy(active_users[j], active_users[j + 1]);
            active_count--;
            break;
        }
    }
}

/* ------------------------------------------------------------------ */
/* Send chat history for a room                                         */
/* ------------------------------------------------------------------ */
void send_chat_history(int client_socket, int room)
{
    char filename[50];
    sprintf(filename, "data/chatlog_room%d.txt", room);

    FILE *fp = fopen(filename, "r");
    if (!fp) return;

    char line[1024];

    send(client_socket, "\n--- Chat History ---\n", 22, 0);

    while (fgets(line, sizeof(line), fp))
        send(client_socket, line, strlen(line), 0);

    send(client_socket, "--------------------\n", 21, 0);

    fclose(fp);
}

/* ------------------------------------------------------------------ */
/* Send room list to a client                                           */
/* ------------------------------------------------------------------ */
void send_rooms(int client_socket)
{
    FILE *fp = fopen("data/rooms.txt", "r");
    if (!fp)
    {
        perror("rooms.txt");
        return;
    }

    char line[200];
    char buffer[4096] = "ROOM_LIST\n";

    while (fgets(line, sizeof(line), fp))
        strcat(buffer, line);

    strcat(buffer, "END_ROOMS\n");
    send(client_socket, buffer, strlen(buffer), 0);
    fclose(fp);
}

/* ------------------------------------------------------------------ */
/* Validate room ID                                                     */
/* ------------------------------------------------------------------ */
int is_valid_room(int room_id)
{
    FILE *fp = fopen("data/rooms.txt", "r");
    if (!fp) return 0;

    int id;
    char name[100];

    while (fscanf(fp, "%d %[^\n]", &id, name) == 2)
    {
        if (id == room_id)
        {
            fclose(fp);
            return 1;
        }
    }

    fclose(fp);
    return 0;
}

/* ------------------------------------------------------------------ */
/* Send list of users currently in the same room (/who)                */
/* ------------------------------------------------------------------ */
void send_who(int client_socket, int room)
{
    char response[2048] = "WHO_LIST\nUsers in this room:\n";

    pthread_mutex_lock(&lock);

    int found = 0;
    for (int i = 0; i < client_count; i++)
    {
        if (client_rooms[i] == room)
        {
            strcat(response, "  - ");
            strcat(response, client_usernames[i]);
            strcat(response, "\n");
            found++;
        }
    }

    pthread_mutex_unlock(&lock);

    if (!found)
        strcat(response, "  (no one here)\n");

    strcat(response, "END_WHO\n");
    send(client_socket, response, strlen(response), 0);
}

/* ------------------------------------------------------------------ */
/* Send a direct message to a specific user                            */
/* ------------------------------------------------------------------ */
void send_dm(int sender_socket, const char *sender, const char *target, const char *msg)
{
    char ts[10];
    get_timestamp(ts, sizeof(ts));

    char dm_msg[BUFFER_SIZE + 200];
    snprintf(dm_msg, sizeof(dm_msg), "%s[DM from %s]: %s\n", ts, sender, msg);

    pthread_mutex_lock(&lock);

    int delivered = 0;
    for (int i = 0; i < client_count; i++)
    {
        if (strcmp(client_usernames[i], target) == 0)
        {
            send(client_sockets[i], dm_msg, strlen(dm_msg), 0);
            delivered = 1;
            break;
        }
    }

    pthread_mutex_unlock(&lock);

    if (delivered)
    {
        char echo[BUFFER_SIZE + 200];
        snprintf(echo, sizeof(echo), "%s[DM to %s]: %s\n", ts, target, msg);
        send(sender_socket, echo, strlen(echo), 0);
    }
    else
    {
        send(sender_socket, "DM_FAILED: User not online.\n", 28, 0);
    }
}

/* ------------------------------------------------------------------ */
/* Client handler thread                                                */
/* ------------------------------------------------------------------ */
void *handle_client(void *arg)
{
    int room  = -1;
    int index = -1;
    int client_socket = *((int *)arg);
    free(arg);

    char buffer[BUFFER_SIZE];
    char username[50], password[50], role[50];
    int bytes_read;

    printf("Client connected\n");

    /* ---- LOGIN ---- */
    bytes_read = recv(client_socket, buffer, sizeof(buffer) - 1, 0);
    if (bytes_read <= 0)
    {
        close(client_socket);
        pthread_exit(NULL);
    }
    buffer[bytes_read] = '\0';

    if (sscanf(buffer, "LOGIN %s %s", username, password) != 2)
    {
        send(client_socket, "LOGIN_FAILED\n", 13, 0);
        close(client_socket);
        pthread_exit(NULL);
    }

    if (!login_user(username, password, role))
    {
        send(client_socket, "LOGIN_FAILED\n", 13, 0);
        close(client_socket);
        pthread_exit(NULL);
    }

    /* ---- BAN CHECK ---- */
    if (is_banned(username))
    {
        send(client_socket, "BANNED\n", 7, 0);
        close(client_socket);
        pthread_exit(NULL);
    }

    pthread_mutex_lock(&lock);

    if (is_user_logged_in(username))
    {
        pthread_mutex_unlock(&lock);
        send(client_socket, "ALREADY_LOGGED_IN\n", 18, 0);
        close(client_socket);
        pthread_exit(NULL);
    }

    add_user(username);

    /* find this client's slot and store username */
    for (int i = 0; i < client_count; i++)
    {
        if (client_sockets[i] == client_socket)
        {
            index = i;
            client_rooms[index] = -1;
            strcpy(client_usernames[index], username);
            break;
        }
    }

    pthread_mutex_unlock(&lock);

    char response[100];
    sprintf(response, "LOGIN_SUCCESS %s\n", role);
    send(client_socket, response, strlen(response), 0);

    printf("User logged in: %s (%s)\n", username, role);

    /* Log login event via IPC pipe to logger process. */
    char login_log[150];
    snprintf(login_log, sizeof(login_log),
             "LOGIN  user='%s' role='%s'\n", username, role);
    log_message(login_log);

    /* ---- CHAT LOOP ---- */
    while ((bytes_read = recv(client_socket, buffer, sizeof(buffer) - 1, 0)) > 0)
    {
        buffer[bytes_read] = '\0';

        /* ROOM SELECTION */
        if (strncmp(buffer, "ROOM", 4) == 0 &&
            (buffer[4] == ' ' || buffer[4] == '\n'))
        {
            sscanf(buffer, "ROOM %d", &room);

            if (!is_valid_room(room))
            {
                send(client_socket, "INVALID_ROOM\n", 13, 0);
                continue;
            }

            client_rooms[index] = room;
            printf("User %s joined room %d\n", username, room);

            /* Log room join via IPC pipe. */
            char room_log[150];
            snprintf(room_log, sizeof(room_log),
                     "ROOM_JOIN  user='%s' room=%d\n", username, room);
            log_message(room_log);
            send(client_socket, "ROOM_OK\n", 8, 0);
            continue;
        }

        else if (strncmp(buffer, "GET_ROOMS", 9) == 0)
        {
            send_rooms(client_socket);
            continue;
        }

        else if (strncmp(buffer, "GET_HISTORY", 11) == 0)
        {
            if (client_rooms[index] != -1)
                send_chat_history(client_socket, client_rooms[index]);
            continue;
        }

        /* /who — list users in current room */
        else if (strncmp(buffer, "/who", 4) == 0)
        {
            int cur = client_rooms[index];
            if (cur == -1)
                send(client_socket, "Join a room first.\n", 19, 0);
            else
                send_who(client_socket, cur);
            continue;
        }

        /* /dm <target> <message> — private message */
        else if (strncmp(buffer, "/dm ", 4) == 0)
        {
            char target[50], msg[BUFFER_SIZE];
            if (sscanf(buffer, "/dm %49s %[^\n]", target, msg) == 2)
                send_dm(client_socket, username, target, msg);
            else
                send(client_socket, "Usage: /dm <user> <message>\n", 28, 0);
            continue;
        }

        /* /ban <target> — admin only */
        else if (strncmp(buffer, "/ban ", 5) == 0)
        {
            if (strcmp(role, "admin") != 0)
            {
                send(client_socket, "NOT_ADMIN\n", 10, 0);
                continue;
            }

            char target[50];
            sscanf(buffer, "/ban %49s", target);

            if (strlen(target) == 0)
            {
                send(client_socket, "Usage: /ban <username>\n", 23, 0);
                continue;
            }

            ban_user(target);

            /* kick the banned user if currently online */
            pthread_mutex_lock(&lock);
            for (int i = 0; i < client_count; i++)
            {
                if (strcmp(client_usernames[i], target) == 0)
                {
                    send(client_sockets[i], "YOU_ARE_BANNED\n", 15, 0);
                    close(client_sockets[i]);
                    break;
                }
            }
            pthread_mutex_unlock(&lock);

            char ban_ack[100];
            snprintf(ban_ack, sizeof(ban_ack), "BAN_OK %s\n", target);
            send(client_socket, ban_ack, strlen(ban_ack), 0);

            printf("Admin %s banned user %s\n", username, target);

            /* Log ban event via IPC pipe. */
            char ban_log[150];
            snprintf(ban_log, sizeof(ban_log),
                     "BAN  admin='%s' target='%s'\n", username, target);
            log_message(ban_log);
            continue;
        }

        /* /ADDROOM — admin only */
        else if (strncmp(buffer, "/ADDROOM", 8) == 0)
        {
            if (strcmp(role, "admin") != 0)
            {
                send(client_socket, "NOT_ADMIN\n", 10, 0);
                continue;
            }

            int room_id;
            char room_name[100];

            if (sscanf(buffer, "/ADDROOM %d %[^\n]", &room_id, room_name) != 2)
            {
                send(client_socket, "INVALID_COMMAND\n", 16, 0);
                continue;
            }

            if (is_valid_room(room_id))
            {
                send(client_socket, "ROOM_EXISTS\n", 12, 0);
                continue;
            }

            FILE *fp = fopen("data/rooms.txt", "a");
            fprintf(fp, "%d %s\n", room_id, room_name);
            fclose(fp);

            char filename[100];
            sprintf(filename, "data/chatlog_room%d.txt", room_id);
            FILE *cf = fopen(filename, "w");
            if (cf) fclose(cf);

            send(client_socket, "ROOM_ADDED\n", 11, 0);

            printf("Admin %s added room %d (%s)\n", username, room_id, room_name);
            continue;
        }

        /* NORMAL CHAT MESSAGE */
        int current_room = client_rooms[index];

        if (current_room == -1)
            continue;

        /* prepend [HH:MM] timestamp */
        char ts[10];
        get_timestamp(ts, sizeof(ts));

        char stamped[BUFFER_SIZE + 20];
        snprintf(stamped, sizeof(stamped), "%s%s", ts, buffer);

        /* log to room file */
        char filename[50];
        sprintf(filename, "data/chatlog_room%d.txt", current_room);

        FILE *fp = fopen(filename, "a");
        if (fp)
        {
            fprintf(fp, "%s", stamped);
            fclose(fp);
        }

        /* broadcast the stamped message */
        broadcast_message(stamped, client_socket, current_room);
    }

    /* CLEANUP */
    printf("User disconnected: %s\n", username);

    /* Log disconnect event via IPC pipe. */
    char disc_log[150];
    snprintf(disc_log, sizeof(disc_log),
             "LOGOUT  user='%s'\n", username);
    log_message(disc_log);

    pthread_mutex_lock(&lock);
    remove_user(username);
    pthread_mutex_unlock(&lock);

    close(client_socket);
    remove_client(client_socket);

    pthread_exit(NULL);
}

/* ------------------------------------------------------------------ */
/* Main                                                                 */
/* ------------------------------------------------------------------ */
int main()
{
    int server_socket;
    struct sockaddr_in server_addr, client_addr;
    socklen_t client_len = sizeof(client_addr);

    pthread_mutex_init(&lock, NULL);

    /* ----------------------------------------------------------------
     * IPC SETUP — fork the logger process then open the write end of
     * the named pipe.  All log_message() calls in this process will
     * be forwarded to the logger process via the FIFO.
     * -------------------------------------------------------------- */

    /* Step 1: create the FIFO on disk before forking so both the
       child and parent can open it.                                   */
    if (mkfifo(PIPE_PATH, 0666) < 0)
        perror("mkfifo (may already exist — continuing)");

    /* Step 2: fork the logger process.                                */
    pid_t logger_pid = fork();

    if (logger_pid < 0)
    {
        perror("fork logger_process");
        exit(EXIT_FAILURE);
    }

    if (logger_pid == 0)
    {
        /* ---- CHILD: become the logger process ---- */
        execl("./logger_process", "logger_process", (char *)NULL);
        /* execl only returns on error */
        perror("execl logger_process");
        exit(EXIT_FAILURE);
    }

    /* ---- PARENT (server): open the write end of the FIFO ---- */
    /* Small delay so the child process has time to open its read end. */
    usleep(100000);   /* 100 ms */

    if (ipc_init() < 0)
    {
        fprintf(stderr, "Failed to initialise IPC pipe. Exiting.\n");
        kill(logger_pid, SIGTERM);
        exit(EXIT_FAILURE);
    }

    printf("[server] Logger process started (PID %d).\n", logger_pid);
    log_message("=== Chat server started ===\n");

    /* -------------------------------------------------------------- */

    server_socket = socket(AF_INET, SOCK_STREAM, 0);

    int opt = 1;
    setsockopt(server_socket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    server_addr.sin_family      = AF_INET;
    server_addr.sin_port        = htons(PORT);
    server_addr.sin_addr.s_addr = INADDR_ANY;

    bind(server_socket, (struct sockaddr *)&server_addr, sizeof(server_addr));
    listen(server_socket, 10);

    printf("Server running on port %d...\n", PORT);

    while (1)
    {
        int *client_socket = malloc(sizeof(int));

        *client_socket = accept(server_socket,
                                (struct sockaddr *)&client_addr,
                                &client_len);

        pthread_mutex_lock(&lock);
        client_sockets[client_count]      = *client_socket;
        client_rooms[client_count]        = -1;
        client_usernames[client_count][0] = '\0';
        client_count++;
        pthread_mutex_unlock(&lock);

        pthread_t tid;
        pthread_create(&tid, NULL, handle_client, client_socket);
        pthread_detach(tid);
    }

    /* Graceful shutdown (reached only if the accept loop is broken). */
    log_message("=== Chat server stopped ===\n");
    ipc_close();
    waitpid(logger_pid, NULL, 0);

    close(server_socket);
    pthread_mutex_destroy(&lock);
    return 0;
}