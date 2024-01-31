#include "parser.h"
#include "jobs.h"
#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <sys/wait.h>


struct job* head = NULL;
int job_count = 1;

char* status_to_str(int status) {
    if (status == 0) {
        return "stopped";
    } else if (status == 1) {
        return "running";
    } else if (status == 2) {
        return "restarting";
    } else if (status == 3) {
        return "finished";
    } else {
        return "";
    }
}

void print_job_update( struct job* j) {
    char* status = status_to_str(j->status);
    printf("%s: %s\n",status,j->cmd_raw);
}

void remove_trailing(char* cmd_raw) {
    for(int i = 0; i < strlen(cmd_raw); i++) {
        if (cmd_raw[i] == '&' || cmd_raw[i] == '\n') {
            cmd_raw[i] = '\0';
            return;
        }
    }
}

void create_job(char* cmd_raw, int * pids, int pids_len, int status) {
    struct job* j = (job*) malloc(sizeof(job));
    j -> id = job_count++;
    remove_trailing(cmd_raw);
    j -> cmd_raw = cmd_raw;
    j -> status = status;
    j -> pgid = pids[0];
    j -> pids = pids;
    j -> pids_len = pids_len;
    j -> next = NULL;

    if (head == NULL) {
        head = j;
    } else {
        job* current = head;
        while(current -> next != NULL) {
            current = current -> next;
        }
        current -> next = j;
    }

    print_job_update(j);
}

void print_jobs() {
    job * current = head;
    while (current != NULL) {
        char* status = status_to_str(current->status);
        printf("[%d] %s (%s)\n", current -> id, current->cmd_raw,status);
        current = current -> next;
    }
    return;
}

// get pgid of job given id, id = -1 means get most recently added
struct job* get_job(int id) {
    if (head == NULL) {
        perror("job doesn't exist");
        return NULL;
    }

    struct job* current = head;

    if (id == -1) {
        job* last_stopped = NULL;
        while (current -> next != NULL) {
            if (current -> status == 0) {
                last_stopped = current;
            }
            current = current ->next;
        }
        if (last_stopped != NULL) {
            return last_stopped;
        } else {
            return current;
        }
    } else {
        while (current != NULL) {
            if (current -> id == id) {
                return current;
            }
            current = current ->next;
        }

        perror("job doesn't exist");
        return NULL;

    }


}

void remove_job(int pgid) {
    struct job* current = head;

    if (current -> pgid == pgid) {
        head = current-> next;
        free(current -> cmd_raw);
        free(current -> pids);
        free(current);
        return;
    }

    while (current != NULL) {
        if (current -> next -> pgid == pgid) {
            struct job* to_delete = current -> next;
            current -> next = to_delete -> next;
            free(to_delete->cmd_raw);
            free(to_delete->pids);
            free(to_delete);
            return;
        }
        current = current -> next;
    }

}

void update_status(int pgid, int status) {
        //throw error if pgid can't be found?
        if (head == NULL) {
            return;
        }

        job* current = head;
        while(current -> pgid != pgid) {
            current = current -> next;
            if (current == NULL) {
                return;
            }
        }
        current -> status = status;
        print_job_update(current);
}

bool in_array(int x, int* arr, int len) {
    for (int i = 0; i<len; i++) {
        if (arr[i] == x) {
            return true;
        }
    }
    return false;
}

int get_pgid(int pid) {
    job* current = head;
    while(current != NULL) {
        if (in_array(pid, current->pids, current->pids_len)) {
            return current ->pgid;
        }
        current = current -> next;
    }
    return -1;

}

void process_terminated(int pid) {
    // printf("PID: %d",pid);
    int pgid = get_pgid(pid);
    if (killpg(pgid, 0) == -1) {
        update_status(pgid, 3);
        remove_job(pgid);
    }
}

void update_statuses() {
    int status;
    int curr_pid = waitpid(-1, &status, WNOHANG | WUNTRACED);

    while (curr_pid > 0) {
        if (WIFEXITED(status) || WIFSIGNALED(status)) {
            process_terminated(curr_pid);
        } else if (WIFSTOPPED(status)) {
            update_status(curr_pid, 0);
        }
        curr_pid = waitpid(-1, &status, WNOHANG | WUNTRACED);
    } 


}