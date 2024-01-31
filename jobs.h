#include "parser.h"

typedef struct job {
    // job id
    int id;

    // job status
    int status;

    // job process group id
    int pgid;

    int * pids;

    int pids_len;

    // job command line
    struct parsed_command *cmd;

    char * cmd_raw;

    // job next
    struct job *next;
} job;

// typedef struct job_list {
//     int job_count;
//     struct job *head;
// } job_list;

void create_job(char * cmd_raw, int * pids, int pids_len, int status);

void print_jobs();

void update_status(int pgid, int status);

void update_statuses();

struct job* get_job(int id);

void remove_job(int pgid);

void process_terminated(int pid);

// void print();

// void create_job_list();

// void push_job();