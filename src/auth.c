#include <stdio.h>
#include <string.h>
#include "../include/auth.h"

int login_user(char username[], char password[], char role[])
{
    FILE *fp = fopen("./data/users.txt", "r");

    if (fp == NULL)
    {
        printf("Could not open users file.\n");
        return 0;
    }

    char line[200];
    char file_user[50], file_pass[50], file_role[50];

    while (fgets(line, sizeof(line), fp))
    {
        sscanf(line, "%49[^,],%49[^,],%49s",
               file_user, file_pass, file_role);

        if (strcmp(username, file_user) == 0 &&
            strcmp(password, file_pass) == 0)
        {
            strcpy(role, file_role);
            fclose(fp);
            return 1;
        }
    }

    fclose(fp);
    return 0;
}