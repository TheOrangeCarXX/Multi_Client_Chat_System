#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <pthread.h>
#include "../include/auth.h"

#define PORT 8080
#define BUFFER_SIZE 1024

int sockfd;
char role[50];

void trim_newline(char *str)
{
    int len = strlen(str);
    if (len > 0 && str[len - 1] == '\n')
        str[len - 1] = '\0';
}

void *receive_messages(void *arg)
{
    char buffer[BUFFER_SIZE];
    int bytes_read;

    while ((bytes_read = recv(sockfd, buffer, sizeof(buffer) - 1, 0)) > 0)
    {
        buffer[bytes_read] = '\0';
        printf("%s", buffer);
        fflush(stdout);
    }

    printf("\nDisconnected.\n");
    exit(0);
}

int main()
{
    char username[50], password[50];
    char message[BUFFER_SIZE];
    char final_message[BUFFER_SIZE + 100];

    printf("Username: ");
    fgets(username, sizeof(username), stdin);
    trim_newline(username);

    printf("Password: ");
    fgets(password, sizeof(password), stdin);
    trim_newline(password);

    if (!login_user(username, password, role))
    {
        printf("Invalid login.\n");
        return 1;
    }

    printf("Login successful. Role = %s\n", role);

    struct sockaddr_in server_addr;

    sockfd = socket(AF_INET, SOCK_STREAM, 0);

    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(PORT);
    server_addr.sin_addr.s_addr = inet_addr("127.0.0.1");

    if (connect(sockfd, (struct sockaddr *)&server_addr,
                sizeof(server_addr)) < 0)
    {
        printf("Connection failed.\n");
        return 1;
    }

    pthread_t tid;
    pthread_create(&tid, NULL, receive_messages, NULL);

    while (1)
    {
        fgets(message, sizeof(message), stdin);
        trim_newline(message);

        if (strcmp(message, "exit") == 0)
            break;

        /* guest is read-only */
        if (strcmp(role, "guest") == 0)
        {
            printf("Guests cannot send messages.\n");
            continue;
        }

        snprintf(final_message, sizeof(final_message),
                 "%s: %s\n", username, message);

        send(sockfd, final_message,
             strlen(final_message), 0);
    }

    close(sockfd);
    return 0;
}