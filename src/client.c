#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <pthread.h>

#define PORT 8080
#define BUFFER_SIZE 1024

int sockfd;
char username[50];

void trim_newline(char *str)
{
    int len = strlen(str);
    if (len > 0 && str[len - 1] == '\n')
        str[len - 1] = '\0';
}

/* receive messages */
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
    char password[50];
    char message[BUFFER_SIZE];
    char final_message[BUFFER_SIZE + 100];
    char buffer[BUFFER_SIZE];

    struct sockaddr_in server_addr;

    int login_success = 0;

    /* -------- LOGIN LOOP -------- */
    while (!login_success)
    {
        printf("Username: ");
        fgets(username, sizeof(username), stdin);
        trim_newline(username);

        printf("Password: ");
        fgets(password, sizeof(password), stdin);
        trim_newline(password);

        sockfd = socket(AF_INET, SOCK_STREAM, 0);

        server_addr.sin_family = AF_INET;
        server_addr.sin_port = htons(PORT);

        char *server_ip = getenv("SERVER_IP");
        if (!server_ip)
        {
            printf("SERVER_IP not set. Using default 127.0.0.1\n");
            server_ip = "127.0.0.1";
        }

        server_addr.sin_addr.s_addr = inet_addr(server_ip);

        if (connect(sockfd, (struct sockaddr *)&server_addr,
                    sizeof(server_addr)) < 0)
        {
            printf("Connection failed.\n");
            close(sockfd);
            continue;
        }

        /* -------- SEND LOGIN -------- */
        char login_msg[200];
        snprintf(login_msg, sizeof(login_msg),
                 "LOGIN %s %s\n", username, password);

        send(sockfd, login_msg, strlen(login_msg), 0);

        /* -------- RECEIVE RESPONSE -------- */
        int bytes = recv(sockfd, buffer, sizeof(buffer) - 1, 0);

        if (bytes <= 0)
        {
            printf("Server closed connection.\n");
            close(sockfd);
            continue;
        }

        buffer[bytes] = '\0';

        if (strncmp(buffer, "LOGIN_FAILED", 12) == 0)
        {
            printf("Invalid credentials. Try again.\n\n");
            close(sockfd);
            continue;
        }

        if (strncmp(buffer, "ALREADY_LOGGED_IN", 17) == 0)
        {
            printf("User already logged in. Try another account.\n\n");
            close(sockfd);
            continue;
        }

        if (strncmp(buffer, "LOGIN_SUCCESS", 13) == 0)
        {
            printf("Login successful!\n");
            login_success = 1;
        }
        else
        {
            printf("Unknown response. Try again.\n\n");
            close(sockfd);
        }
    }

    /* -------- START RECEIVING THREAD -------- */
    pthread_t tid;
    pthread_create(&tid, NULL, receive_messages, NULL);

    /* -------- CHAT LOOP -------- */
    while (1)
    {
        fgets(message, sizeof(message), stdin);
        trim_newline(message);

        if (strcmp(message, "exit") == 0)
            break;

        snprintf(final_message, sizeof(final_message),
                 "%s: %s\n", username, message);

        send(sockfd, final_message,
             strlen(final_message), 0);
    }

    close(sockfd);
    return 0;
}