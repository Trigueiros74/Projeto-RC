#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <errno.h>
extern int errno;

#include "parser_user.h"
#include "commands.h"

#define DEFAULT_PORT "58068"
#define PORT_STRLEN 6
#define WAIT_TIME_LIMIT 15
#define MAX_USERS 100
#define MAX_PASS_LEN 64

typedef struct {
    char UID[16];
    char password[MAX_PASS_LEN];
    int logged_in;
} UserSession;

UserSession user_table[MAX_USERS];
int num_users = 0;

void add_user_session(const char *UID, const char *password) {
    for (int i = 0; i < num_users; i++) {
        if (strcmp(user_table[i].UID, UID) == 0) {  // compare strings
            strcpy(user_table[i].password, password);
            user_table[i].logged_in = 1;
            return;
        }
    }
    if (num_users < MAX_USERS) {
        strcpy(user_table[num_users].UID, UID);      // copy string
        strcpy(user_table[num_users].password, password);
        user_table[num_users].logged_in = 1;
        num_users++;
    }
}


char* get_user_password(const char *UID) {
    for (int i = 0; i < num_users; i++) {
        if (strcmp(user_table[i].UID, UID) == 0 && user_table[i].logged_in)
            return user_table[i].password;
    }
    return NULL;
}


void remove_user_session(const char *UID) {
    for (int i = 0; i < num_users; i++) {
        if (strcmp(user_table[i].UID, UID) == 0) {
            user_table[i].logged_in = 0;
            return;
        }
    }
}



void get_addr_info(char *ESIP, char* ESport, struct addrinfo **res_udp, struct addrinfo **res_tcp) {
    struct addrinfo hints;
    int errcode;

    if(strcmp(ESIP, "") == 0) {
        if(gethostname(ESIP, INET_ADDRSTRLEN) == -1) {
            fprintf(stderr,"Error: %s\n", strerror(errno));
            exit(1);
        }
    }

    memset(&hints,0,sizeof hints);
    hints.ai_family=AF_INET;
    hints.ai_socktype=SOCK_DGRAM;
    hints.ai_flags=AI_CANONNAME;
    
    if((errcode = getaddrinfo(ESIP, ESport, &hints, res_udp)) != 0) {
        fprintf(stderr, "Error: getaddrinfo: %s\n", gai_strerror(errcode));
        exit(1);
    }

    memset(&hints,0,sizeof hints);
    hints.ai_family=AF_INET;
    hints.ai_socktype=SOCK_STREAM;
    hints.ai_flags=AI_CANONNAME;
    
    if((errcode = getaddrinfo(ESIP, ESport, &hints, res_tcp)) != 0) {
        fprintf(stderr, "Error: getaddrinfo: %s\n", gai_strerror(errcode));
        freeaddrinfo(*res_udp);
        exit(1);
    }

    return;
}

