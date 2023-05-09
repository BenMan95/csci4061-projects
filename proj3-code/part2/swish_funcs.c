#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include "string_vector.h"
#include "swish_funcs.h"

#define MAX_ARGS 10

/*
 * Helper function to run a single command within a pipeline. You should make
 * make use of the provided 'run_command' function here.
 * tokens: String vector containing the tokens representing the command to be
 * executed, possible redirection, and the command's arguments.
 * pipes: An array of pipe file descriptors.
 * n_pipes: Length of the 'pipes' array
 * in_idx: Index of the file descriptor in the array from which the program
 *         should read its input, or -1 if input should not be read from a pipe.
 * out_idx: Index of the file descriptor in the array to which the program
 *          should write its output, or -1 if output should not be written to
 *          a pipe.
 * Returns 0 on success or -1 on error.
 */
int run_piped_command(strvec_t *tokens, int *pipes, int n_pipes, int in_idx, int out_idx) {
    // TODO Complete this function's implementation
    // redirect input if necessary
    if (in_idx >= 0) {
        int result = dup2(pipes[in_idx], STDIN_FILENO);
        if (result == -1) {
            perror("dup2");
            return -1;
        }
    }

    // redirect output if necessary
    if (out_idx >= 0) {
        int result = dup2(pipes[out_idx], STDOUT_FILENO);
        if (result == -1) {
            perror("dup2");
            return -1;
        }
    }

    if (run_command(tokens))
        return -1;

    return 0;
}

int run_pipelined_commands(strvec_t *tokens) {
    // TODO Complete this function's implementation
    // allocate memory for pipes
    int n_pipes = strvec_num_occurrences(tokens, "|");
    int n_fds = n_pipes * 2;
    int *pipe_fds = malloc(sizeof(int) * n_fds);
    if (pipe_fds == NULL) {
        perror("malloc");
        return -1;
    }

    // create pipes
    for (int i = 0; i < n_fds; i += 2) {
        int result = pipe(pipe_fds + i);
        if (result == -1) {
            perror("pipe");
            for (int j = 0; j < i; j++)
                close(pipe_fds[j]);
            free(pipe_fds);
            return -1;
        }
    }

    // get tokens for last command
    strvec_t *tokens_left = tokens;
    strvec_t cmd_tokens;

    int pipe_idx = strvec_find_last(tokens_left, "|");
    strvec_slice(tokens_left, &cmd_tokens, pipe_idx+1, tokens_left->length);
    strvec_take(tokens_left, pipe_idx);

    // fork child for last command
    pid_t pid = fork();

    // error checking
    if (pid == -1) {
        perror("fork");
        for (int i = 0; i < n_fds; i++)
            close(pipe_fds[i]);
        free(pipe_fds);
        return -1;
    }

    // child code
    if (pid == 0) {
        // close unneeded pipes
        int in_idx = n_fds-2;
        int close_fail = 0;
        for (int i = 0; i < n_fds; i++) {
            if (i == in_idx)
                continue;
            if (close(pipe_fds[i])) {
                perror("close");
                close_fail = -1;
            }
        }

        // error checking
        if (close_fail) {
            close(pipe_fds[in_idx]);
            free(pipe_fds);
            strvec_clear(&cmd_tokens);
            exit(1);
        }

        // run command
        if (run_piped_command(&cmd_tokens, pipe_fds, n_fds, in_idx, -1) == -1) {
            close(pipe_fds[in_idx]);
            free(pipe_fds);
            strvec_clear(&cmd_tokens);
            exit(1);
        }

        // clean up
        if (close(pipe_fds[in_idx]) == -1) {
            perror("close");
            free(pipe_fds);
            strvec_clear(&cmd_tokens);
            exit(1);
        }
        free(pipe_fds);
        strvec_clear(&cmd_tokens);
        exit(0);
    }
    strvec_clear(&cmd_tokens);

    for (int i = n_fds-2; i >= 0; i -= 2) {
        // get tokens
        int pipe_idx = strvec_find_last(tokens_left, "|");
        strvec_slice(tokens_left, &cmd_tokens, pipe_idx+1, tokens_left->length);
        strvec_take(tokens_left, pipe_idx);

        pid = fork();

        // return to top of loop if parent
        if (pid > 0) {
            strvec_clear(&cmd_tokens);
            continue;
        }

        // error checking
        if (pid == -1) {
            perror("fork");
            for (int j = 0; j < n_fds; j++)
                close(pipe_fds[j]);
            free(pipe_fds);
            strvec_clear(&cmd_tokens);
            return -1;
        }
        
        // child code
        // close unnecessary pipes
        int in_idx = i-2;
        int out_idx = i+1;
        int close_fail = 0;
        for (int j = 0; j < n_fds; j++) {
            if (j == in_idx || j == out_idx)
                continue;
            if (close(pipe_fds[j]) == -1) {
                perror("close");
                close_fail = -1;
            }
        }

        // error checking
        if (close_fail) {
            close(pipe_fds[in_idx]);
            close(pipe_fds[out_idx]);
            free(pipe_fds);
            strvec_clear(&cmd_tokens);
            exit(1);
        }

        // run command
        if (run_piped_command(&cmd_tokens, pipe_fds, n_fds, in_idx, out_idx) == -1) {
            close(pipe_fds[in_idx]);
            close(pipe_fds[out_idx]);
            free(pipe_fds);
            strvec_clear(&cmd_tokens);
            exit(1);
        }

        // clean up
        if (close(pipe_fds[in_idx]) == -1) {
            perror("close");
            close(pipe_fds[out_idx]);
            free(pipe_fds);
            strvec_clear(&cmd_tokens);
            exit(1);
        }
        if (close(pipe_fds[in_idx]) == -1) {
            perror("close");
            free(pipe_fds);
            strvec_clear(&cmd_tokens);
            exit(1);
        }
        free(pipe_fds);
        strvec_clear(&cmd_tokens);
        exit(0);
    }

    // error checking
    int ret_val = 0;
    for (int j = 0; j < n_fds; j++) {
        if (close(pipe_fds[j]) == -1) {
            perror("close");
            ret_val = -1;
        }
    }

    for (int i = 0; i <= n_pipes; i++) {
        if (wait(NULL) == -1) {
            perror("wait");
            ret_val = -1;
        }
    }

    free(pipe_fds);
    return ret_val;
}
