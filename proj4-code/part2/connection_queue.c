#include <stdio.h>
#include <string.h>
#include "connection_queue.h"

int connection_queue_init(connection_queue_t *queue) {
    int result;

    // Initialize integer values
    queue->length = 0;
    queue->read_idx = 0;
    queue->write_idx = 0;
    queue->shutdown = 0;

    // Initialize thread synchronization primitives
    result = pthread_mutex_init(&queue->lock, NULL);
    if (result) {
        fprintf(stderr, "pthread_mutex_init: %s\n", strerror(result));
        return -1;
    }

    result = pthread_cond_init(&queue->full, NULL);
    if (result) {
        fprintf(stderr, "pthread_cond_init: %s\n", strerror(result));
        pthread_mutex_destroy(&queue->lock);
        return -1;
    }

    result = pthread_cond_init(&queue->empty, NULL);
    if (result) {
        fprintf(stderr, "pthread_cond_init: %s\n", strerror(result));
        pthread_mutex_destroy(&queue->lock);
        pthread_cond_destroy(&queue->full);
        return -1;
    }

    return 0;
}

int connection_enqueue(connection_queue_t *queue, int connection_fd) {
    int result;

    // Lock mutex
    result = pthread_mutex_lock(&queue->lock);
    if (result) {
        fprintf(stderr, "pthread_mutex_lock: %s\n", strerror(result));
        return -1;
    }

    while (queue->length == CAPACITY) {
        // Exit instead of waiting on shutdown
        if (queue->shutdown) {
            pthread_mutex_unlock(&queue->lock);
            return -1;
        }

        // Wait until space available in queue
        result = pthread_cond_wait(&queue->full, &queue->lock);
        if (result) {
            fprintf(stderr, "pthread_cond_wait: %s\n", strerror(result));
            return -1;
        }
    }

    // Add item to queue
    queue->client_fds[queue->write_idx] = connection_fd;
    queue->length++;
    if (++queue->write_idx == CAPACITY)
        queue->write_idx = 0;

    // Signal waiting threads
    result = pthread_cond_signal(&queue->empty);
    if (result) {
        fprintf(stderr, "pthread_cond_signal: %s\n", strerror(result));
        pthread_mutex_unlock(&queue->lock);
        return -1;
    }

    // Unlock mutex
    result = pthread_mutex_unlock(&queue->lock);
    if (result) {
        fprintf(stderr, "pthread_mutex_unlock: %s\n", strerror(result));
        return -1;
    }

    return 0;
}

int connection_dequeue(connection_queue_t *queue) {
    int result;

    // Lock mutex
    result = pthread_mutex_lock(&queue->lock);
    if (result) {
        fprintf(stderr, "pthread_mutex_lock: %s\n", strerror(result));
        return -1;
    }

    while (queue->length == 0) {
        // Exit when queue empty instead of blocking on shutdown
        if (queue->shutdown) {
            pthread_mutex_unlock(&queue->lock);
            return -1;
        }

        // Wait items available to dequeue
        result = pthread_cond_wait(&queue->empty, &queue->lock);
        if (result) {
            fprintf(stderr, "pthread_cond_wait: %s\n", strerror(result));
            return -1;
        }
    }

    // Read item from queue
    int fd = queue->client_fds[queue->read_idx];
    queue->length--;
    if (++queue->read_idx == CAPACITY)
        queue->read_idx = 0;

    // Signal waiting threads
    result = pthread_cond_signal(&queue->full);
    if (result) {
        fprintf(stderr, "pthread_cond_signal: %s\n", strerror(result));
        pthread_mutex_unlock(&queue->lock);
        return -1;
    }

    // Unlock mutex
    result = pthread_mutex_unlock(&queue->lock);
    if (result) {
        fprintf(stderr, "pthread_mutex_unlock: %s\n", strerror(result));
        return -1;
    }

    return fd;
}

int connection_queue_shutdown(connection_queue_t *queue) {
    int ret_val = 0;
    int result;

    queue->shutdown = 1;

    // Broadcast to threads
    result = pthread_cond_broadcast(&queue->full);
    if (result) {
        fprintf(stderr, "pthread_cond_broadcast: %s\n", strerror(result));
        ret_val = -1;
    }

    result = pthread_cond_broadcast(&queue->empty);
    if (result) {
        fprintf(stderr, "pthread_cond_broadcast: %s\n", strerror(result));
        ret_val = -1;
    }

    return ret_val;
}

int connection_queue_free(connection_queue_t *queue) {
    int ret_val = 0;
    int result;

    // Free thread synchronization primitives
    result = pthread_mutex_destroy(&queue->lock);
    if (result) {
        fprintf(stderr, "pthread_mutex_destroy: %s\n", strerror(result));
        ret_val = -1;
    }

    result = pthread_cond_destroy(&queue->full);
    if (result) {
        fprintf(stderr, "pthread_cond_destroy: %s\n", strerror(result));
        ret_val = -1;
    }

    result = pthread_cond_destroy(&queue->empty);
    if (result) {
        fprintf(stderr, "pthread_cond_destroy: %s\n", strerror(result));
        ret_val = -1;
    }

    return ret_val;
}
