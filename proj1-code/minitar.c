#include <fcntl.h>
#include <grp.h>
#include <math.h>
#include <pwd.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <sys/types.h>
#include <unistd.h>

#include "minitar.h"

#define NUM_TRAILING_BLOCKS 2
#define BLOCK_SIZE 512
#define MAX_MSG_LEN 512

/*
 * Helper function to compute the checksum of a tar header block
 * Performs a simple sum over all bytes in the header in accordance with POSIX
 * standard for tar file structure.
 */
void compute_checksum(tar_header *header) {
    // Have to initially set header's checksum to "all blanks"
    memset(header->chksum, ' ', 8);
    unsigned sum = 0;
    char *bytes = (char *)header;
    for (int i = 0; i < sizeof(tar_header); i++) {
        sum += bytes[i];
    }
    snprintf(header->chksum, 8, "%07o", sum);
}

/*
 * Populates a tar header block pointed to by 'header' with metadata about
 * the file identified by 'file_name'.
 * Returns 0 on success or -1 if an error occurs
 */
int fill_tar_header(tar_header *header, const char *file_name) {
    memset(header, 0, sizeof(tar_header));
    char err_msg[MAX_MSG_LEN];
    struct stat stat_buf;
    // stat is a system call to inspect file metadata
    if (stat(file_name, &stat_buf) != 0) {
        snprintf(err_msg, MAX_MSG_LEN, "Failed to stat file %s", file_name);
        perror(err_msg);
        return -1;
    }

    strncpy(header->name, file_name, 100); // Name of the file, null-terminated string
    snprintf(header->mode, 8, "%07o", stat_buf.st_mode & 07777); // Permissions for file, 0-padded octal

    snprintf(header->uid, 8, "%07o", stat_buf.st_uid); // Owner ID of the file, 0-padded octal
    struct passwd *pwd = getpwuid(stat_buf.st_uid); // Look up name corresponding to owner ID
    if (pwd == NULL) {
        snprintf(err_msg, MAX_MSG_LEN, "Failed to look up owner name of file %s", file_name);
        perror(err_msg);
        return -1;
    }
    strncpy(header->uname, pwd->pw_name, 32); // Owner  name of the file, null-terminated string

    snprintf(header->gid, 8, "%07o", stat_buf.st_gid); // Group ID of the file, 0-padded octal
    struct group *grp = getgrgid(stat_buf.st_gid); // Look up name corresponding to group ID
    if (grp == NULL) {
        snprintf(err_msg, MAX_MSG_LEN, "Failed to look up group name of file %s", file_name);
        perror(err_msg);
        return -1;
    }
    strncpy(header->gname, grp->gr_name, 32); // Group name of the file, null-terminated string

    snprintf(header->size, 12, "%011o", (unsigned)stat_buf.st_size); // File size, 0-padded octal
    snprintf(header->mtime, 12, "%011o", (unsigned)stat_buf.st_mtime); // Modification time, 0-padded octal
    header->typeflag = REGTYPE; // File type, always regular file in this project
    strncpy(header->magic, MAGIC, 6); // Special, standardized sequence of bytes
    memcpy(header->version, "00", 2); // A bit weird, sidesteps null termination
    snprintf(header->devmajor, 8, "%07o", major(stat_buf.st_dev)); // Major device number, 0-padded octal
    snprintf(header->devminor, 8, "%07o", minor(stat_buf.st_dev)); // Minor device number, 0-padded octal

    compute_checksum(header);
    return 0;
}

/*
 * Removes 'nbytes' bytes from the file identified by 'file_name'
 * Returns 0 upon success, -1 upon error
 * Note: This function uses lower-level I/O syscalls (not stdio), which we'll learn about later
 */
int remove_trailing_bytes(const char *file_name, size_t nbytes) {
    char err_msg[MAX_MSG_LEN];
    // Note: ftruncate does not work with O_APPEND
    int fd = open(file_name, O_WRONLY);
    if (fd == -1) {
        snprintf(err_msg, MAX_MSG_LEN, "Failed to open file %s", file_name);
        perror(err_msg);
        return -1;
    }
    //  Seek to end of file - nbytes
    off_t current_pos = lseek(fd, -1 * nbytes, SEEK_END);
    if (current_pos == -1) {
        snprintf(err_msg, MAX_MSG_LEN, "Failed to seek in file %s", file_name);
        perror(err_msg);
        close(fd);
        return -1;
    }
    // Remove all contents of file past current position
    if (ftruncate(fd, current_pos) == -1) {
        snprintf(err_msg, MAX_MSG_LEN, "Failed to truncate file %s", file_name);
        perror(err_msg);
        close(fd);
        return -1;
    }
    if (close(fd) == -1) {
        snprintf(err_msg, MAX_MSG_LEN, "Failed to close file %s", file_name);
        perror(err_msg);
        return -1;
    }
    return 0;
}

