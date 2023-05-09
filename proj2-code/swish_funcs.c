#include <assert.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include "job_list.h"
#include "string_vector.h"
#include "swish_funcs.h"

#define MAX_ARGS 10

int tokenize(char *s, strvec_t *tokens) {
    // TODO Task 0: Tokenize string s
    // Assume each token is separated by a single space (" ")
    // Use the strtok() function to accomplish this
    // Add each token to the 'tokens' parameter (a string vector)
    // Return 0 on success, -1 on error

    // tokenize string s
    char *token = strtok(s, " ");
    // iterate through each token and add to the string vector 'tokens'
    while (token) {
        if (strvec_add(tokens, token) != 0) {// add token to the string vector
            printf("strvec_add: Error adding element"); // error message if add failed
            return -1;
        }

        token = strtok(NULL, " ");// get the next token
    }
    return 0;
}

int run_command(strvec_t *tokens) {
    // TODO Task 2: Execute the specified program (token 0) with the
    // specified command-line arguments
    // THIS FUNCTION SHOULD BE CALLED FROM A CHILD OF THE MAIN SHELL PROCESS
    // Hint: Build a string array from the 'tokens' vector and pass this into execvp()
    // Another Hint: You have a guarantee of the longest possible needed array, so you
    // won't have to use malloc.


    // check if the number of tokens exceeds the maximum allowed number of arguments
    int num_tok = tokens->length;
    if (num_tok > MAX_ARGS) {
        printf("Too many arguments");
        return -1;
    }
    // iterate through each token and add to a char buffer 'tok_buf'
    char *tok_buf[MAX_ARGS + 1];
    for (int i = 0; i < num_tok; i++) {
        tok_buf[i] = strvec_get(tokens, i);
        // check if getting token from the string vector was successful
        if (tok_buf[i] == NULL) {
            printf("strvec_get: Error getting element");
            return -1;
        }
    }
    tok_buf[num_tok] = NULL;

    // TODO Task 3: Extend this function to perform output redirection before exec()'ing
    // Check for '<' (redirect input), '>' (redirect output), '>>' (redirect and append output)
    // entries inside of 'tokens' (the strvec_find() function will do this for you)
    // Open the necessary file for reading (<), writing (>), or appending (>>)
    // Use dup2() to redirect stdin (<), stdout (> or >>)
    // DO NOT pass redirection operators and file names to exec()'d program
    // E.g., "ls -l > out.txt" should be exec()'d with strings "ls", "-l", NULL

    char *path_in = NULL;
    char *path_out = NULL;
    int mode_out;

    // iterate through each token in the buffer to check for input/output redirection symbols
    for (int i = 0; i < num_tok; i++) {
        if (strcmp(tok_buf[i], "<") == 0) {
            tok_buf[i] = NULL; // prevents subsequent arguments from being passed to exec
            path_in = tok_buf[i+1];
        } else if (strcmp(tok_buf[i], ">") == 0) {
            tok_buf[i] = NULL;
            path_out = tok_buf[i+1];
            mode_out = O_TRUNC;// set the output mode to truncate
        } else if (strcmp(tok_buf[i], ">>") == 0) {
            tok_buf[i] = NULL;
            path_out = tok_buf[i+1];
            mode_out = O_APPEND;
        }
    }
    // if input file path exists, open it and redirect stdin to it
    if (path_in) {
        int fd = open(path_in, O_RDONLY);
        if (fd < 0) {// check if opening file was successful
            perror("Failed to open input file");
            return -1;
        }
        if (dup2(fd, STDIN_FILENO) < 0) {
            perror("dup2");
            return -1;
        }
    }
    // if output file path exists, open it and redirect stdout to it
    if (path_out) {
        int fd = open(path_out, O_CREAT|O_WRONLY|mode_out, S_IRUSR|S_IWUSR);
        if (fd < 0) {
            perror("Failed to open output file");
            return -1;
        }
        if (dup2(fd, STDOUT_FILENO) < 0) { // redirect stdout to the file
            perror("dup2");
            return -1;
        }
    }


    // TODO Task 4: You need to do two items of setup before exec()'ing
    // 1. Restore the signal handlers for SIGTTOU and SIGTTIN to their defaults.
    // The code in main() within swish.c sets these handlers to the SIG_IGN value.
    // Adapt this code to use sigaction() to set the handlers to the SIG_DFL value.
    // 2. Change the process group of this process (a child of the main shell).
    // Call getpid() to get its process ID then call setpgid() and use this process
    // ID as the value for the new process group ID

    struct sigaction sac;
    sac.sa_handler = SIG_DFL;
    if (sigfillset(&sac.sa_mask) == -1) { // Add all signals to the signal mask
        perror("sigfillset");
        return 1;
    }
    sac.sa_flags = 0;
    if (sigaction(SIGTTIN, &sac, NULL) == -1 || sigaction(SIGTTOU, &sac, NULL) == -1) {
        perror("sigaction");// Register the signal handler for both signals
        return 1;
    }
    // Create a new process group for the child process
    pid_t pid = getpid();
    if (setpgid(pid, pid) != 0) {
        perror("setpgid");
        return -1;
    }
    // Execute the command in the child process
    if (execvp(tok_buf[0], tok_buf) != 0) {
        perror("exec");
        return -1;
    }

    // Not reachable after a successful exec(), but retain here to keep compiler happy
    return 0;
}

