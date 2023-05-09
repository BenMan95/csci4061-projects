#include <errno.h>
#include <netdb.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <unistd.h>

#include "connection_queue.h"
#include "http.h"

#define BUFSIZE 512
#define LISTEN_QUEUE_LEN 5
#define N_THREADS 5

int keep_going = 1;
const char *serve_dir;

void handle_sigint(int signo) {
    keep_going = 0;
}

void *thread_func(void *arg) {
    connection_queue_t *queue = (connection_queue_t*) arg;

    while (1) {
        int client_fd = connection_dequeue(queue);
        if (client_fd == -1) {
            if (queue->shutdown)
                return NULL;
            continue;
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

    return NULL;
}

int main(int argc, char **argv) {
    int result;

    // First command is directory to serve, second command is port
    if (argc != 3) {
        printf("Usage: %s <directory> <port>\n", argv[0]);
        return 1;
    }

    // Read arguments
    serve_dir = argv[1];
    const char *port = argv[2];

    // Setup sigaction struct
    struct sigaction sigact;
    sigact.sa_handler = handle_sigint;
    sigfillset(&sigact.sa_mask);
    sigact.sa_flags = 0;

    // Set action for signal
    result = sigaction(SIGINT, &sigact, NULL);
    if (result == -1) {
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
    result = getaddrinfo(NULL, port, &hints, &server);
    if (result != 0) {
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(result));
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
    result = bind(sock_fd, server->ai_addr, server->ai_addrlen);
    if (result == -1) {
        perror("bind");
        freeaddrinfo(server);
        close(sock_fd);
        return 1;
    }

    // Cleanup address info
    freeaddrinfo(server);

    // Designate server socket
    result = listen(sock_fd, LISTEN_QUEUE_LEN);
    if (result == -1) {
        perror("listen");
        close(sock_fd);
        return 1;
    }

    // Set up signal mask struct for worker threads
    sigset_t newset;
    if (sigfillset(&newset) == -1) {
        perror("sigfillset");
        close(sock_fd);
        return 1;
    }

    // Set signal mask
    sigset_t oldset;
    if (sigprocmask(SIG_SETMASK, &newset, &oldset) == -1) {
        perror("sigprocmask");
        close(sock_fd);
        return 1;
    }

    // Initialize queue
    connection_queue_t queue;
    if (connection_queue_init(&queue) == -1) {
        close(sock_fd);
        return 1;
    }

    // Create worker threads
    pthread_t threads[N_THREADS];
    for (int i = 0; i < N_THREADS; i++) {
        result = pthread_create(threads + i, NULL, thread_func, &queue);

        if (result) {
            fprintf(stderr, "pthread_create: %s\n", strerror(result));
            close(sock_fd);
            connection_queue_shutdown(&queue);
            for (int j = 0; j < i; j++)
                pthread_join(threads[j], NULL);
            connection_queue_free(&queue);
            return 1;
        }
    }

    // Restore signal mask
    if (sigprocmask(SIG_SETMASK, &oldset, NULL) == -1) {
        perror("sigprocmask");
        close(sock_fd);
        connection_queue_shutdown(&queue);
        for (int i = 0; i < N_THREADS; i++)
            pthread_join(threads[i], NULL);
        connection_queue_free(&queue);
        return 1;
    }

    // Begin accept loop
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

        connection_enqueue(&queue, client_fd);
    }

    // Clean up and return
    int ret_val = 0;

    if (connection_queue_shutdown(&queue) == -1)
        ret_val = 1;

    if (close(sock_fd) == -1) {
        perror("close");
        ret_val = 1;
    }

    for (int i = 0; i < N_THREADS; i++) {
        result = pthread_join(threads[i], NULL);
        if (result) {
            fprintf(stderr, "pthread_join: %s\n", gai_strerror(result));
            ret_val = 1;
        }
    }

    if (connection_queue_free(&queue) == -1)
        ret_val = 1;

    return ret_val;
}
