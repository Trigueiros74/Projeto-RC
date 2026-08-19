#ifndef PARSER_SERVER_H
#define PARSER_SERVER_H

/* Field validators. Each returns 1 when the value obeys the protocol. */
int valid_uid_field(const char *s);
int valid_password_field(const char *s);
int valid_eid_field(const char *s);
int valid_event_name_field(const char *s);
int valid_event_date_field(const char *s);
int valid_fname_field(const char *s);
int valid_attendance_field(const char *s, int *out);
int valid_people_field(const char *s, int *out);
int valid_fsize_field(const char *s, long *out);

/* Message parsers. They return 0 when the whole message is well formed and 1
   otherwise, in which case the caller answers with the protocol's ERR status.
   The output buffers must hold at least the sizes declared in storage.h plus
   room for the terminator. */
int parse_lin(const char *buffer, char *uid, char *password);
int parse_lou(const char *buffer, char *uid, char *password);
int parse_unr(const char *buffer, char *uid, char *password);
int parse_lme(const char *buffer, char *uid, char *password);
int parse_lmr(const char *buffer, char *uid, char *password);

#endif /* PARSER_SERVER_H */
