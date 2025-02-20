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
#define SHUTDOWN_MSG "SHUTDOWN"

typedef struct {
    pid_t client_pid;
    char command[MAX_SIZE];
} client_request_t;

void *listen_for_shutdown(void *arg) {
    mqd_t mq = mq_open(SERVER_QUEUE, O_RDONLY);
    if (mq == -1) {
        perror("[Client] mq_open failed");
        pthread_exit(NULL);
    }

    char buffer[MAX_SIZE];
    while (1) {
        if (mq_receive(mq, buffer, MAX_SIZE, NULL) > 0) {
            if (strcmp(buffer, SHUTDOWN_MSG) == 0) {
                printf("[Client] Server is shutting down. Exiting...\n");
                exit(0);
            }
        }
    }

    mq_close(mq);
    return NULL;
}

int main() {
    mqd_t mq = mq_open(SERVER_QUEUE, O_WRONLY);
    if (mq == -1) {
        perror("[Client] mq_open failed");
        exit(1);
    }

    char response_queue_name[MAX_SIZE];
    snprintf(response_queue_name, MAX_SIZE, "%s%d", RESPONSE_QUEUE_PREFIX, getpid());

    struct mq_attr attr = { .mq_flags = 0, .mq_maxmsg = 10, .mq_msgsize = MAX_SIZE, .mq_curmsgs = 0 };
    mqd_t response_mq = mq_open(response_queue_name, O_CREAT | O_RDONLY, 0666, &attr);
    if (response_mq == -1) {
        perror("[Client] Failed to create response queue");
        mq_close(mq);
        exit(1);
    }

    pthread_t shutdown_listener;
    pthread_create(&shutdown_listener, NULL, listen_for_shutdown, NULL);
    pthread_detach(shutdown_listener);

    printf("[Client] Connected. Enter shell commands or type 'LIST'.\n");

    char command[MAX_SIZE];
    while (1) {
        printf("Enter command: ");
        fgets(command, MAX_SIZE, stdin);
        command[strcspn(command, "\n")] = 0;

        if (strcmp(command, "exit") == 0) {
            printf("[Client] Exiting...\n");
            break;
        }

        client_request_t request;
        request.client_pid = getpid();
        strncpy(request.command, command, MAX_SIZE);

        if (mq_send(mq, (char *)&request, sizeof(client_request_t), 0) == -1) {
            perror("[Client] mq_send failed");
        } else {
            char output[MAX_SIZE];
            if (mq_receive(response_mq, output, MAX_SIZE, NULL) > 0) {
                printf("[Client] Response:\n%s\n", output);
            } else {
                perror("[Client] Failed to receive response");
            }
        }
    }

    mq_close(mq);
    mq_close(response_mq);
    mq_unlink(response_queue_name);
    return 0;
}