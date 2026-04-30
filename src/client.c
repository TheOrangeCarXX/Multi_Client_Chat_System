#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <semaphore.h>

#define PORT 8080
#define BUFFER_SIZE 1024

int sockfd;
char username[50];
sem_t rooms_ready;

void trim_newline(char *str)
{
    int len = strlen(str);
    if (len > 0 && str[len - 1] == '\n')
        str[len - 1] = '\0';
}

/* ------------------------------------------------------------------ */
/* Receive thread — handles all incoming server messages               */
/* ------------------------------------------------------------------ */
void *receive_messages(void *arg)
{
    char buffer[BUFFER_SIZE];
    int bytes_read;

    while ((bytes_read = recv(sockfd, buffer, sizeof(buffer) - 1, 0)) > 0)
    {
        buffer[bytes_read] = '\0';

        /* ---- ROOM LIST ---- */
        static char room_buffer[4096] = {0};

        if (strstr(buffer, "ROOM_LIST") || strlen(room_buffer) > 0)
        {
            strcat(room_buffer, buffer);

            if (!strstr(room_buffer, "END_ROOMS"))
                continue;

            char temp[4096];
            strcpy(temp, room_buffer);

            char *line = strtok(temp, "\n");
            while (line)
            {
                if (strcmp(line, "ROOM_LIST") != 0 &&
                    strcmp(line, "END_ROOMS") != 0)
                    printf("%s\n", line);
                line = strtok(NULL, "\n");
            }

            room_buffer[0] = '\0';
            sem_post(&rooms_ready);
            continue;
        }

        /* ---- /WHO LIST ---- */
        static char who_buffer[2048] = {0};

        if (strstr(buffer, "WHO_LIST") || strlen(who_buffer) > 0)
        {
            strcat(who_buffer, buffer);

            if (!strstr(who_buffer, "END_WHO"))
                continue;

            char temp[2048];
            strcpy(temp, who_buffer);

            char *line = strtok(temp, "\n");
            while (line)
            {
                if (strcmp(line, "WHO_LIST") != 0 &&
                    strcmp(line, "END_WHO") != 0)
                    printf("%s\n", line);
                line = strtok(NULL, "\n");
            }

            who_buffer[0] = '\0';
            continue;
        }

        /* ---- SPECIAL RESPONSES ---- */
        if (strncmp(buffer, "INVALID_ROOM", 12) == 0)
        {
            printf("Invalid room selected.\n");
            continue;
        }

        if (strncmp(buffer, "ROOM_OK", 7) == 0)
        {
            send(sockfd, "GET_HISTORY\n", 12, 0);
            continue;
        }

        if (strncmp(buffer, "ROOM_ADDED", 10) == 0)
        {
            printf("Room created successfully.\n");
            continue;
        }

        if (strncmp(buffer, "ROOM_EXISTS", 11) == 0)
        {
            printf("Room already exists.\n");
            continue;
        }

        if (strncmp(buffer, "NOT_ADMIN", 9) == 0)
        {
            printf("Only admin can perform this action.\n");
            continue;
        }

        if (strncmp(buffer, "DM_FAILED", 9) == 0)
        {
            printf("%s", buffer);
            continue;
        }

        if (strncmp(buffer, "BAN_OK", 6) == 0)
        {
            char target[50];
            sscanf(buffer, "BAN_OK %s", target);
            printf("User '%s' has been banned.\n", target);
            continue;
        }

        if (strncmp(buffer, "YOU_ARE_BANNED", 14) == 0)
        {
            printf("\nYou have been banned by an admin.\n");
            close(sockfd);
            exit(1);
        }

        if (strncmp(buffer, "BANNED", 6) == 0)
        {
            printf("Your account is banned.\n");
            close(sockfd);
            exit(1);
        }

        /* ---- DEFAULT: chat messages, history, DMs ---- */
        printf("%s", buffer);
        fflush(stdout);
    }

    printf("\nDisconnected.\n");
    exit(0);
}

