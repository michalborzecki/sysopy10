#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <sys/time.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/un.h>
#include <unistd.h>
#include <errno.h>
#include "main.h"

#define MAX_CLIENTS 10
#define CONNECTION_TIMEOUT 15

struct Client {
    int sock;
    struct sockaddr_storage address;
    socklen_t address_len;
    char username[MAX_USERNAME_LEN + 1];
    struct timeval last_msg_time;
};


int read_args(int argc, char *argv[], int *port, char **socket_unix_path);
void sigalrm_handler(int signum);
int prepare_unix_socket(const char *path);
int prepare_internet_socket(int port);
void sigint_handler(int signum);
void cleanup();
void broadcast(struct Message msg);
void disconnect_client(int index);

char *socket_unix_path;
struct Client clients[MAX_CLIENTS];
int clients_connected = 0;
fd_set sock_set;
int unix_socket;
int internet_socket;

int main(int argc, char *argv[]) {
    int port;
    char *args_help = "Enter port and path to unix socket.\n";
    if (read_args(argc, argv, &port, &socket_unix_path) != 0) {
        printf(args_help);
        return 1;
    }

    atexit(cleanup);
    struct sigaction act;
    memset(&act, 0, sizeof act);
    act.sa_handler = sigint_handler;
    sigaction(SIGINT, &act, NULL);
    sigaction(SIGTSTP, &act, NULL);
    act.sa_handler = sigalrm_handler;
    sigaction(SIGALRM, &act, NULL);

    for (int i = 0; i < MAX_CLIENTS; i++) {
        clients[i].sock = -1;
        clients[i].username[0] = '\0';
        clients[i].address_len = 0;
    }

    unlink(socket_unix_path);
    unix_socket = prepare_unix_socket(socket_unix_path);
    internet_socket = prepare_internet_socket(port);
    if (unix_socket == -1 || internet_socket == -1) {
        printf("Error while creating sockets occurred.\n");
        return 1;
    }

    struct itimerval timer_val;
    struct timeval timer_time;
    timer_time.tv_sec = 3;
    timer_time.tv_usec = 0;
    timer_val.it_interval = timer_time;
    timer_val.it_value = timer_time;

    setitimer(ITIMER_REAL, &timer_val, NULL);

    fd_set read_sock_set;
    FD_ZERO(&sock_set);
    int fd_max = unix_socket > internet_socket ? unix_socket : internet_socket;
    FD_SET(unix_socket, &sock_set);
    FD_SET(internet_socket, &sock_set);
    struct timeval msg_time;
    while(1) {
        gettimeofday(&msg_time, NULL);
        read_sock_set = sock_set;
        if (select(fd_max + 1, &read_sock_set, NULL , NULL , NULL) < 0) {
            if (errno == EINTR)
                continue;
            else {
                printf("Error while executing select occurred.\n");
                sleep(1);
                continue;
            }
        }
        struct Message msg;
        struct sockaddr_storage addr;
        int sock;
        socklen_t sa_len;
        if (FD_ISSET(unix_socket, &read_sock_set)) {
            sa_len = sizeof(struct sockaddr_un);
            if (recvfrom(unix_socket, &msg, MESSAGE_STRUCT_SIZE, 0, (struct sockaddr *)&addr, &sa_len) <= 0) {
                printf("Error while receiving message occurred.\n");
                continue;
            }
            sock = unix_socket;
        }
        else if (FD_ISSET(internet_socket, &read_sock_set)) {
            sa_len = sizeof(struct sockaddr_in);
            if (recvfrom(internet_socket, &msg, MESSAGE_STRUCT_SIZE, 0, (struct sockaddr *)&addr, &sa_len) <= 0) {
                printf("Error while receiving message occurred.\n");
                continue;
            }
            sock = internet_socket;
        }
        else
            continue;
        int index = -1;
        for (int i = 0; i < MAX_CLIENTS; i++) {
            if (strcmp(clients[i].username, msg.username) == 0) {
                index = i;
                break;
            }
        }
        if (index == -1) {
            if (clients_connected == MAX_CLIENTS) {
                printf("Cannot accept new client.\n");
                continue;
            }
            for (int i = 0; i < MAX_CLIENTS; i++) {
                if (strlen(clients[i].username) == 0) {
                    index = i;
                    break;
                }
            }
            strcpy(clients[index].username, msg.username);
            clients_connected++;
            printf("new client\n");
        }
        clients[index].sock = sock;
        clients[index].last_msg_time = msg_time;
        clients[index].address = addr;
        clients[index].address_len = sa_len;
        broadcast(msg);
    }
}

int read_args(int argc, char *argv[], int *port, char **socket_unix_path) {
    if (argc != 3) {
        printf("Incorrect number of arguments.\n");
        return 1;
    }
    int arg_num = 1;
    *port = atoi(argv[arg_num++]);
    if (*port <= 0) {
        printf("Incorrect port number. It should be > 0.\n");
        return 1;
    }
    *socket_unix_path = argv[arg_num];

    return 0;
}

void disconnect_client(int index) {
    struct Message msg;
    strcpy(msg.username, clients[index].username);
    strcpy(msg.message, "Disconnected\n");
    broadcast(msg);
    clients[index].address_len = 0;
    clients[index].sock = -1;
    clients[index].username[0] = '\0';
    clients_connected--;
}

void broadcast(struct Message msg) {
    for (int j = 0; j < MAX_CLIENTS; j++) {
        if (strlen(clients[j].username) == 0 || strcmp(clients[j].username, msg.username) == 0)
            continue;
        if(sendto(clients[j].sock, &msg, sizeof(msg), 0, (struct sockaddr *)&clients[j].address, clients[j].address_len) <= 0) {
            printf("Error while redirecting message.\n");
        }
    }
}

int prepare_unix_socket(const char *path) {
    int sock = socket(AF_UNIX, SOCK_DGRAM, 0);
    if (sock == -1) {
        return -1;
    }
    struct sockaddr_un server_unix;
    server_unix.sun_family = AF_UNIX;
    strcpy(server_unix.sun_path, path);
    if (bind(sock, (struct sockaddr*)&server_unix, sizeof(server_unix)) == -1) {
        return -1;
    }
    return sock;
}

int prepare_internet_socket(int port) {
    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock == -1) {
        return -1;
    }
    struct sockaddr_in server_internet;
    server_internet.sin_family = AF_INET;
    server_internet.sin_port = htons(port);
    server_internet.sin_addr.s_addr = htonl(INADDR_ANY);
    if (bind(sock, (struct sockaddr*)&server_internet, sizeof(server_internet)) == -1) {
        printf("%d", errno);
        return -1;
    }
    return sock;
}

void sigalrm_handler(int signum) {
    struct timeval t;
    gettimeofday(&t, NULL);
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (clients[i].address_len != 0 && clients[i].last_msg_time.tv_sec + CONNECTION_TIMEOUT < t.tv_sec) {
            disconnect_client(i);
            printf("client deleted\n");
        }
    }
}

void cleanup() {
    close(unix_socket);
    close(internet_socket);
    unlink(socket_unix_path);
}

void sigint_handler(int signum) {
    exit(0);
}
