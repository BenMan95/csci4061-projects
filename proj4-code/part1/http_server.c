#include <errno.h>
#include <netdb.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <unistd.h>

#include "http.h"

#define BUFSIZE 512
#define LISTEN_QUEUE_LEN 5

int keep_going = 1;

void handle_sigint(int signo) {
    keep_going = 0;
}

int main(int argc, char **argv) {
    // First command is directory to serve, second command is port
    if (argc != 3) {
        printf("Usage: %s <directory> <port>\n", argv[0]);
        return 1;
    }

    int ret_val;

    // Read arguments
    const char *serve_dir = argv[1];
    const char *port = argv[2];

    // Setup sigaction struct
    struct sigaction sigact;
    sigact.sa_handler = handle_sigint;
    sigfillset(&sigact.sa_mask);
    sigact.sa_flags = 0;

    // Set action for signal
    ret_val = sigaction(SIGINT, &sigact, NULL);
    if (ret_val == -1) {
        perror("sigaction");
        return 1;
    }

    // Set up arguments
    struct addrinfo hints;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;
    struct addrinfo *server;

    // Set up address info
    ret_val = getaddrinfo(NULL, port, &hints, &server);
    if (ret_val != 0) {
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(ret_val));
        return 1;
    }

    // Get socket descriptor
    int sock_fd = socket(server->ai_family, server->ai_socktype, server-> ai_protocol);
    if (sock_fd == -1) {
        perror("socket");
        freeaddrinfo(server);
        return 1;
    }

    // Bind socket to receive at port
    ret_val = bind(sock_fd, server->ai_addr, server->ai_addrlen);
    if (ret_val == -1) {
        perror("bind");
        freeaddrinfo(server);
        close(sock_fd);
        return 1;
    }

    // Cleanup address info
    freeaddrinfo(server);

    // Designate server socket
    ret_val = listen(sock_fd, LISTEN_QUEUE_LEN);
    if (ret_val == -1) {
        perror("listen");
        close(sock_fd);
        return 1;
    }

    while (keep_going) {
        // Wait for client request
        int client_fd = accept(sock_fd, NULL, NULL);
        if (client_fd == -1) {
            if (errno == EINTR)
                break;
            perror("accept");
            close(sock_fd);
            return 1;
        }

        // Read request from client
        char res_name[BUFSIZE];
        if (read_http_request(client_fd, res_name) == -1) {
            if (close(client_fd) == -1)
                perror("close");
            continue;
        }

        // Get path to resource
        char res_path[BUFSIZE];
        strcpy(res_path, serve_dir);
        strcat(res_path, res_name);

        // Write response to client
        write_http_response(client_fd, res_path);

        // if (write_http_response(client_fd, res_path) == -1) {
        //     if (close(client_fd) == -1)
        //         perror("close");
        //     continue;
        // }
        //
        // Note: Error handling logic not necessary since the code below
        // does the same cleanup steps

        // Clean up
        if (close(client_fd) == -1)
            perror("close");
    }

    // Clean up
    if (close(sock_fd) == -1) {
        perror("close");
        return 1;
    }

    return 0;
}