int resume_job(strvec_t *tokens, job_list_t *jobs, int is_foreground) {
    // TODO Task 5: Implement the ability to resume stopped jobs in the foreground
    // 1. Look up the relevant job information (in a job_t) from the jobs list
    //    using the index supplied by the user (in tokens index 1)
    //    Feel free to use sscanf() or atoi() to convert this string to an int
    // 2. Call tcsetpgrp(STDIN_FILENO, <job_pid>) where job_pid is the job's process ID
    // 3. Send the process the SIGCONT signal with the kill() system call
    // 4. Use the same waitpid() logic as in main -- dont' forget WUNTRACED
    // 5. If the job has terminated (not stopped), remove it from the 'jobs' list
    // 6. Call tcsetpgrp(STDIN_FILENO, <shell_pid>). shell_pid is the *current*
    //    process's pid, since we call this function from the main shell process



    if (tokens == NULL || jobs == NULL) {
        // if tokens or jobs is NULL, print error message and return -1
        fprintf(stderr, "NULL pointer error\n");
        return -1;
    }
    int job_index = atoi(strvec_get(tokens, 1));// get job index

    if (job_index >= jobs->length) {
        // if the job index is out of bounds
        fprintf(stderr, "Job index out of bounds\n");
        return -1;
    }
    // get the job from the job list
    job_t *job = job_list_get(jobs, job_index);

    if (job == NULL) {
        fprintf(stderr, "Job not found\n");
        return -1;
    }

    if (is_foreground) {
        if (tcsetpgrp(STDIN_FILENO, job->pid) == -1) { // set the process group of the job to the foreground process group
            perror("tcsetpgrp"); // if there is an error, print error message and return -1
            return -1;
        }
        if (kill(job->pid, SIGCONT) == -1) { // send SIGCONT signal to the job process to resume it
            perror("kill");
            return -1;
        }
        int status; // child status
        if (waitpid(job->pid, &status, WUNTRACED) == -1) { // wait for the job to finish
            perror("waitpid");
            return -1;
        }
        if (WIFEXITED(status) || WIFSIGNALED(status)) { // if the job has exited or has been terminated by a signal
            if (job_list_remove(jobs, job_index) == -1) { // remove the job from the job list
                perror("job_list_remove");
                return -1;
            }
        }
        if (tcsetpgrp(STDIN_FILENO, getpid()) == -1) {
            perror("tcsetpgrp");
            return -1;
        }
    } else { // if the job is a background job
        if (kill(job->pid, SIGCONT) == -1) { // send SIGCONT signal to the job process to resume it
            perror("kill");
            return -1;
        }
        job->status = JOB_BACKGROUND;
    }


    // TODO Task 6: Implement the ability to resume stopped jobs in the background.
    // This really just means omitting some of the steps used to resume a job in the foreground:
    // 1. DO NOT call tcsetpgrp() to manipulate foreground/background terminal process group
    // 2. DO NOT call waitpid() to wait on the job
    // 3. Make sure to modify the 'status' field of the relevant job list entry to JOB_BACKGROUND
    //    (as it was JOB_STOPPED before this)
    return 0;
}

int await_background_job(strvec_t *tokens, job_list_t *jobs) {
    // TODO Task 6: Wait for a specific job to stop or terminate
    // 1. Look up the relevant job information (in a job_t) from the jobs list
    //    using the index supplied by the user (in tokens index 1)
    // 2. Make sure the job's status is JOB_BACKGROUND (no sense waiting for a stopped job)
    // 3. Use waitpid() to wait for the job to terminate, as you have in resume_job() and main().
    // 4. If the process terminates (is not stopped by a signal) remove it from the jobs list
    int index = atoi(strvec_get(tokens, 1));
    int status;
    job_t *job;

    // Check if the index is within the bounds
    if((job = job_list_get(jobs, index)) == NULL) {
        fprintf(stderr, "Job index out of bound\n");
        return -1;
    }
    // Make sure the job is a background job, if not print error message and return -1
    if (job->status != JOB_BACKGROUND) {
        fprintf(stderr, "Job index is for stopped process not background process\n");
        return -1;
    }

    // Wait for the job to terminate and  set the status
    if((job->pid = waitpid(job->pid, &status, WUNTRACED)) == -1) {
        perror("waitpid");
        return -1;
    }
    // check status index
    if (WIFEXITED(status) || WIFSIGNALED(status)) {
        if (job_list_remove(jobs, index) == -1) {
            perror("job_list_remove");
            return -1;
        }
    }

    return 0;
}

int await_all_background_jobs(job_list_t *jobs) {
    // TODO Task 6: Wait for all background jobs to stop or terminate
    // 1. Iterate through the jobs list, ignoring any stopped jobs
    // 2. For a background job, call waitpid() with WUNTRACED.
    // 3. If the job has stopped (check with WIFSTOPPED), change its
    //    status to JOB_STOPPED. If the job has terminated, do nothing until the
    //    next step (don't attempt to remove it while iterating through the list).
    // 4. Remove all background jobs (which have all just terminated) from jobs list.
    //    Use the job_list_remove_by_status() function.

    int status;
    job_t *current = jobs->head;// pointer to head of job list
    // loop through each job in the list
    while (current != NULL) {
        if (current->status == JOB_BACKGROUND) {
            if (waitpid(current->pid, &status, WUNTRACED) == -1) {
                perror("waitpid");
                return -1;
            }
            if (WIFSTOPPED(status)) {// if job is stopped
                current->status = JOB_STOPPED;// change status to JOB_STOPPED
            }
        }
        current = current->next;
    }


    // remove jobs from the list
    job_list_remove_by_status(jobs, JOB_BACKGROUND);

    return 0;
}
