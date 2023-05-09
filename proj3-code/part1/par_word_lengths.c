#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#define MAX_WORD_LEN 25

/*
 * Counts the number of occurrences of words of different lengths in a text
 * file and stores the results in an array.
 * file_name: The name of the text file from which to read words
 * counts: An array of integers storing the number of words of each possible
 *     length.  counts[0] is the number of 1-character words, counts [1] is the
 *     number of 2-character words, and so on.
 * Returns 0 on success or -1 on error.
 */
int count_word_lengths(const char *file_name, int *counts) {
    // open file and check for error
    FILE* fd = fopen(file_name, "r");
    if (fd == NULL) {
        perror("fopen");
        return -1;
    }

    // scan words from file and check lengths
    char word[MAX_WORD_LEN];
    while (fscanf(fd, "%s", word) != EOF) {
        int len = strlen(word);
        counts[len-1]++;
    }

    // check for errors
    if (ferror(fd)) {
        perror("fscanf");
        fclose(fd);
        return -1;
    }

    // clean up
    fclose(fd);
    return 0;
}

/*
 * Processes a particular file (counting the number of words of each length)
 * and writes the results to a file descriptor.
 * This function should be called in child processes.
 * file_name: The name of the file to analyze.
 * out_fd: The file descriptor to which results are written
 * Returns 0 on success or -1 on error
 */
int process_file(const char *file_name, int out_fd) {
    // get word lengths
    int counts[MAX_WORD_LEN];
    memset(counts, 0, sizeof(int) * MAX_WORD_LEN);
    if (count_word_lengths(file_name, counts))
        return -1;

    // write results to pipe
    if (write(out_fd, counts, sizeof(int) * MAX_WORD_LEN) == -1) {
        perror("write");
        return -1;
    }

    return 0;
}

int main(int argc, char **argv) {
    if (argc == 1) {
        // No files to consume, return immediately
        return 0;
    }

    // TODO Create a pipe for child processes to write their results
    int pipe_fds[2];
    if (pipe(pipe_fds) == -1) {
        perror("pipe");
        return -1;
    }

    // TODO Fork a child to analyze each specified file (names are argv[1], argv[2], ...)
    for (int i = 1; i < argc; i++) {
        pid_t pid = fork();

        // return to top of loop if parent
        if (pid > 0)
            continue;

        // check for errors
        if (pid == -1) {
            perror("fork");
            close(pipe_fds[0]);
            close(pipe_fds[1]);
            return -1;
        }

        // child code
        // close read end
        if (close(pipe_fds[0]) == -1) {
            perror("close");
            close(pipe_fds[1]);
            exit(1);
        }

        // process file and write
        if (process_file(argv[i], pipe_fds[1]) == -1) {
            close(pipe_fds[1]);
            exit(1);
        }

        // close write end
        if (close(pipe_fds[1]) == -1) {
            perror("close");
            exit(1);
        }

        exit(0);
    }

    // parent code
    // close write end of pipe
    if (close(pipe_fds[1]) == -1) {
        perror("close");
        return -1;
    }

    // TODO Aggregate all the results together by reading from the pipe in the parent
    int totals[MAX_WORD_LEN];
    memset(totals, 0, sizeof(int) * MAX_WORD_LEN);

    int temp[MAX_WORD_LEN];
    int nbytes;
    while ((nbytes = read(pipe_fds[0], temp, sizeof(int) * MAX_WORD_LEN)) > 0) {
        for (int i = 0; i < MAX_WORD_LEN; i++) {
            totals[i] += temp[i];
        }
    }
    
    // check for read errors
    if (nbytes == -1) {
        perror("read");
        close(pipe_fds[0]);
        return -1;
    }

    // close read end of pipe
    if (close(pipe_fds[0]) == -1) {
        perror("close");
        return -1;
    }

    // TODO Change this code to print out the total count of words of each length
    for (int i = 1; i <= MAX_WORD_LEN; i++) {
        printf("%d-Character Words: %d\n", i, totals[i-1]);
    }

    int ret_val = 0;
    for (int i = 1; i < argc; i++) {
        if (wait(NULL) == -1) {
            perror("wait");
            ret_val = -1;
        }
    }

    return ret_val;
}
