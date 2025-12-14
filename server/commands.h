#ifndef COMMANDS
#define COMMANDS

#include <stddef.h>

#define MAX_FILENAME_LEN 24
#define MAX_FDATA_SIZE (10 * 1024 * 1024)  /* 10 MB */
#define MAX_EID 999  /* EID is 3-digit: 001-999 */
#define MAX_MYRESERVATIONS_DISPLAY 50  /* myreservations command shows max 50 (most recent) */

typedef struct {
    char uid[7];
    char password[128];
    int logged_in;
    int used;
} User;

typedef struct {
    int eid;
    char owner[7];
    char name[11];
    char event_date[17];  /* dd-mm-yyyy hh:mm */
    int attendance_size;
    int seats_reserved;
    char fname[MAX_FILENAME_LEN+1];
    int fsize;
    char *fdata;
    int closed;
    int used;
} Event;

typedef struct {
    char uid[7];
    int eid;
    int people;
    char timestamp[20];  /* dd-mm-yyyy hh:mm:ss */
    int used;
} Reservation;

/* UDP handlers (fill response buffer, return 0 on success, 1 on fatal error) */
int cmd_lin(char *response, const char *uid, const char *password);
int cmd_lou(char *response, const char *uid, const char *password);
int cmd_unr(char *response, const char *uid, const char *password);
int cmd_lme(char *response, const char *uid, const char *password);
int cmd_lmr(char *response, const char *uid, const char *password);

/* TCP handlers (allocate *response_buf via malloc, set *response_len; return 0 on success) */
int cmd_cre(char **response_buf, size_t *response_len, const char *uid, const char *password,
            const char *name, const char *event_date, int attendance_size,
            const char *fname, int fsize, const char *fdata);

int cmd_cls(char **response_buf, size_t *response_len, const char *uid, const char *password, const char *eid);
int cmd_lst(char **response_buf, size_t *response_len);
int cmd_sed(char **response_buf, size_t *response_len, const char *eid);
int cmd_rid(char **response_buf, size_t *response_len, const char *uid, const char *password, const char *eid, int people);
int cmd_cps(char **response_buf, size_t *response_len, const char *uid, const char *oldpass, const char *newpass);

/* lifecycle helpers */
int commands_init(void);
int init_persistence(void);  /* Initialize file-based persistence system */

void commands_cleanup(void);

#endif
