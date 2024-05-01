#include "api.h"

// Nossas //
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>
#include <stdio.h>
#include <math.h>
#include "common/constants.h"
#include "common/io.h"
#include <unistd.h>

int req_pipe_fd, resp_pipe_fd, server_pipe_fd, session_id;

int ems_setup(char const* req_pipe_path, char const* resp_pipe_path, char const* server_pipe_path) {
    //TODO: create pipes and connect to the server
    unlink(req_pipe_path);
    unlink(resp_pipe_path);
    printf("req_pipe_path -> %s\n",req_pipe_path);
    if (mkfifo(req_pipe_path, 0604) < 0)  // Cria request pipe
        return 1;
    if (mkfifo(resp_pipe_path, 0604) < 0) // Cria response pipe
        return 1;

    if ((server_pipe_fd = open(server_pipe_path, O_WRONLY)) < 0) return 1;

    if (write(server_pipe_fd, "1|", 2) < 0) return 1;
    char buf[40];
    strcpy(buf, req_pipe_path);
    if (write(server_pipe_fd, buf, 40) < 0) return 1;

    if (write(server_pipe_fd, "|", 1) < 0) return 1;
    strcpy(buf, resp_pipe_path);
    if (write(server_pipe_fd, buf, 40) < 0) return 1;

    if ((req_pipe_fd = open(req_pipe_path, O_WRONLY)) < 0) return 1;
    if ((resp_pipe_fd = open(resp_pipe_path, O_RDONLY)) < 0) return 1;

    char read_res[sizeof(int)]; int res;
    if (read(resp_pipe_fd, read_res, sizeof(int)) < 0) return 1;
    memcpy(&res, read_res, sizeof(int));
    return res;
}

int ems_quit(void) {
    //TODO: close pipes
    if (write(req_pipe_fd, "2|", 2) < 0) return 1;
    if (write(req_pipe_fd, &session_id, sizeof(int)) < 0) return 1;
    close(server_pipe_fd);
    close(req_pipe_fd);
    close(resp_pipe_fd);
    return 0;
}

int ems_create(unsigned int event_id, size_t num_rows, size_t num_cols) {
    //TODO: send create request to the server (through the request pipe) and wait for the response (through the response pipe)
    if (write(req_pipe_fd, "3|", 2) < 0) return 1;
    if (write(req_pipe_fd, &session_id, sizeof(int)) < 0) return 1;
    if (write(req_pipe_fd, "|", 1) < 0) return 1;
    if (write(req_pipe_fd, &event_id, sizeof(unsigned int)) < 0) return 1;
    if (write(req_pipe_fd, "|", 1) < 0) return 1;
    if (write(req_pipe_fd, &num_rows, sizeof(size_t)) < 0) return 1;
    if (write(req_pipe_fd, "|", 1) < 0) return 1;
    if (write(req_pipe_fd, &num_cols, sizeof(size_t)) < 0) return 1;

    char read_res[sizeof(int)]; int res;
    if (read(resp_pipe_fd, read_res, sizeof(int)) < 0) return 1;
    memcpy(&res, read_res, sizeof(int));
    return res;
}

int ems_reserve(unsigned int event_id, size_t num_seats, size_t* xs, size_t* ys) {
    //TODO: send reserve request to the server (through the request pipe) and wait for the response (through the response pipe)
    if (write(req_pipe_fd, "4|", 2) < 0) return 1;
    if (write(req_pipe_fd, &session_id, sizeof(int)) < 0) return 1;
    if (write(req_pipe_fd, "|", 1) < 0) return 1;
    if (write(req_pipe_fd, &event_id, sizeof(unsigned int)) < 0) return 1;
    if (write(req_pipe_fd, "|", 1) < 0) return 1;
    if (write(req_pipe_fd, &num_seats, sizeof(size_t)) < 0) return 1;
    if (write(req_pipe_fd, "|", 1) < 0) return 1;
    if (write(req_pipe_fd, xs, sizeof(size_t) * num_seats) < 0) return 1;
    if (write(req_pipe_fd, "|", 1) < 0) return 1;
    if (write(req_pipe_fd, ys, sizeof(size_t) * num_seats) < 0) return 1;

    // printf("\nems_reserve\nevent_id -> %d\nnum_seats -> %ld\n", event_id, num_seats);
    // for (int i=0; i<(int)num_seats; i++) printf("xs[%d] -> %ld, ys[%d] -> %ld\n", i, xs[i], i, ys[i]);

    char read_res[sizeof(int)]; int res;
    if (read(resp_pipe_fd, read_res, sizeof(int)) < 0) return 1;
    memcpy(&res, read_res, sizeof(int));
    return res;
}

