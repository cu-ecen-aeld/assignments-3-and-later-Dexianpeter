#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#define PORT 9000
#define BACKLOG 10
#define DATA_FILE "/var/tmp/aesdsocketdata"
#define RECV_BUF_SIZE 1024

static volatile sig_atomic_t exit_requested = 0;

static void signal_handler(int signo)
{
    (void)signo;
    exit_requested = 1;
}

static int send_all(int fd, const char *buf, size_t len)
{
    size_t sent_total = 0;

    while (sent_total < len) {
        ssize_t sent = send(fd, buf + sent_total, len - sent_total, 0);
        if (sent < 0) {
            if (errno == EINTR) {
                continue;
            }
            return -1;
        }
        if (sent == 0) {
            return -1;
        }
        sent_total += (size_t)sent;
    }

    return 0;
}

static int send_file_to_client(int client_fd)
{
    FILE *fp = fopen(DATA_FILE, "r");
    if (fp == NULL) {
        syslog(LOG_ERR, "Failed to open %s: %s", DATA_FILE, strerror(errno));
        return -1;
    }

    char buf[RECV_BUF_SIZE];
    size_t bytes_read;

    while ((bytes_read = fread(buf, 1, sizeof(buf), fp)) > 0) {
        if (send_all(client_fd, buf, bytes_read) != 0) {
            fclose(fp);
            return -1;
        }
    }

    if (ferror(fp)) {
        syslog(LOG_ERR, "Failed to read %s", DATA_FILE);
        fclose(fp);
        return -1;
    }

    fclose(fp);
    return 0;
}

static int append_packet_to_file(const char *packet, size_t packet_len)
{
    FILE *fp = fopen(DATA_FILE, "a");
    if (fp == NULL) {
        syslog(LOG_ERR, "Failed to open %s: %s", DATA_FILE, strerror(errno));
        return -1;
    }

    size_t written = fwrite(packet, 1, packet_len, fp);
    if (written != packet_len) {
        syslog(LOG_ERR, "Failed to write complete packet to %s", DATA_FILE);
        fclose(fp);
        return -1;
    }

    fclose(fp);
    return 0;
}

static int handle_client(int client_fd)
{
    char recv_buf[RECV_BUF_SIZE];
    char *packet = NULL;
    size_t packet_len = 0;

    while (!exit_requested) {
        ssize_t bytes_received = recv(client_fd, recv_buf, sizeof(recv_buf), 0);

        if (bytes_received < 0) {
            if (errno == EINTR) {
                continue;
            }
            syslog(LOG_ERR, "recv failed: %s", strerror(errno));
            free(packet);
            return -1;
        }

        if (bytes_received == 0) {
            break;
        }

        for (ssize_t i = 0; i < bytes_received; i++) {
            char *new_packet = realloc(packet, packet_len + 1);
            if (new_packet == NULL) {
                syslog(LOG_ERR, "realloc failed");
                free(packet);
                return -1;
            }

            packet = new_packet;
            packet[packet_len] = recv_buf[i];
            packet_len++;

            if (recv_buf[i] == '\n') {
                if (append_packet_to_file(packet, packet_len) != 0) {
                    free(packet);
                    return -1;
                }

                free(packet);
                packet = NULL;
                packet_len = 0;

                if (send_file_to_client(client_fd) != 0) {
                    return -1;
                }
            }
        }
    }

    free(packet);
    return 0;
}

static int daemonize_process(void)
{
    pid_t pid = fork();

    if (pid < 0) {
        syslog(LOG_ERR, "fork failed: %s", strerror(errno));
        return -1;
    }

    if (pid > 0) {
        exit(EXIT_SUCCESS);
    }

    if (setsid() < 0) {
        syslog(LOG_ERR, "setsid failed: %s", strerror(errno));
        return -1;
    }

    if (chdir("/") < 0) {
        syslog(LOG_ERR, "chdir failed: %s", strerror(errno));
        return -1;
    }

    int fd = open("/dev/null", O_RDWR);
    if (fd < 0) {
        syslog(LOG_ERR, "open /dev/null failed: %s", strerror(errno));
        return -1;
    }

    dup2(fd, STDIN_FILENO);
    dup2(fd, STDOUT_FILENO);
    dup2(fd, STDERR_FILENO);

    if (fd > STDERR_FILENO) {
        close(fd);
    }

    return 0;
}

int main(int argc, char *argv[])
{
    bool daemon_mode = false;

    if (argc == 2 && strcmp(argv[1], "-d") == 0) {
        daemon_mode = true;
    } else if (argc > 1) {
        fprintf(stderr, "Usage: %s [-d]\n", argv[0]);
        return -1;
    }

    openlog("aesdsocket", LOG_PID, LOG_USER);

    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = signal_handler;

    if (sigaction(SIGINT, &sa, NULL) != 0) {
        syslog(LOG_ERR, "sigaction SIGINT failed: %s", strerror(errno));
        closelog();
        return -1;
    }

    if (sigaction(SIGTERM, &sa, NULL) != 0) {
        syslog(LOG_ERR, "sigaction SIGTERM failed: %s", strerror(errno));
        closelog();
        return -1;
    }

    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        syslog(LOG_ERR, "socket failed: %s", strerror(errno));
        closelog();
        return -1;
    }

    int optval = 1;
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval)) != 0) {
        syslog(LOG_ERR, "setsockopt failed: %s", strerror(errno));
        close(server_fd);
        closelog();
        return -1;
    }

    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(PORT);

    if (bind(server_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) != 0) {
        syslog(LOG_ERR, "bind failed: %s", strerror(errno));
        close(server_fd);
        closelog();
        return -1;
    }

    if (daemon_mode) {
        if (daemonize_process() != 0) {
            close(server_fd);
            closelog();
            return -1;
        }
    }

    if (listen(server_fd, BACKLOG) != 0) {
        syslog(LOG_ERR, "listen failed: %s", strerror(errno));
        close(server_fd);
        closelog();
        return -1;
    }

    while (!exit_requested) {
        struct sockaddr_in client_addr;
        socklen_t client_addr_len = sizeof(client_addr);

        int client_fd = accept(server_fd, (struct sockaddr *)&client_addr, &client_addr_len);
        if (client_fd < 0) {
            if (errno == EINTR && exit_requested) {
                break;
            }
            if (errno == EINTR) {
                continue;
            }
            syslog(LOG_ERR, "accept failed: %s", strerror(errno));
            break;
        }

        char client_ip[INET_ADDRSTRLEN];
        const char *ip_result = inet_ntop(AF_INET, &client_addr.sin_addr,
                                          client_ip, sizeof(client_ip));
        if (ip_result == NULL) {
            strncpy(client_ip, "unknown", sizeof(client_ip));
            client_ip[sizeof(client_ip) - 1] = '\0';
        }

        syslog(LOG_DEBUG, "Accepted connection from %s", client_ip);

        handle_client(client_fd);

        close(client_fd);

        syslog(LOG_DEBUG, "Closed connection from %s", client_ip);
    }

    if (exit_requested) {
        syslog(LOG_DEBUG, "Caught signal, exiting");
    }

    close(server_fd);
    remove(DATA_FILE);
    closelog();

    return 0;
}
