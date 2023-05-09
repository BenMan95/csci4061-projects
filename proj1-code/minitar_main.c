#include <stdio.h>
#include <string.h>

#include "file_list.h"
#include "minitar.h"

int main(int argc, char **argv) {
    if (argc < 4) {
        printf("Usage: %s -c|a|t|u|x -f ARCHIVE [FILE...]\n", argv[0]);
        return 0;
    }

    file_list_t files;
    file_list_init(&files);

    // Parse command-line arguments and invoke functions from 'minitar.h'
    // to execute archive operations
    char* archive = argv[3];

    if (strcmp("-c", argv[1]) == 0) { // create archive

        for (int i = 4; i < argc; i++)
            file_list_add(&files, argv[i]);

        if (create_archive(archive, &files)) {
            file_list_clear(&files);
            return -1;
        }

    } else if (strcmp("-a", argv[1]) == 0) { // append to archive

        for (int i = 4; i < argc; i++)
            file_list_add(&files, argv[i]);

        if (append_files_to_archive(archive, &files)) {
            file_list_clear(&files);
            return -1;
        }

    } else if (strcmp("-t", argv[1]) == 0) { // list files in archive

        if (get_archive_file_list(archive, &files)) {
            file_list_clear(&files);
            return -1;
        }

        node_t* file = files.head;
        while (file) {
            printf("%s\n",file->name);
            file = file->next;
        }

    } else if (strcmp("-u", argv[1]) == 0) { // update files in archive

        file_list_t archived_files;
        file_list_init(&archived_files);

        if (get_archive_file_list(archive, &archived_files)) {
            file_list_clear(&archived_files);
            return -1;
        }

        for (int i = 4; i < argc; i++) {
            if (file_list_contains(&archived_files, argv[i]) == 0) {
                fprintf(stderr, "Error: One or more of the specified files is not already present in archive");
                file_list_clear(&archived_files);
                file_list_clear(&files);
                return -1;
            }
            file_list_add(&files, argv[i]);
        }

        if (append_files_to_archive(archive, &files)) {
            file_list_clear(&archived_files);
            file_list_clear(&files);
            return -1;
        }

        file_list_clear(&archived_files);

    } else if (strcmp("-x", argv[1]) == 0) { // extract files from archive

        if (extract_files_from_archive(archive))
            return -1;

    } else {
        printf("Usage: %s -c|a|t|u|x -f ARCHIVE [FILE...]\n", argv[0]);
    }

    file_list_clear(&files);
    return 0;
}
