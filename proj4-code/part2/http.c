#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <sys/stat.h>
#include <string.h>
#include <unistd.h>
#include "http.h"

#define BUFSIZE 512

const char *get_mime_type(const char *file_extension) {
    if (strcmp(".txt", file_extension) == 0) {
        return "text/plain";
    } else if (strcmp(".html", file_extension) == 0) {
        return "text/html";
    } else if (strcmp(".jpg", file_extension) == 0) {
        return "image/jpeg";
    } else if (strcmp(".png", file_extension) == 0) {
        return "image/png";
    } else if (strcmp(".pdf", file_extension) == 0) {
        return "application/pdf";
    }

    return NULL;
}

int read_http_request(int fd, char *resource_name) {
    char buf[BUFSIZE];

    // Copy descriptor
    int fd_copy = dup(fd);
    if (fd_copy == -1) {
        perror("dup");
        return -1;
    }

    // Convert to file pointer to read line-by-line with fgets()
    FILE *socket_stream = fdopen(fd_copy, "r");
    if (socket_stream == NULL) {
        perror("fdopen");
        close(fd_copy);
        return -1;
    }

    // Disable stdio buffering
    if (setvbuf(socket_stream, NULL, _IONBF, 0) != 0) {
        perror("setvbuf");
        fclose(socket_stream);
        return -1;
    }

    // Read first line
    char *result = fgets(buf, BUFSIZE, socket_stream);
    if (ferror(socket_stream)) {
        perror("fgets");
        fclose(socket_stream);
        return -1;
    }

    // Parse request
    if (sscanf(buf, "GET %s HTTP/1.0\r\n", resource_name) != 1) {
        printf("Could not parse request");
        fclose(socket_stream);
        return -1;
    }

    // Consume rest of request
    while (result != NULL) {
        result = fgets(buf, BUFSIZE, socket_stream);
        if (strcmp(buf, "\r\n") == 0)
            break;
    }

    // Check for error
    if (ferror(socket_stream)) {
        perror("fgets");
        fclose(socket_stream);
        return -1;
    }

    // Clean up
    if (fclose(socket_stream) != 0) {
        perror("fclose");
        return -1;
    }

    return 0;
}

int write_http_response(int fd, const char *resource_path) {
    char buf[BUFSIZE];

    // Check if file exists and get size
    struct stat statbuf;
    if (stat(resource_path, &statbuf) == -1) {
        // Print and return error if file does exist
        if (errno != ENOENT) {
            perror("stat");
            return -1;
        }

        // If file doesn't exist

        // Format response
        if (sprintf(buf, "HTTP/1.0 404 Not Found\r\n"
                         "Content-Length: 0\r\n"
                         "\r\n") < 0) {
            perror("sprintf");
            return -1;
        }

        // Write response
        if (write(fd, buf, strlen(buf)) < 0) {
            perror("write");
            return -1;
        }

        return 0;
    }

    // Get file extension
    char *extension = strrchr(resource_path, '.');
    if (extension == NULL) {
        fprintf(stderr, "Invalid request\n");
        return -1;
    }

    // Get MIME type
    const char *mime_type = get_mime_type(extension);
    if (mime_type == NULL) {
        fprintf(stderr, "Invalid request\n");
        return -1;
    }

    // Open file
    int file_fd = open(resource_path, O_RDONLY);
    if (file_fd == -1) {
        perror("open");
        return -1;
    }

    // Format status line/headers
    if (sprintf(buf, "HTTP/1.0 200 OK\r\n"
                    "Content-Type: %s\r\n"
                    "Content-Length: %jd\r\n"
                    "\r\n",
                    mime_type, statbuf.st_size) < 0) {
        perror("sprintf");
        close(file_fd);
        return -1;
    }

    // Write status line and headers
    if (write(fd, buf, strlen(buf)) < 0) {
        perror("dprintf");
        close(file_fd);
        return -1;
    }

    // Write content
    int bytes_read;
    while ((bytes_read = read(file_fd, buf, BUFSIZE)) > 0) {
        if (write(fd, buf, bytes_read) == -1) {
            perror("write");
            close(file_fd);
            return -1;
        }
    }

    // Check for errors
    if (bytes_read == -1) {
        perror("read");
        close(file_fd);
        return -1;
    }

    // Clean up
    if (close(file_fd) == -1) {
        perror("close");
        return -1;
    }

    return 0;
}