int ems_show(int out_fd, unsigned int event_id) {
    //TODO: send show request to the server (through the request pipe) and wait for the response (through the response pipe)
    if (write(req_pipe_fd, "5|", 2) < 0) return 1;
    if (write(req_pipe_fd, &session_id, sizeof(int)) < 0) return 1;
    if (write(req_pipe_fd, "|", 1) < 0) return 1;
    if (write(req_pipe_fd, &event_id, sizeof(unsigned int)) < 0) return 1;

    char read_res[sizeof(int)]; int res;
    char read_num_rows[sizeof(size_t)]; size_t num_rows;
    char read_num_cols[sizeof(size_t)]; size_t num_cols;
    if (read(resp_pipe_fd, &read_res, sizeof(int)) < 0) return 1;
    read_byte(resp_pipe_fd);
    if (read(resp_pipe_fd, &read_num_rows, sizeof(size_t)) < 0) return 1;
    read_byte(resp_pipe_fd);
    if (read(resp_pipe_fd, &read_num_cols, sizeof(size_t)) < 0) return 1;
    
    memcpy(&res, read_res, sizeof(int));
    memcpy(&num_rows, read_num_rows, sizeof(size_t));
    memcpy(&num_cols, read_num_cols, sizeof(size_t));

    unsigned int seats[num_rows * num_cols];
    read_byte(resp_pipe_fd);
    if (read(resp_pipe_fd, seats, sizeof(unsigned int) * num_rows * num_cols) < 0) return 1;

    for (size_t i = 1; i <= num_rows; i++) {
        for (size_t j = 1; j <= num_cols; j++) {
            char buffer[16];
            sprintf(buffer, "%u", seats[(i - 1) * num_cols + j - 1]);
            if (print_str(out_fd, buffer)) return 1;
            if (j < num_cols)
                if (print_str(out_fd, " ")) return 1;
            }
            if (print_str(out_fd, "\n")) return 1;
    }
    return 0;
}

int ems_list_events(int out_fd) {
    //TODO: send list request to the server (through the request pipe) and wait for the response (through the response pipe)
    if (write(req_pipe_fd, "6|", 2) < 0) return 1;
    if (write(req_pipe_fd, &session_id, sizeof(int)) < 0) return 1;
    if (write(req_pipe_fd, "|", 1) < 0) return 1;

    char read_res[sizeof(int)]; int res;
    char read_num_events[sizeof(size_t)]; size_t num_events;
    if (read(resp_pipe_fd, &read_res, sizeof(int)) < 0) return 1;
    read_byte(resp_pipe_fd);
    if (read(resp_pipe_fd, &read_num_events, sizeof(size_t)) < 0) return 1;

    memcpy(&res, read_res, sizeof(int));
    memcpy(&num_events, read_num_events, sizeof(size_t));
    
    unsigned int ids[num_events];
    read_byte(resp_pipe_fd);
    if (read(resp_pipe_fd, ids, sizeof(unsigned int) * num_events) < 0) return 1;

    if (num_events == 0) {
        char buff[] = "No events\n";
        if (print_str(out_fd, buff)) return 1;
        return 0;
    }
    for (int i = 0; i < (int)num_events; i++) {
        char buff[] = "Event: ";
        if (print_str(out_fd, buff)) return 1;

        char id[16];
        sprintf(id, "%u\n", ids[i]);
        if (print_str(out_fd, id)) return 1;
    }

    return 0;
}