int create_archive(const char *archive_name, const file_list_t *files) {
    char err_msg[MAX_MSG_LEN];

    // open archive file
    FILE *archive_fh = fopen(archive_name, "w");
    if (archive_fh == NULL) {
        snprintf(err_msg, MAX_MSG_LEN, "Failed to open file %s", archive_name);
        perror(err_msg);
        return -1;
    }

    // write files to archive
    char buffer[BLOCK_SIZE];
    size_t bytes;
    node_t *file = files->head;
    while (file) {

        // create header
        tar_header header;
        if (fill_tar_header(&header, file->name)) {
            fclose(archive_fh);
            return -1;
        }

        // write header
        if (fwrite(&header, 1, BLOCK_SIZE, archive_fh) != BLOCK_SIZE) {
            snprintf(err_msg, MAX_MSG_LEN, "Failed to write to file %s", archive_name);
            perror(err_msg);
            fclose(archive_fh);
            return -1;
        }

        // open file
        FILE *fh = fopen(file->name, "r");
        if (fh == NULL) {
            snprintf(err_msg, MAX_MSG_LEN, "Failed to open file %s", file->name);
            perror(err_msg);
            fclose(archive_fh);
            return -1;
        }

        // write file contents
        do {
            // read block
            bytes = fread(buffer, 1, BLOCK_SIZE, fh);
            if (ferror(fh)) {
                snprintf(err_msg, MAX_MSG_LEN, "Failed to read from file %s", file->name);
                perror(err_msg);
                fclose(archive_fh);
                fclose(fh);
                return -1;
            }

            if (bytes == 0)
                break;

            // fill in zeroes if needed
            for (int i = bytes; i < BLOCK_SIZE; i++)
                buffer[i] = 0;

            // write block
            if (fwrite(buffer, 1, BLOCK_SIZE, archive_fh) != BLOCK_SIZE) {
                snprintf(err_msg, MAX_MSG_LEN, "Failed to write to file %s", archive_name);
                perror(err_msg);
                fclose(archive_fh);
                fclose(fh);
                return -1;
            }
        } while (bytes == BLOCK_SIZE);

        fclose(fh);
        file = file->next;
    }

    // write footer blocks
    for (int i = 0; i < BLOCK_SIZE; i++)
        buffer[i] = 0;

    for (int i = 0; i < NUM_TRAILING_BLOCKS; i++) {
        if (fwrite(buffer, 1, BLOCK_SIZE, archive_fh) != BLOCK_SIZE) {
            snprintf(err_msg, MAX_MSG_LEN, "Failed to write to file %s", archive_name);
            perror(err_msg);
            fclose(archive_fh);
            return -1;
        }
    }

    fclose(archive_fh);
    return 0;
}

int append_files_to_archive(const char *archive_name, const file_list_t *files) {
    char err_msg[MAX_MSG_LEN];

    // remove footer blocks
    if (remove_trailing_bytes(archive_name, BLOCK_SIZE * NUM_TRAILING_BLOCKS))
        return -1;

    // open archive
    FILE *archive_fh = fopen(archive_name, "a");
    if (archive_fh == NULL) {
        snprintf(err_msg, MAX_MSG_LEN, "Failed to open file %s", archive_name);
        perror(err_msg);
        return -1;
    }

    // seek to end of file
    if (fseek(archive_fh, 0, SEEK_END)) {
        snprintf(err_msg, MAX_MSG_LEN, "Error reading from file %s", archive_name);
        perror(err_msg);
        fclose(archive_fh);
        return -1;
    }


    // write files to archive
    char buffer[BLOCK_SIZE];
    size_t bytes;
    node_t *file = files->head;
    while (file) {

        // create header
        tar_header header;
        if (fill_tar_header(&header, file->name)) {
            fclose(archive_fh);
            return -1;
        }

        // write header
        if (fwrite(&header, 1, BLOCK_SIZE, archive_fh) != BLOCK_SIZE) {
            snprintf(err_msg, MAX_MSG_LEN, "Failed to write to file %s", archive_name);
            perror(err_msg);
            fclose(archive_fh);
            return -1;
        }

        // open file
        FILE *fh = fopen(file->name, "r");
        if (fh == NULL) {
            snprintf(err_msg, MAX_MSG_LEN, "Failed to open file %s", file->name);
            perror(err_msg);
            fclose(archive_fh);
            return -1;
        }

        // write file contents
        do {
            // read block
            bytes = fread(buffer, 1, BLOCK_SIZE, fh);
            if (ferror(fh)) {
                snprintf(err_msg, MAX_MSG_LEN, "Failed to read from file %s", file->name);
                perror(err_msg);
                fclose(archive_fh);
                fclose(fh);
                return -1;
            }

            if (bytes == 0)
                break;

            // fill in zeroes if needed
            for (int i = bytes; i < BLOCK_SIZE; i++)
                buffer[i] = 0;

            // write block
            if (fwrite(buffer, 1, BLOCK_SIZE, archive_fh) != BLOCK_SIZE) {
                snprintf(err_msg, MAX_MSG_LEN, "Failed to write to file %s", archive_name);
                perror(err_msg);
                fclose(archive_fh);
                fclose(fh);
                return -1;
            }
        } while (bytes == BLOCK_SIZE);

        fclose(fh);
        file = file->next;
    }

    // write footer blocks
    for (int i = 0; i < BLOCK_SIZE; i++)
        buffer[i] = 0;

    for (int i = 0; i < NUM_TRAILING_BLOCKS; i++) {
        if (fwrite(buffer, 1, BLOCK_SIZE, archive_fh) != BLOCK_SIZE) {
            snprintf(err_msg, MAX_MSG_LEN, "Failed to write to file %s", archive_name);
            perror(err_msg);
            fclose(archive_fh);
            return -1;
        }
    }

    fclose(archive_fh);
    return 0;
}

