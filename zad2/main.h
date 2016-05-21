#ifndef ZAD2_MAIN_H
#define ZAD2_MAIN_H

#define MAX_USERNAME_LEN 20
#define MAX_MSG_LEN 200
#define MESSAGE_STRUCT_SIZE MAX_USERNAME_LEN + MAX_MSG_LEN + 2

struct Message {
    char username[MAX_USERNAME_LEN + 1];
    char message[MAX_MSG_LEN + 1];
};

#endif //ZAD2_MAIN_H
