#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <pthread.h>
#include "../include/auth.h"
#include "../include/logger.h"

#define PORT 8080
#define MAX_CLIENTS 100
#define BUFFER_SIZE 1024

#define MAX_USERS 100
char active_users[MAX_USERS][50];
int active_count = 0;

int client_sockets[MAX_CLIENTS];
int client_count = 0;

#define MAX_ROOMS 5
int client_rooms[MAX_CLIENTS];

pthread_mutex_t lock;

/* Broadcast message */
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

/* Remove client */
void remove_client(int socket)
{
    pthread_mutex_lock(&lock);

    for (int i = 0; i < client_count; i++)
    {
        if (client_sockets[i] == socket)
        {
            for (int j = i; j < client_count - 1; j++)
                client_sockets[j] = client_sockets[j + 1];

            client_count--;
            break;
        }
    }

    pthread_mutex_unlock(&lock);
}

/* Active users logic */
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

/* Send chat history */
void send_chat_history(int client_socket, int room)
{
    char filename[50];
    sprintf(filename, "data/chatlog_room%d.txt", room);

    FILE *fp = fopen(filename, "r");
    if (!fp) return;

    char line[1024];

    send(client_socket, "\n--- Chat History ---\n", 25, 0);

    while (fgets(line, sizeof(line), fp))
    {
        send(client_socket, line, strlen(line), 0);
    }

    send(client_socket, "\n--------------------\n", 22, 0);

    fclose(fp);
}

/* Client handler */
void *handle_client(void *arg)
{
    int room;
    int index=-1;
    int client_socket = *((int *)arg);
    free(arg);

    char buffer[BUFFER_SIZE];
    char username[50], password[50], role[50];
    int bytes_read;

    printf("Client connected\n");

    /* LOGIN */
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

    pthread_mutex_lock(&lock);

    if (is_user_logged_in(username))
    {
        pthread_mutex_unlock(&lock);
        send(client_socket, "ALREADY_LOGGED_IN\n", 19, 0);
        close(client_socket);
        pthread_exit(NULL);
    }

    add_user(username);
    /* find index of this client */
    for (int i = 0; i < client_count; i++)
    {
        if (client_sockets[i] == client_socket)
        {
            index = i;
            break;
        }
    }
    pthread_mutex_unlock(&lock);
    send(client_socket, "LOGIN_SUCCESS\n", 14, 0);

    printf("User logged in: %s\n", username);
    /* receive room selection */
    int bytes = recv(client_socket, buffer, sizeof(buffer) - 1, 0);
    if (bytes > 0)
    {
        buffer[bytes] = '\0';
        sscanf(buffer, "ROOM %d", &room);

        client_rooms[index] = room;

        printf("User %s joined room %d\n", username, room);
    }

    /* CHAT LOOP */
    while ((bytes_read = recv(client_socket, buffer, sizeof(buffer) - 1, 0)) > 0)
    {
        buffer[bytes_read] = '\0';

        /* HANDLE ROOM SELECTION */
        if (strncmp(buffer, "ROOM", 4) == 0)
        {
            sscanf(buffer, "ROOM %d", &room);
            client_rooms[index] = room;

            printf("User %s joined room %d\n", username, room);
            continue;
        }

        /* HANDLE CHAT HISTORY */
        if (strncmp(buffer, "GET_HISTORY", 11) == 0)
        {
            send_chat_history(client_socket, client_rooms[index]);
            continue;
        }

        /* NORMAL MESSAGE */
        int current_room = client_rooms[index];

        /* log to room file */
        char filename[50];
        sprintf(filename, "data/chatlog_room%d.txt", current_room);

        FILE *fp = fopen(filename, "a");
        if (fp)
        {
            fprintf(fp, "%s", buffer);
            fclose(fp);
        }

        /* broadcast only to same room */
        broadcast_message(buffer, client_socket, current_room);
    }

    /* CLEANUP */
    printf("User disconnected: %s\n", username);

    pthread_mutex_lock(&lock);
    remove_user(username);
    pthread_mutex_unlock(&lock);

    close(client_socket);
    remove_client(client_socket);

    pthread_exit(NULL);
}

/* MAIN SERVER */
int main()
{
    int server_socket;
    struct sockaddr_in server_addr, client_addr;
    socklen_t client_len = sizeof(client_addr);

    pthread_mutex_init(&lock, NULL);

    server_socket = socket(AF_INET, SOCK_STREAM, 0);

    int opt = 1;
    setsockopt(server_socket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(PORT);
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
        client_sockets[client_count++] = *client_socket;
        pthread_mutex_unlock(&lock);

        pthread_t tid;
        pthread_create(&tid, NULL, handle_client, client_socket);
        pthread_detach(tid);
    }

    close(server_socket);
    pthread_mutex_destroy(&lock);
    return 0;
}