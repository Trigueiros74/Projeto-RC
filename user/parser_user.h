#ifndef PARSER_USER
#define PARSER_USER

int parse_login(char *buffer, char *request);
int parse_changePass(char *buffer, char *request, char *UID);
int parse_unregister(char *buffer, char *request, char *UID);
int parse_logout(char *buffer, char *request, char *UID);
int parse_create(char *buffer, char *request, char *UID);
int parse_close(char *buffer, char *request, char *UID);
int parse_myevents(char *buffer, char *request, char *UID);
int parse_list(char *buffer, char *request);
int parse_show(char *buffer, char *request);
int parse_reserve(char *buffer, char *request, char *UID);
int parse_myreservations(char *buffer, char *request, char *UID);
char* get_user_password(const char *UID);

#endif // PARSER_USER_H
