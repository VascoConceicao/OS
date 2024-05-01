#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <dirent.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/wait.h>

#include "constants.h"
#include "operations.h"
#include "parser.h"

int main(int argc, char *argv[]) {
    unsigned int state_access_delay_ms = STATE_ACCESS_DELAY_MS;

    if (argc > 3) {
        char *endptr;
        unsigned long int delay = strtoul(argv[3], &endptr, 10);

        if (*endptr != '\0' || delay > UINT_MAX) {
            fprintf(stderr, "Invalid delay value or value too large\n");
            return 1;
        }

        state_access_delay_ms = (unsigned int)delay;
        exit(EXIT_FAILURE);
    }

    int MAX_PROC = atoi(argv[2]), active_processes = 0;

    DIR *d = opendir(argv[1]);
    if (d == NULL) {
        perror("Error opening directory");
        return 1;
    }

    struct dirent *entry;
    if (d) {
        int count = 0;
        while ((entry = readdir(d)) != NULL) {
            
            while (active_processes >= MAX_PROC){
                wait(NULL);
                active_processes --;
            }

            if (hasExtension(entry->d_name, "jobs") == 0)
                continue;
        
            pid_t pid = fork();
            printf("count -> %d\n", count++);

            if (pid == -1){
                perror("ERROR in fork!");
                exit(EXIT_FAILURE);
            }

            else if (pid == 0){

                if (ems_init(state_access_delay_ms)) {
                    fprintf(stderr, "Failed to initialize EMS\n");
                    return 1;
                }
        
                char file_read_name[strlen(argv[1]) + 1 + strlen(entry->d_name) + 1];
                snprintf(file_read_name, sizeof(file_read_name), "%s/%s", argv[1], entry->d_name);
                int fd = open(file_read_name, O_RDONLY);
                if (fd < 0) {
                    fprintf(stderr, "open error: %s\n", strerror(errno));
                    return -1;
                }

                char *str = strtok(entry->d_name, ".");
                strcat(str, ".result");
                char file_write_name[strlen(argv[1]) + 1 + strlen(str) + 1];
                snprintf(file_write_name, sizeof(file_write_name), "%s/%s", argv[1], str);
                printf("File write name -> %s\n",file_write_name);
                int fw = open(file_write_name, O_CREAT | O_TRUNC | O_WRONLY , S_IRUSR | S_IWUSR);
                if (fw < 0){
                    fprintf(stderr, "open error: %s\n", strerror(errno));
                    return -1;
                }

                unsigned int event_id, delay, status = 1;
                size_t num_rows, num_columns, num_coords;
                size_t xs[MAX_RESERVATION_SIZE], ys[MAX_RESERVATION_SIZE];

                fflush(stdout);

                while (status) {
                    switch (get_next(fd)) {
                        case CMD_CREATE:
                            if (parse_create(fd, &event_id, &num_rows, &num_columns) != 0) {
                                fprintf(stderr, "Invalid command. See HELP for usage\n");
                                continue;
                            }

                            if (ems_create(event_id, num_rows, num_columns)) {
                                fprintf(stderr, "Failed to create event\n");
                            }

                            break;

                        case CMD_RESERVE:
                            num_coords = parse_reserve(fd, MAX_RESERVATION_SIZE, &event_id, xs, ys);

                            if (num_coords == 0) {
                                fprintf(stderr, "Invalid command. See HELP for usage\n");
                                continue;
                            }

                            if (ems_reserve(event_id, num_coords, xs, ys)) {
                                fprintf(stderr, "Failed to reserve seats\n");
                            }

                            break;

                        case CMD_SHOW:
                            if (parse_show(fd, &event_id) != 0) {
                                fprintf(stderr, "Invalid command. See HELP for usage\n");
                                continue;
                            }

                            if (ems_show(fw, event_id)) {
                                fprintf(stderr, "Failed to show event\n");
                            }

                            break;

                        case CMD_LIST_EVENTS:
                            if (ems_list_events(fw)) {
                                fprintf(stderr, "Failed to list events\n");
                            }

                            break;

                        case CMD_WAIT:
                            if (parse_wait(fd, &delay, NULL) == -1) {    // thread_id is not implemented
                                fprintf(stderr, "Invalid command. See HELP for usage\n");
                                continue;
                            }

                            if (delay > 0) {
                                printf("Waiting...\n");
                                ems_wait(delay);
                            }

                            break;

                        case CMD_INVALID:
                            fprintf(stderr, "Invalid command. See HELP for usage\n");
                            break;

                        case CMD_HELP:
                            printf(
                                    "Available commands:\n"
                                    "    CREATE <event_id> <num_rows> <num_columns>\n"
                                    "    RESERVE <event_id> [(<x1>,<y1>) (<x2>,<y2>) ...]\n"
                                    "    SHOW <event_id>\n"
                                    "    LIST\n"
                                    "    WAIT <delay_ms> [thread_id]\n"    // thread_id is not implemented
                                    "    BARRIER\n"                                            // Not implemented
                                    "    HELP\n");

                            break;

                        case CMD_BARRIER:    // Not implemented
                        case CMD_EMPTY:
                            printf("empty\n");
                            break;

                        case EOC:
                            status = 0;
                            break;
                    }
                }
                ems_terminate();
                close(fd);
                close(fw);
                exit(EXIT_SUCCESS);
            }
            else
                active_processes ++;
            
        }
        closedir(d);
    }

    while (active_processes > 0) {
        wait(NULL);
        active_processes --;
    }

    return 0;
}
