#include <limits.h>
#include <stdio.h>
#include <stdlib.h>

#include "common/constants.h"
#include "common/io.h"
#include "operations.h"

// Nossas //
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include "eventlist.h"

int main(int argc, char* argv[]) {
    if (argc < 2 || argc > 3) {
        fprintf(stderr, "Usage: %s\n <pipe_path> [delay]\n", argv[0]);
        return 1;
    }

    char* endptr;
    unsigned int state_access_delay_us = STATE_ACCESS_DELAY_US;
    if (argc == 3) {
        unsigned long int delay = strtoul(argv[2], &endptr, 10);

        if (*endptr != '\0' || delay > UINT_MAX) {
            fprintf(stderr, "Invalid delay value or value too large\n");
            return 1;
        }

        state_access_delay_us = (unsigned int)delay;
    }

    if (ems_init(state_access_delay_us)) {
        fprintf(stderr, "Failed to initialize EMS\n");
        return 1;
    }

    //TODO: Intialize server, create worker threads
    int server_pipe_fd, req_pipe_fd, resp_pipe_fd, s = 0;
    char req_pipes[MAX_SESSION_COUNT][40];
    char resp_pipes[MAX_SESSION_COUNT][40];
    printf("server_pipe_path -> %s\n", argv[1]);

    unlink(argv[1]);
    if (mkfifo(argv[1], 0604) < 0) return 1;    // Create server pipe
    if ((server_pipe_fd = open(argv[1], O_RDONLY)) < 0) return 1;

    char OP_CODE[1];
    if (read(server_pipe_fd, OP_CODE, 1) < 0) return 1;
    read_byte(server_pipe_fd);

    if (read(server_pipe_fd, req_pipes[s], 40) < 0) return 1;
    read_byte(server_pipe_fd);
    if (read(server_pipe_fd, resp_pipes[s], 40) < 0) return 1;
    
    if ((req_pipe_fd = open(req_pipes[s], O_RDONLY)) < 0) return 1;
    if ((resp_pipe_fd = open(resp_pipes[s], O_WRONLY)) < 0) return 1;
    
    if (write(resp_pipe_fd, &s, sizeof(int)) < 0) return 1;
    close(server_pipe_fd);
    unlink(argv[1]);

    int ended = 0;
    while (!ended) {
        //TODO: Read from pipe

        char read_session_id[sizeof(int)];
        char read_event_id[sizeof(unsigned int)]; unsigned int event_id;
        size_t num_rows, num_cols;
        int res;

        if (read(req_pipe_fd, OP_CODE, 1) < 0) return 1;
        read_byte(req_pipe_fd);
        if (read(req_pipe_fd, &read_session_id, sizeof(int)) < 0) return 1;

        switch(OP_CODE[0]) {
            case '2':
                close(req_pipe_fd);
                close(resp_pipe_fd);
                unlink(req_pipes[s]);
                unlink(resp_pipes[s]);
                ended = 1;
                break;
                
            case '3':
                char read_num_rows[sizeof(size_t)];
                char read_num_cols[sizeof(size_t)];

                read_byte(req_pipe_fd);
                if (read(req_pipe_fd, &read_event_id, sizeof(unsigned int)) < 0) return 1;
                read_byte(req_pipe_fd);
                if (read(req_pipe_fd, &read_num_rows, sizeof(size_t)) < 0) return 1;
                read_byte(req_pipe_fd);
                if (read(req_pipe_fd, &read_num_cols, sizeof(size_t)) < 0) return 1;

                memcpy(&event_id, read_event_id, sizeof(unsigned int));
                memcpy(&num_rows, read_num_rows, sizeof(size_t));
                memcpy(&num_cols, read_num_cols, sizeof(size_t));

                // printf("\nems_create\nevent_id -> %u\nnum_rows -> %ld\nnum_cols -> %ld\n", event_id, num_rows, num_cols);
                
                res = ems_create(event_id, num_rows, num_cols);
                if (write(resp_pipe_fd, &res, sizeof(int)) < 0) return 1;
                break;

            case '5':
                read_byte(req_pipe_fd);
                if (read(req_pipe_fd, &read_event_id, sizeof(unsigned int)) < 0) return 1;

                memcpy(&event_id, read_event_id, sizeof(unsigned int));

                unsigned int *seats;
                res = ems_show(event_id, &num_rows, &num_cols, &seats);

                // unsigned int seats1[num_rows * num_cols];
                // memcpy(seats1, seats, sizeof(unsigned int) * num_rows * num_cols);
                // printf("\nems_show\nevent_id -> %u\nnum_rows -> %ld\nnum_cols -> %ld\n", event_id, num_rows, num_cols);
                if (write(resp_pipe_fd, &res, sizeof(int)) < 0) return 1;
                if (write(resp_pipe_fd, "|", 1) < 0) return 1;
                if (write(resp_pipe_fd, &num_rows, sizeof(size_t)) < 0) return 1;
                if (write(resp_pipe_fd, "|", 1) < 0) return 1;
                if (write(resp_pipe_fd, &num_cols, sizeof(size_t)) < 0) return 1;
                if (write(resp_pipe_fd, "|", 1) < 0) return 1;
                if (write(resp_pipe_fd, seats, sizeof(unsigned int) * (num_rows * num_cols)) < 0) return 1;

                free(seats);
                break;

            case '6':
                read_byte(req_pipe_fd);

                size_t num_events; unsigned int *ids;
                res = ems_list_events(&num_events, &ids);

                if (write(resp_pipe_fd, &res, sizeof(int)) < 0) return 1;
                if (write(resp_pipe_fd, "|", 1) < 0) return 1;
                if (write(resp_pipe_fd, &num_events, sizeof(size_t)) < 0) return 1;
                if (write(resp_pipe_fd, "|", 1) < 0) return 1;
                if (write(resp_pipe_fd, ids, sizeof(unsigned int) * num_events) < 0) return 1;

                free(ids);
                break;

            case '4':
                char read_num_seats[sizeof(size_t)]; size_t num_seats;
                read_byte(req_pipe_fd);
                if (read(req_pipe_fd, &read_event_id, sizeof(unsigned int)) < 0) return 1;
                read_byte(req_pipe_fd);
                if (read(req_pipe_fd, &read_num_seats, sizeof(size_t)) < 0) return 1;

                memcpy(&event_id, read_event_id, sizeof(unsigned int));
                memcpy(&num_seats, read_num_seats, sizeof(size_t));
                
                size_t xs[num_seats]; size_t ys[num_seats];
                read_byte(req_pipe_fd);
                if (read(req_pipe_fd, xs, sizeof(size_t) * num_seats) < 0) return 1;
                read_byte(req_pipe_fd);
                if (read(req_pipe_fd, ys, sizeof(size_t) * num_seats) < 0) return 1;

                // printf("\nems_reserve\nevent_id -> %d\nnum_seats -> %ld\n", event_id, num_seats);
                // for (int i=0; i<(int)num_seats; i++) printf("xs[%d] -> %ld, ys[%d] -> %ld\n", i, xs[i], i, ys[i]);

                res = ems_reserve(event_id, num_seats, xs, ys);
                if (write(resp_pipe_fd, &res, sizeof(int)) < 0) return 1;
                break;
                

        }

        // printf("buf -> %s\n", req_pipes[0]);
        // printf("buf -> %s\n", resp_pipes[0]);

        //TODO: Write new client to the producer-consumer buffer
    }

    //TODO: Close Server

    ems_terminate();
}