int interact(struct addrinfo *res_udp, struct addrinfo *res_tcp, int fd_udp) {
    char UID[16] = "";
    char UID_str[16];
    unsigned int user_id = 0;
    int logged_in = 0;

    while(1) {
        char buffer[256];
        char command[20]; 
        char request[128];

        if(!fgets(buffer, sizeof(buffer), stdin)) {
            fprintf(stderr, "Error reading from stdin!\n");
            return 1;
        }

        if(sscanf(buffer, "%19s", command) != 1) {
            fprintf(stderr, "Please type a command!\n");
            continue;
        };

        strcpy(UID_str, UID); // convert UID to string for parse_* functions

        if(strcmp(command, "login") == 0) {
            if(logged_in) {
                fprintf(stderr, "Already logged in!\n");
                continue;
            }
            if(parse_login(buffer, request) == 0) {
                printf("DEBUG: parse_login produced: '%s'\n", request);
                char password[MAX_PASS_LEN];
                char temp_UID[16];
                if(sscanf(buffer, "login %15s %63s", temp_UID, password) != 2) {
                    fprintf(stderr, "Invalid login format!\n");
                    continue;
                }
                if(cmd_login(request, &user_id, &logged_in, fd_udp, res_udp, password) == 1)
                    return 1;
                strcpy(UID, temp_UID);
                add_user_session(temp_UID, password);
            }
        }
        else if(strcmp(command, "changePass") == 0) {
            if(!logged_in) {
                fprintf(stderr, "Please login first!\n");
                continue;
            }
            if(parse_changePass(buffer, request, UID_str) == 0) {
                char old_pass[MAX_PASS_LEN], new_pass[MAX_PASS_LEN];
                if(sscanf(buffer, "changePass %63s %63s", old_pass, new_pass) != 2) {
                    fprintf(stderr, "Invalid changePass format!\n");
                    continue;
                }
                if(cmd_changePass(request, res_tcp) == 1)
                    return 1;
                char *current = get_user_password(UID_str);
                if (current) strcpy(current, new_pass);
            }
        }
        else if(strcmp(command, "unregister") == 0) {
            if(!logged_in) {
                fprintf(stderr, "Please login first!\n");
                continue;
            }
            if(parse_unregister(buffer, request, UID_str) == 0) {
                if(cmd_unregister(request, &user_id, &logged_in, fd_udp, res_udp) == 1)
                    return 1;
                remove_user_session(UID);
                UID[0] = '\0';
            }
        }
        else if(strcmp(command, "logout") == 0) {
            if(!logged_in) {
                fprintf(stderr, "Please login first!\n");
                continue;
            }
            if(parse_logout(buffer, request, UID_str) == 0) {
                if(cmd_logout(request, &user_id, &logged_in, fd_udp, res_udp) == 1)
                    return 1;
                remove_user_session(UID);
                UID[0] = '\0';
            }
        }
        else if(strcmp(command, "create") == 0) {
            if(!logged_in) {
                fprintf(stderr, "Please login first!\n");
                continue;
            }
            if(parse_create(buffer, request, UID_str) == 0)
                if(cmd_create(request, res_tcp) == 1)
                    return 1;
        }
        else if(strcmp(command, "close") == 0) {
            if(!logged_in) {
                fprintf(stderr, "Please login first!\n");
                continue;
            }
            if(parse_close(buffer, request, UID_str) == 0)
                if(cmd_close(request, res_tcp) == 1)
                    return 1;
        }
        else if(strcmp(command, "myevents") == 0 || strcmp(command, "mye") == 0) {
            if(!logged_in) {
                fprintf(stderr, "Please login first!\n");
                continue;
            }
            if(parse_myevents(buffer, request, UID_str) == 0)
                if(cmd_myevents(request, fd_udp, res_udp) == 1)
                    return 1;
        }
        else if(strcmp(command, "reserve") == 0) {
            if(!logged_in) {
                fprintf(stderr, "Please login first!\n");
                continue;
            }
            if(parse_reserve(buffer, request, UID_str) == 0)
                if(cmd_reserve(request, res_tcp) == 1)
                    return 1;
        }
        else if(strcmp(command, "myreservations") == 0 || strcmp(command, "myr") == 0) {
            if(!logged_in) {
                fprintf(stderr, "Please login first!\n");
                continue;
            }
            if(parse_myreservations(buffer, request, UID_str) == 0)
                if(cmd_myreservations(request, fd_udp, res_udp) == 1)
                    return 1;
        }
        else if(strcmp(command, "list") == 0) {
            if(parse_list(buffer, request) == 0) {
                printf("DEBUG: parse_list produced: '%s'\n", request);
                if(cmd_list(request, res_tcp) == 1)
                    return 1;
            }
        }
        else if(strcmp(command, "show") == 0) {
            if(parse_show(buffer, request) == 0)
                if(cmd_show(request, res_tcp) == 1)
                    return 1;
        }
        else if(strcmp(command, "exit") == 0) {
            if(logged_in) {
                fprintf(stderr, "Please logout first!\n");
                continue;
            }
            break;
        }
        else
            fprintf(stderr, "Invalid command!\n");
    }

    return 0;
}


int main(int argc, char *argv[]) {
    char ESIP[INET_ADDRSTRLEN] = "localhost";
    char ESport[PORT_STRLEN] = DEFAULT_PORT;
    struct addrinfo *res_udp, *res_tcp;
    struct timeval timeout;
    
    switch (argc) {
        case 1:
            break;
        case 3:
            if(strcmp(argv[1], "-n") == 0) {
                strcpy(ESIP, argv[2]);
            }
            else if(strcmp(argv[1], "-p") == 0){
                strcpy(ESport, argv[2]);
            }
            else {
                fprintf(stderr, "Invalid format. Please use: ./user [-n ESIP] [-p ESport]\n");
                return 1;
            }
            break;
        case 5:
            if(strcmp(argv[1], "-n") == 0 && strcmp(argv[3], "-p") == 0){
                strcpy(ESIP, argv[2]);
                strcpy(ESport, argv[4]);
            }
            else if(strcmp(argv[1], "-p") == 0 && strcmp(argv[3], "-n") == 0){
                strcpy(ESIP, argv[4]);
                strcpy(ESport, argv[2]);
            }
            else{
                fprintf(stderr, "Invalid format. Please use: ./user [-n ESIP] [-p ESport]\n");
                return 1;
            }
            break;
        default:
            fprintf(stderr, "Invalid number of parameters. Please use: ./user [-n ESIP] [-p ESport]\n");
            return 1;
    }

    get_addr_info(ESIP, ESport, &res_udp, &res_tcp);

    int fd_udp = socket(AF_INET, SOCK_DGRAM, 0); 
    if(fd_udp == -1) {
        fprintf(stderr, "Error in socket creation\n");
        freeaddrinfo(res_udp);
        freeaddrinfo(res_tcp);
        return 1;
    }

    timeout.tv_sec = WAIT_TIME_LIMIT;  
    timeout.tv_usec = 0;
    if (setsockopt(fd_udp, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) < 0) {
        fprintf(stderr, "Error in socket creation\n");
        freeaddrinfo(res_udp);
        freeaddrinfo(res_tcp);
        close(fd_udp);
        return 1;
    }

    int response = interact(res_udp, res_tcp, fd_udp);

    close(fd_udp);
    freeaddrinfo(res_udp);
    freeaddrinfo(res_tcp);
    
    return response;
}