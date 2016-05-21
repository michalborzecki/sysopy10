#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <ctype.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/un.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <errno.h>
#include <pthread.h>
#include "main.h"

typedef enum {LOCAL, REMOTE} Server_type;

int read_args(int argc, char *argv[], char **username, Server_type *server_type,
              char **ip_address, int *port, char **socket_unix_path);
void cleanup();
void sigint_handler(int signum);
void sigusr1_handler(int signum);
void *input_thread(void *arg);

int sock = -1;
pthread_t input_thread_id = -1;
pthread_mutex_t input_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t input_cond = PTHREAD_COND_INITIALIZER;
struct Message msg;
int is_message_to_send = 0;
struct sockaddr_un su;
struct sockaddr_in sa;
struct sockaddr *server_addr;
socklen_t addr_len;
char *socket_unix_path;
char *socket_unix_loc_path;
Server_type server_type;

int main(int argc, char *argv[]) {
    atexit(cleanup);
    struct sigaction act;
    act.sa_handler = sigint_handler;
    sigaction(SIGINT, &act, NULL);
    sigaction(SIGTSTP, &act, NULL);
    act.sa_handler = sigusr1_handler;
    sigaction(SIGUSR1, &act, NULL);
    char *username;
    char *ip_address;
    int port;
    char *args_help = "Enter username, server type (remote - then type ip address and port or local - then type path).\n";
    if (read_args(argc, argv, &username, &server_type, &ip_address, &port, &socket_unix_path) != 0) {
        printf(args_help);
        return 1;
    }

    strcpy(msg.username, username);

    if (server_type == LOCAL) {
        socket_unix_loc_path = malloc((strlen(socket_unix_path) + 10)*sizeof(char));
        sprintf(socket_unix_loc_path, "%s-%d", socket_unix_path, getpid());
        sock = socket(AF_UNIX, SOCK_DGRAM, 0);
        if (sock == -1) {
            return -1;
        }
        su.sun_family = AF_UNIX;
        strcpy(su.sun_path, socket_unix_loc_path);
        unlink(socket_unix_loc_path);
        if (bind(sock, (struct sockaddr*)&su, sizeof(su)) == -1) {
            printf("Error while connecting to server occurred.\n");
            return 1;
        }
        strcpy(su.sun_path,socket_unix_path);
        server_addr = (struct sockaddr *)&su;
        addr_len = sizeof(su);
    }
    else {
        sock = socket(AF_INET, SOCK_DGRAM, 0);
        if (sock == -1)
            return -1;
        sa.sin_family = AF_INET;
        sa.sin_port = htons(port);
        sa.sin_addr.s_addr = inet_addr(ip_address);
        server_addr = (struct sockaddr *)&sa;
        addr_len = sizeof(sa);
    }

    strcpy(msg.message, "Connected\n");
    is_message_to_send = 1;
    kill(getpid(), SIGUSR1);

    if (pthread_create(&input_thread_id, NULL, input_thread, NULL) != 0) {
        printf("Error while creating input thread occurred.\n");
        return 1;
    }

    struct Message server_msg;
    int rcv_bytes = 0;
    do {
        errno = 0;
        rcv_bytes = recvfrom(sock, &server_msg, MESSAGE_STRUCT_SIZE, 0, NULL, NULL);
        if (rcv_bytes > 0) {
            printf("%s: %s", server_msg.username, server_msg.message);
        }
        fflush(stdout);
    } while (rcv_bytes > 0 || (rcv_bytes == -1 && errno == EINTR));
    printf("Server closed connection.\n");
    return 0;
}

void str_to_lower(char * str) {
    for(int i = 0; str[i] != '\0'; i++){
        str[i] = tolower(str[i]);
    }
}

int read_args(int argc, char *argv[], char **username, Server_type *server_type,
              char **ip_address, int *port, char **socket_unix_path) {
    if (argc < 4) {
        printf("Incorrect number of arguments.\n");
        return 1;
    }
    int arg_num = 1;
    if (strlen(argv[arg_num]) > MAX_USERNAME_LEN) {
        printf("Username is too long. It must be max %d length.\n", MAX_USERNAME_LEN);
        return 1;
    }
    *username = argv[arg_num++];
    str_to_lower(argv[arg_num]);
    if (strcmp(argv[arg_num], "local") == 0) {
        *server_type = LOCAL;
    }
    else if (strcmp(argv[arg_num], "remote") == 0) {
        *server_type = REMOTE;
    }
    else {
        printf("Incorrect server type.\n");
        return 1;
    }
    arg_num++;
    if (*server_type == LOCAL) {
        *socket_unix_path = argv[arg_num];
    }
    else {
        if (argc < 5) {
            printf("Incorrect number of arguments.\n");
            return 1;
        }
        *ip_address = argv[arg_num++];
        *port = atoi(argv[arg_num]);
        if (*port <= 0) {
            printf("Incorrect port number. It should be > 0.\n");
            return 1;
        }
    }

    return 0;
}

void *input_thread(void *arg) {
    int read_bytes = 0;
    while (1) {
        pthread_mutex_lock(&input_mutex);
        while (is_message_to_send) {
            pthread_cond_wait(&input_cond, &input_mutex);
        }
        read_bytes = read(STDIN_FILENO, msg.message, MAX_MSG_LEN);
        if (read_bytes == 0) {
            pthread_mutex_unlock(&input_mutex);
            continue;
        }
        if (msg.message[read_bytes - 1] != '\n') {
            printf("Too long message!\n");
            while ((read_bytes = read(STDIN_FILENO, msg.message, MAX_MSG_LEN)) > 0);
            pthread_mutex_unlock(&input_mutex);
            continue;
        }
        msg.message[read_bytes] = '\0';
        is_message_to_send = 1;
        pthread_cond_signal(&input_cond);
        pthread_mutex_unlock(&input_mutex);
        kill(getpid(), SIGUSR1);
    }
}

void cleanup() {
    if (sock != -1)
        close(sock);
    if (input_thread_id != -1) {
        pthread_cancel(input_thread_id);
        pthread_join(input_thread_id, NULL);
    }
    pthread_mutex_destroy(&input_mutex);
    pthread_cond_destroy(&input_cond);
    if (server_type == LOCAL)
        unlink(socket_unix_loc_path);
}

void sigint_handler(int signum) {
    exit(0);
}

void sigusr1_handler(int signum) {
    pthread_mutex_lock(&input_mutex);
    if(sendto(sock, &msg, sizeof(msg), 0, server_addr, addr_len) <= 0) {
        printf("Error while sending message occurred.\n");
        pthread_mutex_unlock(&input_mutex);
        exit(0);
    }
    is_message_to_send = 0;
    pthread_cond_signal(&input_cond);
    pthread_mutex_unlock(&input_mutex);
}