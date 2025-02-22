#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <mqueue.h>
#include <pthread.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <signal.h>

#define SERVER_QUEUE "/server_queue"
#define RESPONSE_QUEUE_PREFIX "/client_queue_"
#define MAX_SIZE 1024
#define MAX_CLIENTS 10
#define SHUTDOWN_MSG "SHUTDOWN"
#define LIST_CMD "LIST"
#define HIDE_CMD "HIDE"
#define UNHIDE_CMD "UNHIDE"

typedef struct {
    pid_t client_pid;
    char command[MAX_SIZE];
} client_request_t;

pthread_mutex_t client_list_mutex = PTHREAD_MUTEX_INITIALIZER;
pid_t client_pids[MAX_CLIENTS];
int client_hidden[MAX_CLIENTS] = {0}; // Initially, all clients are visible

int client_count = 0;

void *handle_client_request(void *arg) {
    client_request_t request = *(client_request_t *)arg;
    free(arg);

    printf("[Server] Received command from client %d: %s\n", request.client_pid, request.command);
    printf("[Server] Created thread to process command.\n");

    char response_queue_name[MAX_SIZE];
    snprintf(response_queue_name, MAX_SIZE, "%s%d", RESPONSE_QUEUE_PREFIX, request.client_pid);
    
    mqd_t response_mq = mq_open(response_queue_name, O_WRONLY);
    if (response_mq == -1) {
        perror("[Server] Failed to open response queue");
        pthread_exit(NULL);
    }

    char output[MAX_SIZE] = {0};

    if (strcmp(request.command, LIST_CMD) == 0) {
        // Handle "LIST" command to send all connected client PIDs
        pthread_mutex_lock(&client_list_mutex);
        int visible_clients = 0; // Counter for visible clients
        snprintf(output, MAX_SIZE, "[Server] Connected Clients:\n");
        for (int i = 0; i < client_count; i++) {
            if (!client_hidden[i]) { // Check if the client is not hidden
                char client_info[50];
                snprintf(client_info, 50, "Client PID: %d\n", client_pids[i]);
                strncat(output, client_info, MAX_SIZE - strlen(output) - 1);
                visible_clients++;
            }
        }
        if (visible_clients == 0) { // If no visible clients
            snprintf(output, MAX_SIZE, "[Server] All Clients Are Hidden...\n");
        }
        pthread_mutex_unlock(&client_list_mutex);
    } else if (strcmp(request.command, HIDE_CMD) == 0) {
        pthread_mutex_lock(&client_list_mutex);
        for (int i = 0; i < client_count; i++) {
            if (client_pids[i] == request.client_pid) {
                if (client_hidden[i] == 1) {
                    snprintf(output, MAX_SIZE, "[Server] You Are Already Hidden...\n");
                } else {
                    client_hidden[i] = 1;
                    snprintf(output, MAX_SIZE, "[Server] You Are Now Hidden...\n");
                }
                break;
            }
        }
        pthread_mutex_unlock(&client_list_mutex);
    } else if (strcmp(request.command, UNHIDE_CMD) == 0) {
        pthread_mutex_lock(&client_list_mutex);
        for (int i = 0; i < client_count; i++) {
            if (client_pids[i] == request.client_pid) {
                if (client_hidden[i] == 0) {
                    snprintf(output, MAX_SIZE, "[Server] You Are Not Hidden At All...\n");
                } else {
                    client_hidden[i] = 0;
                    snprintf(output, MAX_SIZE, "[Server] You Are Now Visible Again...\n");
                }
                break;
            }
        }
        pthread_mutex_unlock(&client_list_mutex);
    } else {
        // Handle shell commands
        FILE *fp = popen(request.command, "r");
        if (fp == NULL) {
            snprintf(output, MAX_SIZE, "[Error] Failed to execute command.");
        } else {
            fread(output, 1, MAX_SIZE, fp);
            pclose(fp);
        }
    }

    mq_send(response_mq, output, strlen(output) + 1, 0);
    mq_close(response_mq);

    printf("[Server] Exited child thread.\n");
    return NULL;
}

void handle_shutdown(int signum) {
    printf("[Server] Shutting down...\n");

    mqd_t mq = mq_open(SERVER_QUEUE, O_WRONLY);
    if (mq != -1) {
        mq_send(mq, SHUTDOWN_MSG, strlen(SHUTDOWN_MSG) + 1, 0);
        mq_close(mq);
    }
    
    mq_unlink(SERVER_QUEUE);
    exit(0);
}

int main() {
    signal(SIGINT, handle_shutdown);

    struct mq_attr attr = { .mq_flags = 0, .mq_maxmsg = 10, .mq_msgsize = sizeof(client_request_t), .mq_curmsgs = 0 };
    
    mqd_t mq = mq_open(SERVER_QUEUE, O_CREAT | O_RDONLY, 0666, &attr);
    if (mq == -1) {
        perror("[Server] mq_open failed");
        exit(1);
    }

    printf("[Server] Listening for client commands...\n");

    while (1) {
        client_request_t *request = malloc(sizeof(client_request_t));
        if (mq_receive(mq, (char *)request, sizeof(client_request_t), NULL) > 0) {
            pthread_mutex_lock(&client_list_mutex);
            int known_client = 0;
            for (int i = 0; i < client_count; i++) {
                if (client_pids[i] == request->client_pid) {
                    known_client = 1;
                    break;
                }
            }
            if (!known_client && client_count < MAX_CLIENTS) {
                client_pids[client_count] = request->client_pid;
                client_hidden[client_count] = 0; // Initialize as visible
                client_count++;
            }
            pthread_mutex_unlock(&client_list_mutex);

            pthread_t thread;
            pthread_create(&thread, NULL, handle_client_request, request);
            pthread_detach(thread);
        } else {
            free(request);
        }
    }

    mq_close(mq);
    mq_unlink(SERVER_QUEUE);
    return 0;
}
