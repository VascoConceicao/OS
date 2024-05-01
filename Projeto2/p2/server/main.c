#include <limits.h>
#include <stdio.h>
#include <stdlib.h>

#include "common/constants.h"
#include "common/io.h"
#include "operations.h"

// Added //
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
#include <semaphore.h>
#include <pthread.h>
#include <signal.h>

sem_t sem_empty;
sem_t sem_full;
pthread_mutex_t mutexBuffer;
char buffer[MAX_SESSION_COUNT][81];
int waiting = 0, S = 0, signal_flag = 0;

void catch_sigusr1() {
    signal_flag = 1;
    signal(SIGUSR1, catch_sigusr1);
}

void block_sigusr1() {
    sigset_t set;
    sigemptyset(&set);
    sigaddset(&set, SIGUSR1);
    if (pthread_sigmask(SIG_BLOCK, &set, NULL) != 0) {
        perror("Failed to block SIGUSR1 for worker threads");
        exit(EXIT_FAILURE);
    }
}

void *worker() {
    block_sigusr1();
    int id = S++;
    while (1) {
        sem_wait(&sem_full);
        pthread_mutex_lock(&mutexBuffer);

        char pipes[81];
        strcpy(pipes, buffer[0]);
        for (int i = 1; i < waiting; i++)
            strcpy(buffer[i - 1], buffer[i]);
        waiting--;

        pthread_mutex_unlock(&mutexBuffer);
        sem_post(&sem_empty);

        char *req_pipe = strtok(pipes, "|");
        int req_pipe_fd = open(req_pipe, O_RDONLY);
        char *resp_pipe = strtok(NULL, "|");
        int resp_pipe_fd = open(resp_pipe, O_WRONLY);

        write(resp_pipe_fd, &id, sizeof(int));

        int ended = 0;
        while (!ended) {
            char OP_CODE[1];
            char read_session_id[sizeof(int)]; int session_id, res;
            char read_event_id[sizeof(unsigned int)]; unsigned int event_id;
            size_t num_rows, num_cols;

            read(req_pipe_fd, OP_CODE, 1);
            read_byte(req_pipe_fd);
            read(req_pipe_fd, &read_session_id, sizeof(int));
            memcpy(&session_id, read_session_id, sizeof(int));

            switch(OP_CODE[0]) {
                case '2':
                    close(req_pipe_fd);
                    close(resp_pipe_fd);
                    unlink(req_pipe);
                    unlink(resp_pipe);
                    ended = 1;
                    break;
                    
                case '3':
                    char read_num_rows[sizeof(size_t)];
                    char read_num_cols[sizeof(size_t)];

                    read_byte(req_pipe_fd);
                    read(req_pipe_fd, &read_event_id, sizeof(unsigned int));
                    read_byte(req_pipe_fd);
                    read(req_pipe_fd, &read_num_rows, sizeof(size_t));
                    read_byte(req_pipe_fd);
                    read(req_pipe_fd, &read_num_cols, sizeof(size_t));

                    memcpy(&event_id, read_event_id, sizeof(unsigned int));
                    memcpy(&num_rows, read_num_rows, sizeof(size_t));
                    memcpy(&num_cols, read_num_cols, sizeof(size_t));
                    
                    res = ems_create(event_id, num_rows, num_cols);
                    write(resp_pipe_fd, &res, sizeof(int));
                    break;

                case '5':
                    read_byte(req_pipe_fd);
                    read(req_pipe_fd, &read_event_id, sizeof(unsigned int));

                    memcpy(&event_id, read_event_id, sizeof(unsigned int));

                    unsigned int *seats;
                    res = ems_show(event_id, &num_rows, &num_cols, &seats);

                    write(resp_pipe_fd, &res, sizeof(int));
                    write(resp_pipe_fd, "|", 1);
                    write(resp_pipe_fd, &num_rows, sizeof(size_t));
                    write(resp_pipe_fd, "|", 1);
                    write(resp_pipe_fd, &num_cols, sizeof(size_t));
                    write(resp_pipe_fd, "|", 1);
                    write(resp_pipe_fd, seats, sizeof(unsigned int) * (num_rows * num_cols));

                    free(seats);
                    break;

                case '6':
                    read_byte(req_pipe_fd);

                    size_t num_events; unsigned int *ids;
                    res = ems_list_events(&num_events, &ids);

                    write(resp_pipe_fd, &res, sizeof(int));
                    write(resp_pipe_fd, "|", 1);
                    write(resp_pipe_fd, &num_events, sizeof(size_t));
                    write(resp_pipe_fd, "|", 1);
                    write(resp_pipe_fd, ids, sizeof(unsigned int) * num_events);

                    free(ids);
                    break;

                case '4':
                    char read_num_seats[sizeof(size_t)]; size_t num_seats;
                    read_byte(req_pipe_fd);
                    read(req_pipe_fd, &read_event_id, sizeof(unsigned int));
                    read_byte(req_pipe_fd);
                    read(req_pipe_fd, &read_num_seats, sizeof(size_t));

                    memcpy(&event_id, read_event_id, sizeof(unsigned int));
                    memcpy(&num_seats, read_num_seats, sizeof(size_t));
                    
                    size_t xs[num_seats]; size_t ys[num_seats];
                    read_byte(req_pipe_fd);
                    read(req_pipe_fd, xs, sizeof(size_t) * num_seats);
                    read_byte(req_pipe_fd);
                    read(req_pipe_fd, ys, sizeof(size_t) * num_seats);

                    res = ems_reserve(event_id, num_seats, xs, ys);
                    write(resp_pipe_fd, &res, sizeof(int));
                    break;

            }
        }
    }
}

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

    if (signal(SIGUSR1, catch_sigusr1) == SIG_ERR) {
        perror("Failed to register signal handler for SIGUSR1");
        return 1;
    }

    sem_init(&sem_empty, 0, MAX_SESSION_COUNT);
    sem_init(&sem_full, 0, 0);
    pthread_mutex_init(&mutexBuffer, NULL);
    pthread_t th[MAX_SESSION_COUNT];

    unlink(argv[1]);
    if (mkfifo(argv[1], 0604) < 0) return 1;    // Create server pipe
    int server_pipe_fd;

    for (int i = 0; i < MAX_SESSION_COUNT; i++)
        if (pthread_create(&th[i], NULL, &worker, NULL) != 0)
            perror("Failed to create thread");

    while (1) {
        if (signal_flag == 1) {
            signal_flag = 0;
            unsigned int *ids;
            size_t num_events;
            ems_list_events(&num_events, &ids);

            for (int k = 0; k < (int)num_events; k++) {
                printf("Event ID: %u\n", ids[k]);

                size_t num_rows, num_cols;
                unsigned int *seats;
                int res = ems_show(ids[k], &num_rows, &num_cols, &seats);

                if (res == 0) {
                    for (int i = 0; i < (int)num_rows; i++) {
                        for (int j = 0; j < (int)num_cols; j++) 
                            printf("%u ", seats[i * (int)num_cols + j]);
                        printf("\n");
                    }
                    free(seats);
                }
            }
            free(ids);
        }

        sem_wait(&sem_empty);

        char aux1[40], aux2[40];
        if ((server_pipe_fd = open(argv[1], O_RDONLY)) < 0) {
            if (signal_flag == 1) {
                sem_post(&sem_empty);
                continue;
            }
            return 1;
        }

        read_byte(server_pipe_fd);
        read_byte(server_pipe_fd);

        pthread_mutex_lock(&mutexBuffer);
        if (read(server_pipe_fd, aux1, 40) < 0) return 1;
        read_byte(server_pipe_fd);
        if (read(server_pipe_fd, aux2, 40) < 0) return 1;
        sprintf(buffer[waiting++],"%s|%s", aux1, aux2);
        pthread_mutex_unlock(&mutexBuffer);

        sem_post(&sem_full);
    }

    sem_destroy(&sem_empty);
    sem_destroy(&sem_full);
    pthread_mutex_destroy(&mutexBuffer);
    close(server_pipe_fd);
    return 0;
}