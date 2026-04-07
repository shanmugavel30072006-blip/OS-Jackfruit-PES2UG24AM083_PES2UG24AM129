#define _GNU_SOURCE
#include <stdio.h>
#include <unistd.h>
#include <sys/wait.h>
#include <string.h>
#include <signal.h>
#include <sched.h>
#include <sys/mount.h>
#include <stdlib.h>
#include <pthread.h>
#include <fcntl.h>
#include <sys/ioctl.h>

#define MAX_CONTAINERS 10
#define STACK_SIZE (1024 * 1024)
#define BUFFER_SIZE 10

#define IOCTL_REGISTER_PID _IOW('a', 'a', int)

// ================= BUFFER =================
typedef struct {
    char data[BUFFER_SIZE][256];
    int in, out, count;
    pthread_mutex_t mutex;
    pthread_cond_t not_full;
    pthread_cond_t not_empty;
} buffer_t;

buffer_t buffer;

// ================= CONTAINER =================
typedef struct {
    int id;
    pid_t pid;
    char status[20];
    int pipe_fd;
} container_t;

container_t containers[MAX_CONTAINERS];
int count = 0;

char stack[STACK_SIZE];

// ================= BUFFER INIT =================
void init_buffer() {
    buffer.in = buffer.out = buffer.count = 0;
    pthread_mutex_init(&buffer.mutex, NULL);
    pthread_cond_init(&buffer.not_full, NULL);
    pthread_cond_init(&buffer.not_empty, NULL);
}

// ================= PRODUCER =================
void* producer(void* arg) {
    int fd = *(int*)arg;
    char temp[256];
    int n;

    while ((n = read(fd, temp, sizeof(temp)-1)) > 0) {
        temp[n] = '\0';

        pthread_mutex_lock(&buffer.mutex);

        while (buffer.count == BUFFER_SIZE)
            pthread_cond_wait(&buffer.not_full, &buffer.mutex);

        strcpy(buffer.data[buffer.in], temp);
        buffer.in = (buffer.in + 1) % BUFFER_SIZE;
        buffer.count++;

        pthread_cond_signal(&buffer.not_empty);
        pthread_mutex_unlock(&buffer.mutex);
    }

    return NULL;
}

// ================= CONSUMER =================
void* consumer(void* arg) {
    FILE *log = fopen("logs/container.log", "a");

    while (1) {
        pthread_mutex_lock(&buffer.mutex);

        while (buffer.count == 0)
            pthread_cond_wait(&buffer.not_empty, &buffer.mutex);

        char temp[256];
        strcpy(temp, buffer.data[buffer.out]);

        buffer.out = (buffer.out + 1) % BUFFER_SIZE;
        buffer.count--;

        pthread_cond_signal(&buffer.not_full);
        pthread_mutex_unlock(&buffer.mutex);

        // ✅ Only write to file (no terminal printing)
        fprintf(log, "%s", temp);
        fflush(log);
    }
}

// ================= CONTAINER FUNCTION =================
int container_func(void *arg) {
    int *pipefd = (int *)arg;

    dup2(pipefd[1], STDOUT_FILENO);
    dup2(pipefd[1], STDERR_FILENO);

    close(pipefd[0]);
    close(pipefd[1]);

    sethostname("mycontainer", 11);

    chroot("./rootfs");
    chdir("/");
    mount("proc", "/proc", "proc", 0, NULL);

    execlp("sh", "sh", "-c", "echo A; echo B; echo C", NULL);

    perror("exec failed");
    return 1;
}

// ================= SIGCHLD =================
void sigchld_handler(int sig) {
    int status;
    pid_t pid;

    while ((pid = waitpid(-1, &status, WNOHANG)) > 0) {
        for (int i = 0; i < count; i++) {
            if (containers[i].pid == pid) {
                strcpy(containers[i].status, "stopped");
                break;
            }
        }
    }
}

// ================= MAIN =================
int main() {
    char command[100];

    signal(SIGCHLD, sigchld_handler);
    init_buffer();

    // Consumer thread (detached)
    pthread_t cons;
    pthread_create(&cons, NULL, consumer, NULL);
    pthread_detach(cons);

    while (1) {
        printf("mini-container> ");
        fflush(stdout);

        fgets(command, sizeof(command), stdin);
        command[strcspn(command, "\n")] = 0;

        // RUN
        if (strncmp(command, "run", 3) == 0) {

            int pipefd[2];
            pipe(pipefd);

            pid_t pid = clone(container_func,
                              stack + STACK_SIZE,
                              CLONE_NEWPID | CLONE_NEWUTS | CLONE_NEWNS | SIGCHLD,
                              pipefd);

            if (pid == -1) {
                perror("clone failed");
                continue;
            }

            // Send PID to kernel
            int fd = open("/dev/container_monitor", O_RDWR);
            if (fd >= 0) {
                ioctl(fd, IOCTL_REGISTER_PID, &pid);
                close(fd);
            }

            containers[count].id = count;
            containers[count].pid = pid;
            strcpy(containers[count].status, "running");
            containers[count].pipe_fd = pipefd[0];
            count++;

            close(pipefd[1]);

            // Producer thread (detached)
            pthread_t prod;
            pthread_create(&prod, NULL, producer, &pipefd[0]);
            pthread_detach(prod);
        }

        // PS
        else if (strcmp(command, "ps") == 0) {
            printf("\nID\tPID\tSTATUS\n");
            for (int i = 0; i < count; i++) {
                printf("%d\t%d\t%s\n",
                       containers[i].id,
                       containers[i].pid,
                       containers[i].status);
            }
        }

        // EXIT
        else if (strcmp(command, "exit") == 0) {
            break;
        }

        else {
            printf("Unknown command\n");
        }
    }

    return 0;
}