/* ------------------------------------------------------------------ */
/* Main                                                                 */
/* ------------------------------------------------------------------ */
int main()
{
    char password[50];
    char message[BUFFER_SIZE];
    char final_message[BUFFER_SIZE + 100];
    char buffer[BUFFER_SIZE];
    char role[20];

    struct sockaddr_in server_addr;
    int login_success = 0;

    sem_init(&rooms_ready, 0, 0);

    /* ---- LOGIN LOOP ---- */
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
        server_addr.sin_port   = htons(PORT);

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

        char login_msg[200];
        snprintf(login_msg, sizeof(login_msg),
                 "LOGIN %s %s\n", username, password);
        send(sockfd, login_msg, strlen(login_msg), 0);

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
            printf("Invalid credentials.\n\n");
            close(sockfd);
            continue;
        }

        if (strncmp(buffer, "ALREADY_LOGGED_IN", 17) == 0)
        {
            printf("User already logged in.\n\n");
            close(sockfd);
            continue;
        }

        if (strncmp(buffer, "BANNED", 6) == 0)
        {
            printf("Your account has been banned.\n");
            close(sockfd);
            return 1;
        }

        if (strncmp(buffer, "LOGIN_SUCCESS", 13) == 0)
        {
            printf("Login successful!\n");
            sscanf(buffer, "LOGIN_SUCCESS %s", role);
            login_success = 1;
        }
        else
        {
            printf("Unknown response.\n");
            close(sockfd);
        }
    }

    /* ---- START RECEIVE THREAD ---- */
    pthread_t tid;
    pthread_create(&tid, NULL, receive_messages, NULL);

    /* ---- MENU ---- */
    int choice;

    while (1)
    {
        printf("\n===== MENU =====\n");
        printf("1. Enter Chat\n");
        printf("2. Add Room (Admin Only)\n");
        printf("3. Ban User (Admin Only)\n");
        printf("4. Logout\n");
        printf("Enter choice: ");
        scanf("%d", &choice);
        getchar();

        if (choice == 1)
        {
            int room;

            printf("\nSelect Chat Room:\n");
            send(sockfd, "GET_ROOMS\n", 10, 0);
            sem_wait(&rooms_ready);

            printf("Enter room number: ");
            scanf("%d", &room);
            getchar();

            char room_msg[50];
            sprintf(room_msg, "ROOM %d\n", room);
            send(sockfd, room_msg, strlen(room_msg), 0);

            printf("(type 'exit' to leave, '/who' to list users, '/dm <user> <msg>' to DM)\n");

            while (1)
            {
                fgets(message, sizeof(message), stdin);
                trim_newline(message);

                if (strcmp(message, "exit") == 0)
                    break;

                /* pass /who and /dm straight to server as-is */
                if (strncmp(message, "/who", 4) == 0 ||
                    strncmp(message, "/dm ", 4) == 0)
                {
                    char cmd[BUFFER_SIZE + 2];
                    snprintf(cmd, sizeof(cmd), "%s\n", message);
                    send(sockfd, cmd, strlen(cmd), 0);
                    continue;
                }

                snprintf(final_message, sizeof(final_message),
                         "%s: %s\n", username, message);
                send(sockfd, final_message, strlen(final_message), 0);
            }
        }

        else if (choice == 2)
        {
            if (strcmp(role, "admin") != 0)
            {
                printf("Only admin can perform this action.\n");
                continue;
            }

            int room_id;
            char room_name[100];

            printf("Enter room ID: ");
            scanf("%d", &room_id);
            getchar();

            printf("Enter room name: ");
            fgets(room_name, sizeof(room_name), stdin);
            trim_newline(room_name);

            char cmd[200];
            sprintf(cmd, "/ADDROOM %d %s\n", room_id, room_name);
            send(sockfd, cmd, strlen(cmd), 0);
        }

        else if (choice == 3)
        {
            if (strcmp(role, "admin") != 0)
            {
                printf("Only admin can perform this action.\n");
                continue;
            }

            char target[50];
            printf("Enter username to ban: ");
            fgets(target, sizeof(target), stdin);
            trim_newline(target);

            char cmd[100];
            snprintf(cmd, sizeof(cmd), "/ban %s\n", target);
            send(sockfd, cmd, strlen(cmd), 0);
        }

        else if (choice == 4)
        {
            printf("Logging out...\n");
            close(sockfd);
            return 0;
        }

        else
        {
            printf("Invalid choice.\n");
        }
    }

    sem_destroy(&rooms_ready);
    return 0;
}