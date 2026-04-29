#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <time.h>
#include "../include/logger.h"

void log_message(char message[])
{
    int fd = open("data/chatlog.txt",
                  O_WRONLY | O_CREAT | O_APPEND,
                  0644);

    if (fd < 0)
        return;

    struct flock lock;

    lock.l_type = F_WRLCK;
    lock.l_whence = SEEK_SET;
    lock.l_start = 0;
    lock.l_len = 0;

    /* wait for lock */
    fcntl(fd, F_SETLKW, &lock);

    /* timestamp */
    time_t now = time(NULL);
    char *time_str = ctime(&now);

    char buffer[2048];

    snprintf(buffer, sizeof(buffer),
             "[%.*s] %s",
             (int)(strlen(time_str) - 1),
             time_str,
             message);

    write(fd, buffer, strlen(buffer));

    /* unlock */
    lock.l_type = F_UNLCK;
    fcntl(fd, F_SETLK, &lock);

    close(fd);
}