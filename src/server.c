#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <pthread.h>

#define PORT 8080
#define MAX_CLIENTS 100
#define BUFFER_SIZE 1024

int client_sockets[MAX_CLIENTS];
int client_count = 0;

pthread_mutex_t lock;

/* Broadcast message to everyone except sender */
void broadcast_message(char *message, int sender_socket)
{
    pthread_mutex_lock(&lock);

    for (int i = 0; i < client_count; i++)
    {
        if (client_sockets[i] != sender_socket)
        {
            send(client_sockets[i], message, strlen(message), 0);
        }
    }

    pthread_mutex_unlock(&lock);
}

/* Remove disconnected client */
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
            }

            client_count--;
            break;
        }
    }

    pthread_mutex_unlock(&lock);
}

/* Thread function for each client */
void *handle_client(void *arg)
{
    int client_socket = *((int *)arg);
    free(arg);

    char buffer[BUFFER_SIZE];
    int bytes_read;

    printf("Client connected: Socket %d\n", client_socket);

    while ((bytes_read = recv(client_socket, buffer, sizeof(buffer) - 1, 0)) > 0)
    {
        buffer[bytes_read] = '\0';

        printf("Received: %s", buffer);

        broadcast_message(buffer, client_socket);
    }

    printf("Client disconnected: Socket %d\n", client_socket);

    close(client_socket);
    remove_client(client_socket);

    pthread_exit(NULL);
}

int main()
{
    int server_socket;
    struct sockaddr_in server_addr, client_addr;
    socklen_t client_len = sizeof(client_addr);

    pthread_mutex_init(&lock, NULL);

    server_socket = socket(AF_INET, SOCK_STREAM, 0);

    if (server_socket < 0)
    {
        perror("Socket failed");
        return 1;
    }

    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(PORT);
    server_addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(server_socket, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0)
    {
        perror("Bind failed");
        return 1;
    }

    if (listen(server_socket, 10) < 0)
    {
        perror("Listen failed");
        return 1;
    }

    printf("Server started on port %d...\n", PORT);

    while (1)
    {
        int *client_socket = malloc(sizeof(int));

        *client_socket = accept(server_socket,
                                (struct sockaddr *)&client_addr,
                                &client_len);

        if (*client_socket < 0)
        {
            perror("Accept failed");
            free(client_socket);
            continue;
        }

        pthread_mutex_lock(&lock);

        if (client_count < MAX_CLIENTS)
        {
            client_sockets[client_count++] = *client_socket;
        }

        pthread_mutex_unlock(&lock);

        pthread_t tid;
        pthread_create(&tid, NULL, handle_client, client_socket);
        pthread_detach(tid);
    }

    close(server_socket);
    pthread_mutex_destroy(&lock);

    return 0;
}