int get_archive_file_list(const char *archive_name, file_list_t *files) {
    char err_msg[MAX_MSG_LEN];

    // open archive file
    FILE *fh = fopen(archive_name, "r");
    if (fh == NULL) {
        snprintf(err_msg, MAX_MSG_LEN, "Failed to open file %s", archive_name);
        perror(err_msg);
        return -1;
    }

    int size;
    tar_header header;
    while (1) {
        // read header block
        int bytes = fread(&header, 1, BLOCK_SIZE, fh);
        if (bytes < BLOCK_SIZE) {
            snprintf(err_msg, MAX_MSG_LEN, "Failed to read from file %s", archive_name);
            perror(err_msg);
            fclose(fh);
            return -1;
        }

        if (header.name[0] == '\0')
            break;

        // read name from header
        if (header.name != NULL) {
            int result = file_list_add(files, header.name);
            if (result) {
                fprintf(stderr, "Error reading file %s\n", archive_name);
                fclose(fh);
                return result;
            }
        }

        // read size from header
        size = 0;
        for (int i = 0; i < 11; i++) {
            size *= 8;
            size += header.size[i] - '0';
        }
        int seek_amt = (size + BLOCK_SIZE - 1) / BLOCK_SIZE * BLOCK_SIZE;

        // seek to next block
        if (fseek(fh, seek_amt, SEEK_CUR)) {
            snprintf(err_msg, MAX_MSG_LEN, "Error reading from file %s", archive_name);
            perror(err_msg);
            fclose(fh);
            return -1;
        }
    }

    fclose(fh);
    return 0;
}

int extract_files_from_archive(const char *archive_name) {
    char err_msg[MAX_MSG_LEN];

    // open archive file
    FILE *archive_fh = fopen(archive_name, "r");
    if (archive_fh == NULL) {
        snprintf(err_msg, MAX_MSG_LEN, "Failed to open file %s", archive_name);
        perror(err_msg);
        return -1;
    }

    int size;
    tar_header header;
    while (1) {
        // read header block
        int bytes = fread(&header, 1, BLOCK_SIZE, archive_fh);
        if (bytes < BLOCK_SIZE) {
            snprintf(err_msg, MAX_MSG_LEN, "Failed to read from file %s", archive_name);
            perror(err_msg);
            fclose(archive_fh);
            return -1;
        }

        if (header.size[0] == 0)
            break;

        // read size from header
        size = 0;
        for (int i = 0; i < 11; i++) {
            size *= 8;
            size += header.size[i] - '0';
        }

        // open file for writing
        FILE* fh = fopen(header.name, "w");
        if (fh == NULL) {
            snprintf(err_msg, MAX_MSG_LEN, "Failed to open file %s", header.name);
            perror(err_msg);
            fclose(archive_fh);
            return -1;
        }

        // read file from archive
        char buffer[BLOCK_SIZE];
        while (size) {
            int num_bytes = size < BLOCK_SIZE ? size : BLOCK_SIZE;

            // load block from archive
            fread(buffer, 1, BLOCK_SIZE, archive_fh);
            if (ferror(archive_fh)) {
                snprintf(err_msg, MAX_MSG_LEN, "Failed to read from file %s", archive_name);
                perror(err_msg);
                fclose(archive_fh);
                fclose(fh);
                return -1;
            }

            // write block to file
            if (fwrite(buffer, 1, num_bytes, fh) != num_bytes) {
                snprintf(err_msg, MAX_MSG_LEN, "Failed to write to file %s", header.name);
                perror(err_msg);
                fclose(archive_fh);
                fclose(fh);
                return -1;
            }

            size -= num_bytes;
        }
    }

    fclose(archive_fh);
    return 0;
}
