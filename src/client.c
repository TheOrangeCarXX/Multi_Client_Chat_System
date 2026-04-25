#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <pthread.h>

#define PORT 8080
#define BUFFER_SIZE 1024

int sockfd;

/* Thread to continuously receive messages from server */
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

    printf("\nDisconnected from server.\n");
    close(sockfd);
    exit(0);
}

/* Remove newline from fgets */
void trim_newline(char *str)
{
    int len = strlen(str);
    if (len > 0 && str[len - 1] == '\n')
        str[len - 1] = '\0';
}

int main()
{
    struct sockaddr_in server_addr;
    pthread_t recv_thread;

    char name[50];
    char message[BUFFER_SIZE];
    char final_message[BUFFER_SIZE + 100];

    sockfd = socket(AF_INET, SOCK_STREAM, 0);

    if (sockfd < 0)
    {
        perror("Socket failed");
        return 1;
    }

    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(PORT);
    server_addr.sin_addr.s_addr = inet_addr("127.0.0.1");

    if (connect(sockfd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0)
    {
        perror("Connection failed");
        return 1;
    }

    printf("Connected to server.\n");
    printf("Enter your name: ");
    fgets(name, sizeof(name), stdin);
    trim_newline(name);

    pthread_create(&recv_thread, NULL, receive_messages, NULL);

    while (1)
    {
        fgets(message, sizeof(message), stdin);
        trim_newline(message);

        if (strcmp(message, "exit") == 0)
        {
            close(sockfd);
            break;
        }

        snprintf(final_message, sizeof(final_message), "%s: %s\n", name, message);

        send(sockfd, final_message, strlen(final_message), 0);
    }

    return 0;
}