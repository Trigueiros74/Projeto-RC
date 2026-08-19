#ifndef PARSER_USER_H
#define PARSER_USER_H

/* ---- protocol limits (section 3 of the project statement) ---- */
#define UID_LEN           6                   /* 6 digits                 */
#define PASSWORD_LEN      8                   /* 8 alphanumerics          */
#define EID_LEN           3                   /* 3 digits                 */
#define EVENT_NAME_MAX    10                  /* alphanumeric only        */
#define EVENT_DATE_LEN    16                  /* dd-mm-yyyy hh:mm         */
#define TIMESTAMP_LEN     19                  /* dd-mm-yyyy hh:mm:ss      */
#define FILENAME_MAX_LEN  24                  /* "nnn...nnn.xxx"          */
#define FDATA_MAX_SIZE    (10L * 1000 * 1000) /* 10 MB                    */
#define ATTENDANCE_MIN    10
#define ATTENDANCE_MAX    999
#define PEOPLE_MIN        1
#define PEOPLE_MAX        999

/* Longest line the interactive prompt accepts. Anything longer is refused
   with a message instead of being silently split into two commands. */
#define INPUT_LINE_MAX    512

/* ---- field validators: 1 when the value obeys the protocol ---- */
int valid_uid(const char *s);
int valid_password(const char *s);
int valid_eid(const char *s);
int valid_event_name(const char *s);
int valid_event_date(const char *s);
int valid_fname(const char *s);

/* ---- command line parsers ------------------------------------------------
 * Each one validates the arguments the user typed and fills the output
 * buffers, whose sizes are the limits above plus one. They return 0 on
 * success, and 1 after explaining on stderr what was wrong. */
int parse_login(const char *args, char *uid, char *password);
int parse_change_pass(const char *args, char *old_password, char *new_password);
int parse_create(const char *args, char *name, char *fname, char *event_date, int *attendees);
int parse_eid(const char *args, char *eid);                  /* close, show   */
int parse_reserve(const char *args, char *eid, int *people);
int parse_no_args(const char *args);                         /* list, logout… */

#endif /* PARSER_USER_H */
