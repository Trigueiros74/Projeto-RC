#ifndef PARSER_SERVER
#define PARSER_SERVER

int validate_string_spaces(const char *str, int limit);

/* Validate password: exactly 8 alphanumeric characters */
int validate_password_str(const char *s);

int parse_lin(char *buffer, char *uid, char *password);
int parse_lou(char *buffer, char *uid, char *password);
int parse_unr(char *buffer, char *uid, char *password);
int parse_lme(char *buffer, char *uid, char *password);
int parse_lmr(char *buffer, char *uid, char *password);

int parse_cre_header(char *header, char *uid, char *password, char *name, char *event_date, int *attendance_size, char *fname);
int parse_cls(char *buffer, char *uid, char *password, char *eid);
int parse_lst(char *buffer);
int parse_sed(char *buffer, char *eid);
int parse_rid(char *buffer, char *uid, char *password, char *eid, int *people);
int parse_cps(char *buffer, char *uid, char *oldpass, char *newpass);

#endif
