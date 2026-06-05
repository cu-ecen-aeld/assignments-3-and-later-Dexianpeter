#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>

int main(int argc, char *argv[]) {
    openlog("aesd-writer", LOG_PID, LOG_USER);

    if (argc != 3) {
        syslog(LOG_ERR, "Error: Invalid number of arguments. Expected 2, got %d", argc - 1);
        fprintf(stderr, "Usage: %s <file_path> <string_to_write>\n", argv[0]);
        closelog();
        return 1;
    }

    char *writefile = argv[1];
    char *textstr = argv[2];

    syslog(LOG_DEBUG, "Writing %s to %s", textstr, writefile);

    int fd = open(writefile, O_WRONLY | O_CREAT | O_TRUNC, 0664);
    if (fd == -1) {
        syslog(LOG_ERR, "Error: Failed to open or create file %s", writefile);
        perror("File open error");
        closelog();
        return 1;
    }

    ssize_t bytes_written = write(fd, textstr, strlen(textstr));
    if (bytes_written == -1) {
        syslog(LOG_ERR, "Error: Failed to write data to file %s", writefile);
        perror("File write error");
        close(fd);
        closelog();
        return 1;
    }

    close(fd);
    closelog();
    return 0;
}
