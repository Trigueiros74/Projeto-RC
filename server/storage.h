/* storage.h - persistent state of the Event Reservation Server.
 *
 * The server keeps *no* long-lived state in memory: every request reads what
 * it needs from the ESDIR tree and writes back what it changed.  Concurrent
 * request handlers (one forked child per TCP connection, plus the parent
 * serving UDP) therefore never disagree about the state of the world, as long
 * as each of them brackets its work with storage_lock()/storage_unlock().
 *
 * On-disk layout:
 *
 *   ESDIR/
 *   |-- .lock                                   advisory lock (flock)
 *   |-- USERS/<UID>/
 *   |   |-- <UID>_pass.txt                      password (presence == registered)
 *   |   |-- <UID>_login.txt                     presence == logged in
 *   |   |-- CREATED/<EID>.txt                   events created by the user
 *   |   `-- RESERVED/R-<UID>-<ts>-<seq>.txt     reservations made by the user
 *   `-- EVENTS/<EID>/
 *       |-- START_<EID>.txt                     owner name fname attendance date
 *       |-- END_<EID>.txt                       presence == closed by the owner
 *       |-- RES_<EID>.txt                       seats reserved so far
 *       |-- DESCRIPTION/<Fname>                 the file describing the event
 *       `-- RESERVATIONS/R-<UID>-<ts>-<seq>.txt
 */

#ifndef STORAGE_H
#define STORAGE_H

#include <stddef.h>

/* ---- protocol limits (section 3 of the project statement) ---- */
#define UID_LEN                 6                  /* 6 digits              */
#define PASSWORD_LEN            8                  /* 8 alphanumerics       */
#define EVENT_NAME_MAX          10                 /* alphanumeric only     */
#define EVENT_DATE_LEN          16                 /* dd-mm-yyyy hh:mm      */
#define TIMESTAMP_LEN           19                 /* dd-mm-yyyy hh:mm:ss   */
#define FILENAME_MAX_LEN        24                 /* "nnn...nnn.xxx"       */
#define FSIZE_MAX_DIGITS        8                  /* Fsize is <= 8 digits  */
#define FDATA_MAX_SIZE          (10L * 1000 * 1000) /* 10 MB                */
#define ATTENDANCE_MIN          10
#define ATTENDANCE_MAX          999
#define EID_MIN                 1
#define EID_MAX                 999
#define RESERVATIONS_LISTED_MAX 50                 /* LMR sends at most 50  */

/* ---- event states reported by LST and LME ---- */
enum event_state {
    EVENT_PAST     = 0,  /* the event date is already in the past  */
    EVENT_OPEN     = 1,  /* still accepting reservations           */
    EVENT_SOLD_OUT = 2,  /* in the future but with no seats left   */
    EVENT_CLOSED   = 3   /* closed by its owner                    */
};

typedef struct {
    char uid[UID_LEN + 1];
    char password[PASSWORD_LEN + 1];
    int  logged_in;
} User;

typedef struct {
    int  eid;
    char owner[UID_LEN + 1];
    char name[EVENT_NAME_MAX + 1];
    char event_date[EVENT_DATE_LEN + 1];   /* dd-mm-yyyy hh:mm */
    int  attendance_size;
    int  seats_reserved;
    char fname[FILENAME_MAX_LEN + 1];
    long fsize;                            /* size of the description file */
    int  closed;
} Event;

typedef struct {
    char uid[UID_LEN + 1];
    int  eid;
    int  people;
    char timestamp[TIMESTAMP_LEN + 1];     /* dd-mm-yyyy hh:mm:ss */
} Reservation;

/* ---- clock helpers ---- */
void now_datetime(char out[EVENT_DATE_LEN + 1]);   /* dd-mm-yyyy hh:mm     */
void now_timestamp(char out[TIMESTAMP_LEN + 1]);   /* dd-mm-yyyy hh:mm:ss  */
int  date_cmp(const char *a, const char *b);       /* -1 / 0 / 1           */

/* ---- lifecycle and mutual exclusion ---- */
int  storage_init(void);
int  storage_lock(int exclusive);   /* 0 on success; blocks until acquired */
void storage_unlock(void);
/* Must be called by a child right after fork(): flock() locks belong to the
   open file description, which fork() shares, so a child that kept the
   inherited descriptor would silently share the parent's lock instead of
   competing for it. */
int  storage_after_fork(void);

/* ---- users ---- */
int  user_load(const char *uid, User *out);        /* 0 found, -1 unknown  */
int  user_register(const char *uid, const char *password);
int  user_set_password(const char *uid, const char *password);
int  user_set_login(const char *uid, int logged_in);
int  user_unregister(const char *uid);

/* ---- events ---- */
int  event_load(int eid, Event *out);              /* 0 found, -1 unknown  */
int  event_next_eid(void);                         /* -1 when exhausted    */
int  event_create(Event *ev, const char *staged_description);
int  event_set_reserved(int eid, int seats_reserved);
int  event_close(int eid);
int  event_state(const Event *ev);
/* Both listings return a malloc'ed array sorted by ascending EID. */
int  event_list_all(Event **out, int *count);
int  event_list_by_owner(const char *uid, Event **out, int *count);
int  event_description_path(const Event *ev, char *out, size_t cap);

/* ---- reservations ---- */
int  reservation_add(const Reservation *r);
/* Newest first, at most `limit` entries; *out is malloc'ed (NULL when none). */
int  reservation_list_by_user(const char *uid, Reservation **out, int *count, int limit);

/* ---- staging area for uploads ---- */
/* Returns a writable fd for a unique temporary file and stores its path in
 * `path`; the caller either hands it to event_create() or removes it. */
int  storage_stage_open(char *path, size_t cap);

#endif /* STORAGE_H */
