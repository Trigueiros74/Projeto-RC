#ifndef COMMANDS
#define COMMANDS

#include <stddef.h>
#include <netdb.h>

void add_user_session(const char *UID, const char *password);
void remove_user_session(const char *UID);

int validate_string_spaces(const char *str, int limit);
int send_udp_request(char *request, int fd_udp, struct addrinfo *res, char *response);
int send_tcp_request(char *request, struct addrinfo *res, char *response, size_t response_cap);
int create_file(char *f_name, char *f_data);

int cmd_login(char *request, unsigned int *user_id, int *logged_in, int fd_udp, struct addrinfo *res, const char *password);
int cmd_logout(char *request, unsigned int *user_id, int *logged_in, int fd_udp, struct addrinfo *res);
int cmd_unregister(char *request, unsigned int *user_id, int *logged_in, int fd_udp, struct addrinfo *res);
int cmd_myevents(char *request, int fd_udp, struct addrinfo *res);
int cmd_myreservations(char *request, int fd_udp, struct addrinfo *res);

int cmd_changePass(char *request, struct addrinfo *res);
int cmd_create(char *request, struct addrinfo *res);
int cmd_close(char *request, struct addrinfo *res);
int cmd_list(char *request, struct addrinfo *res);
int cmd_show(char *request, struct addrinfo *res);
int cmd_reserve(char *request, struct addrinfo *res);

int cmd_exit(int logged_in);

#endif
