/* storage.c - persistent, lock-protected state of the ES server.
   See storage.h for the on-disk layout and the rationale. */

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#include "storage.h"

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

#define BASE_DIR   "ESDIR"
#define USERS_DIR  BASE_DIR "/USERS"
#define EVENTS_DIR BASE_DIR "/EVENTS"
#define STAGE_DIR  BASE_DIR "/STAGE"
#define LOCK_FILE  BASE_DIR "/.lock"

static int lock_fd = -1;

/* ========================================================================
 * Clock helpers
 * ==================================================================== */

void now_datetime(char out[EVENT_DATE_LEN + 1]) {
    time_t now = time(NULL);
    struct tm tm;
    if (localtime_r(&now, &tm) == NULL || strftime(out, EVENT_DATE_LEN + 1, "%d-%m-%Y %H:%M", &tm) == 0)
        out[0] = '\0';
}

void now_timestamp(char out[TIMESTAMP_LEN + 1]) {
    time_t now = time(NULL);
    struct tm tm;
    if (localtime_r(&now, &tm) == NULL || strftime(out, TIMESTAMP_LEN + 1, "%d-%m-%Y %H:%M:%S", &tm) == 0)
        out[0] = '\0';
}

/* Compares two "dd-mm-yyyy hh:mm" strings chronologically. Both operands are
   zero padded and fixed width, so a field-by-field memcmp is enough. */
int date_cmp(const char *a, const char *b) {
    static const struct { int off, len; } fields[] = {
        {6, 4}, {3, 2}, {0, 2}, {11, 2}, {14, 2}  /* year, month, day, hour, minute */
    };
    size_t i;

    if (!a || !b) return 0;
    if (strlen(a) < EVENT_DATE_LEN || strlen(b) < EVENT_DATE_LEN) return 0;

    for (i = 0; i < sizeof fields / sizeof fields[0]; i++) {
        int cmp = memcmp(a + fields[i].off, b + fields[i].off, (size_t)fields[i].len);
        if (cmp != 0) return cmp < 0 ? -1 : 1;
    }
    return 0;
}

/* ========================================================================
 * Path helpers
 *
 * Every path is built from a validated UID (6 digits), a validated EID
 * (1..999) or a validated file name, so no client-supplied string can ever
 * escape the ESDIR tree.
 * ==================================================================== */

static int valid_uid(const char *uid) {
    int i;
    if (!uid || strlen(uid) != UID_LEN) return 0;
    for (i = 0; i < UID_LEN; i++)
        if (!isdigit((unsigned char)uid[i])) return 0;
    return 1;
}

static int valid_eid(int eid) {
    return eid >= EID_MIN && eid <= EID_MAX;
}

/* A description file name must stay a single path component: no separator, no
   "." or ".." and nothing but the characters the protocol allows. */
static int valid_fname(const char *fname) {
    size_t len, i;
    if (!fname) return 0;
    len = strlen(fname);
    if (len == 0 || len > FILENAME_MAX_LEN) return 0;
    if (strcmp(fname, ".") == 0 || strcmp(fname, "..") == 0) return 0;
    for (i = 0; i < len; i++) {
        char c = fname[i];
        if (!(isalnum((unsigned char)c) || c == '-' || c == '_' || c == '.')) return 0;
    }
    return 1;
}

static int ensure_dir(const char *path) {
    if (mkdir(path, 0700) == 0) return 0;
    return (errno == EEXIST) ? 0 : -1;
}

static int event_path(char *out, size_t cap, int eid, const char *suffix) {
    if (!valid_eid(eid)) return -1;
    return snprintf(out, cap, "%s/%03d%s", EVENTS_DIR, eid, suffix ? suffix : "") < (int)cap ? 0 : -1;
}

/* Removes a directory and its regular files (one level deep is all we need). */
static void remove_dir_contents(const char *path) {
    DIR *d = opendir(path);
    struct dirent *e;
    if (!d) return;
    while ((e = readdir(d)) != NULL) {
        char child[PATH_MAX];
        if (strcmp(e->d_name, ".") == 0 || strcmp(e->d_name, "..") == 0) continue;
        if (snprintf(child, sizeof child, "%s/%s", path, e->d_name) >= (int)sizeof child) continue;
        unlink(child);
    }
    closedir(d);
}

/* ========================================================================
 * Lifecycle and mutual exclusion
 * ==================================================================== */

static int open_lock_file(void) {
    int fd = open(LOCK_FILE, O_RDWR | O_CREAT, 0600);
    if (fd == -1) return -1;
    (void)fcntl(fd, F_SETFD, FD_CLOEXEC);
    return fd;
}

int storage_init(void) {
    if (ensure_dir(BASE_DIR) == -1 || ensure_dir(USERS_DIR) == -1 ||
        ensure_dir(EVENTS_DIR) == -1 || ensure_dir(STAGE_DIR) == -1)
        return -1;

    /* Leftovers from a previous run that was killed mid-upload. */
    remove_dir_contents(STAGE_DIR);

    lock_fd = open_lock_file();
    return lock_fd == -1 ? -1 : 0;
}

int storage_after_fork(void) {
    if (lock_fd != -1) close(lock_fd);
    lock_fd = open_lock_file();
    return lock_fd == -1 ? -1 : 0;
}

/* Serializes state access between the UDP parent and the forked TCP children.
   Readers share the lock, writers take it exclusively. */
int storage_lock(int exclusive) {
    if (lock_fd == -1) return -1;
    while (flock(lock_fd, exclusive ? LOCK_EX : LOCK_SH) == -1) {
        if (errno != EINTR) return -1;
    }
    return 0;
}

void storage_unlock(void) {
    if (lock_fd != -1) (void)flock(lock_fd, LOCK_UN);
}

/* ========================================================================
 * Users
 * ==================================================================== */

int user_load(const char *uid, User *out) {
    char path[PATH_MAX];
    char line[64];
    FILE *f;
    size_t len;

    if (!out || !valid_uid(uid)) return -1;
    if (snprintf(path, sizeof path, "%s/%s/%s_pass.txt", USERS_DIR, uid, uid) >= (int)sizeof path)
        return -1;

    f = fopen(path, "r");
    if (!f) return -1;                       /* no password file == not registered */
    if (fgets(line, sizeof line, f) == NULL) { fclose(f); return -1; }
    fclose(f);

    len = strcspn(line, "\r\n");
    line[len] = '\0';

    memset(out, 0, sizeof *out);
    snprintf(out->uid, sizeof out->uid, "%s", uid);
    snprintf(out->password, sizeof out->password, "%s", line);

    if (snprintf(path, sizeof path, "%s/%s/%s_login.txt", USERS_DIR, uid, uid) >= (int)sizeof path)
        return -1;
    out->logged_in = (access(path, F_OK) == 0);
    return 0;
}

int user_register(const char *uid, const char *password) {
    char dir[PATH_MAX], path[PATH_MAX];

    if (!valid_uid(uid) || !password) return -1;

    if (snprintf(dir, sizeof dir, "%s/%s", USERS_DIR, uid) >= (int)sizeof dir) return -1;
    if (ensure_dir(dir) == -1) return -1;
    if (snprintf(path, sizeof path, "%s/CREATED", dir) < (int)sizeof path) ensure_dir(path);
    if (snprintf(path, sizeof path, "%s/RESERVED", dir) < (int)sizeof path) ensure_dir(path);

    return user_set_password(uid, password);
}

int user_set_password(const char *uid, const char *password) {
    char path[PATH_MAX];
    FILE *f;

    if (!valid_uid(uid) || !password) return -1;
    if (snprintf(path, sizeof path, "%s/%s/%s_pass.txt", USERS_DIR, uid, uid) >= (int)sizeof path)
        return -1;

    f = fopen(path, "w");
    if (!f) return -1;
    if (fprintf(f, "%s\n", password) < 0) { fclose(f); return -1; }
    return fclose(f) == 0 ? 0 : -1;
}

int user_set_login(const char *uid, int logged_in) {
    char path[PATH_MAX];

    if (!valid_uid(uid)) return -1;
    if (snprintf(path, sizeof path, "%s/%s/%s_login.txt", USERS_DIR, uid, uid) >= (int)sizeof path)
        return -1;

    if (!logged_in) {
        unlink(path);
        return 0;
    }
    FILE *f = fopen(path, "w");
    if (!f) return -1;
    return fclose(f) == 0 ? 0 : -1;
}

/* Unregistering drops the credentials; the events and reservations the user
   created stay on disk so that the other participants keep seeing them. */
int user_unregister(const char *uid) {
    char path[PATH_MAX];

    if (!valid_uid(uid)) return -1;
    if (snprintf(path, sizeof path, "%s/%s/%s_pass.txt", USERS_DIR, uid, uid) < (int)sizeof path)
        unlink(path);
    if (snprintf(path, sizeof path, "%s/%s/%s_login.txt", USERS_DIR, uid, uid) < (int)sizeof path)
        unlink(path);
    return 0;
}

/* ========================================================================
 * Events
 * ==================================================================== */

int event_load(int eid, Event *out) {
    char path[PATH_MAX];
    char owner[32], name[64], fname[128], date[64];
    int attendance;
    FILE *f;
    struct stat st;

    if (!out || event_path(path, sizeof path, eid, "") == -1) return -1;
    if (snprintf(path, sizeof path, "%s/%03d/START_%03d.txt", EVENTS_DIR, eid, eid) >= (int)sizeof path)
        return -1;

    f = fopen(path, "r");
    if (!f) return -1;
    /* START holds: owner name fname attendance dd-mm-yyyy hh:mm */
    if (fscanf(f, "%31s %63s %127s %d %63[^\n]", owner, name, fname, &attendance, date) != 5) {
        fclose(f);
        return -1;
    }
    fclose(f);

    memset(out, 0, sizeof *out);
    out->eid = eid;
    snprintf(out->owner, sizeof out->owner, "%s", owner);
    snprintf(out->name, sizeof out->name, "%s", name);
    snprintf(out->fname, sizeof out->fname, "%s", fname);
    snprintf(out->event_date, sizeof out->event_date, "%s", date);
    out->attendance_size = attendance;

    if (snprintf(path, sizeof path, "%s/%03d/RES_%03d.txt", EVENTS_DIR, eid, eid) < (int)sizeof path) {
        f = fopen(path, "r");
        if (f) {
            if (fscanf(f, "%d", &out->seats_reserved) != 1) out->seats_reserved = 0;
            fclose(f);
        }
    }

    if (snprintf(path, sizeof path, "%s/%03d/END_%03d.txt", EVENTS_DIR, eid, eid) < (int)sizeof path)
        out->closed = (access(path, F_OK) == 0);

    /* The description file is never held in memory: only its size is cached
       here, the bytes are streamed straight from disk to the socket. */
    if (event_description_path(out, path, sizeof path) == 0 && stat(path, &st) == 0)
        out->fsize = (long)st.st_size;

    return 0;
}

int event_description_path(const Event *ev, char *out, size_t cap) {
    if (!ev || !valid_eid(ev->eid) || !valid_fname(ev->fname)) return -1;
    return snprintf(out, cap, "%s/%03d/DESCRIPTION/%s", EVENTS_DIR, ev->eid, ev->fname) < (int)cap
               ? 0 : -1;
}

/* EIDs are handed out in ascending order and never reused, so the next one is
   simply "highest on disk + 1". */
int event_next_eid(void) {
    DIR *d = opendir(EVENTS_DIR);
    struct dirent *e;
    int highest = 0;

    if (!d) return EID_MIN;
    while ((e = readdir(d)) != NULL) {
        int eid;
        char *end;
        if (strlen(e->d_name) != 3) continue;
        eid = (int)strtol(e->d_name, &end, 10);
        if (*end != '\0' || !valid_eid(eid)) continue;
        if (eid > highest) highest = eid;
    }
    closedir(d);

    return (highest >= EID_MAX) ? -1 : highest + 1;
}

/* Publishes an event: `staged_description` is the temporary file holding the
   uploaded bytes and is moved (not copied) into the event directory. */
int event_create(Event *ev, const char *staged_description) {
    char dir[PATH_MAX], path[PATH_MAX];
    FILE *f;

    if (!ev || !valid_eid(ev->eid) || !valid_uid(ev->owner) || !valid_fname(ev->fname))
        return -1;

    if (event_path(dir, sizeof dir, ev->eid, "") == -1 || ensure_dir(dir) == -1) return -1;
    if (snprintf(path, sizeof path, "%s/DESCRIPTION", dir) >= (int)sizeof path) return -1;
    if (ensure_dir(path) == -1) return -1;
    if (snprintf(path, sizeof path, "%s/RESERVATIONS", dir) >= (int)sizeof path) return -1;
    if (ensure_dir(path) == -1) return -1;

    if (event_description_path(ev, path, sizeof path) == -1) return -1;
    if (staged_description && rename(staged_description, path) == -1) return -1;

    if (snprintf(path, sizeof path, "%s/START_%03d.txt", dir, ev->eid) >= (int)sizeof path) return -1;
    f = fopen(path, "w");
    if (!f) return -1;
    fprintf(f, "%s %s %s %d %s\n", ev->owner, ev->name, ev->fname, ev->attendance_size, ev->event_date);
    if (fclose(f) != 0) return -1;

    if (event_set_reserved(ev->eid, ev->seats_reserved) == -1) return -1;

    /* Index the event under its owner so that LME does not scan every event. */
    if (snprintf(path, sizeof path, "%s/%s/CREATED", USERS_DIR, ev->owner) < (int)sizeof path)
        ensure_dir(path);
    if (snprintf(path, sizeof path, "%s/%s/CREATED/%03d.txt", USERS_DIR, ev->owner, ev->eid) < (int)sizeof path) {
        f = fopen(path, "w");
        if (f) { fprintf(f, "%03d\n", ev->eid); fclose(f); }
    }
    return 0;
}

int event_set_reserved(int eid, int seats_reserved) {
    char path[PATH_MAX];
    FILE *f;

    if (snprintf(path, sizeof path, "%s/%03d/RES_%03d.txt", EVENTS_DIR, eid, eid) >= (int)sizeof path)
        return -1;
    if (!valid_eid(eid)) return -1;

    f = fopen(path, "w");
    if (!f) return -1;
    fprintf(f, "%d\n", seats_reserved);
    return fclose(f) == 0 ? 0 : -1;
}

int event_close(int eid) {
    char path[PATH_MAX], stamp[TIMESTAMP_LEN + 1];
    FILE *f;

    if (!valid_eid(eid)) return -1;
    if (snprintf(path, sizeof path, "%s/%03d/END_%03d.txt", EVENTS_DIR, eid, eid) >= (int)sizeof path)
        return -1;

    now_timestamp(stamp);
    f = fopen(path, "w");
    if (!f) return -1;
    fprintf(f, "%s\n", stamp);
    return fclose(f) == 0 ? 0 : -1;
}

/* State codes as defined in sections 3.1.h and 3.2.f of the statement. An
   event that is already in the past reports 0 even if it also sold out. */
int event_state(const Event *ev) {
    char now[EVENT_DATE_LEN + 1];

    if (ev->closed) return EVENT_CLOSED;
    now_datetime(now);
    if (date_cmp(ev->event_date, now) < 0) return EVENT_PAST;
    if (ev->seats_reserved >= ev->attendance_size) return EVENT_SOLD_OUT;
    return EVENT_OPEN;
}

static int cmp_event_eid(const void *a, const void *b) {
    int x = ((const Event *)a)->eid, y = ((const Event *)b)->eid;
    return (x > y) - (x < y);
}

/* Loads every event whose EID appears in `dir`, whose entries are named either
   "NNN" (event directories) or "NNN.txt" (owner index entries). */
static int event_list_from_dir(const char *dir, Event **out, int *count) {
    DIR *d;
    struct dirent *e;
    Event *list = NULL;
    int n = 0, cap = 0;

    *out = NULL;
    *count = 0;

    d = opendir(dir);
    if (!d) return 0;

    while ((e = readdir(d)) != NULL) {
        char digits[4];
        char *end;
        int eid;
        Event ev;

        if (strlen(e->d_name) < 3 || !isdigit((unsigned char)e->d_name[0])) continue;
        memcpy(digits, e->d_name, 3);
        digits[3] = '\0';
        eid = (int)strtol(digits, &end, 10);
        if (*end != '\0' || !valid_eid(eid)) continue;
        if (event_load(eid, &ev) == -1) continue;

        if (n == cap) {
            Event *grown;
            int new_cap = cap ? cap * 2 : 16;
            grown = realloc(list, (size_t)new_cap * sizeof *list);
            if (!grown) { free(list); closedir(d); return -1; }
            list = grown;
            cap = new_cap;
        }
        list[n++] = ev;
    }
    closedir(d);

    if (n > 1) qsort(list, (size_t)n, sizeof *list, cmp_event_eid);
    *out = list;
    *count = n;
    return 0;
}

int event_list_all(Event **out, int *count) {
    return event_list_from_dir(EVENTS_DIR, out, count);
}

int event_list_by_owner(const char *uid, Event **out, int *count) {
    char dir[PATH_MAX];

    *out = NULL;
    *count = 0;
    if (!valid_uid(uid)) return -1;
    if (snprintf(dir, sizeof dir, "%s/%s/CREATED", USERS_DIR, uid) >= (int)sizeof dir) return -1;
    return event_list_from_dir(dir, out, count);
}

/* ========================================================================
 * Reservations
 * ==================================================================== */

/* Reservation files are named after their instant so that a plain descending
   sort by file name yields the most recent reservations first. The trailing
   sequence number disambiguates reservations made within the same second. */
static int reservation_filename(const Reservation *r, const char *dir, char *out, size_t cap) {
    char stamp[16];
    int seq;

    /* dd-mm-yyyy hh:mm:ss -> yyyymmdd_hhmmss */
    if (strlen(r->timestamp) < TIMESTAMP_LEN) return -1;
    snprintf(stamp, sizeof stamp, "%.4s%.2s%.2s_%.2s%.2s%.2s",
             r->timestamp + 6, r->timestamp + 3, r->timestamp + 0,
             r->timestamp + 11, r->timestamp + 14, r->timestamp + 17);

    for (seq = 0; seq < 1000; seq++) {
        char probe[PATH_MAX];
        if (snprintf(out, cap, "R-%s-%s-%03d.txt", r->uid, stamp, seq) >= (int)cap) return -1;
        if (snprintf(probe, sizeof probe, "%s/%s", dir, out) >= (int)sizeof probe) return -1;
        if (access(probe, F_OK) != 0) return 0;
    }
    return -1;
}

int reservation_add(const Reservation *r) {
    char event_dir[PATH_MAX], user_dir[PATH_MAX], name[128], path[PATH_MAX];
    FILE *f;

    if (!r || !valid_uid(r->uid) || !valid_eid(r->eid)) return -1;

    if (snprintf(event_dir, sizeof event_dir, "%s/%03d/RESERVATIONS", EVENTS_DIR, r->eid) >= (int)sizeof event_dir)
        return -1;
    if (snprintf(user_dir, sizeof user_dir, "%s/%s/RESERVED", USERS_DIR, r->uid) >= (int)sizeof user_dir)
        return -1;
    ensure_dir(event_dir);
    ensure_dir(user_dir);

    if (reservation_filename(r, user_dir, name, sizeof name) == -1) return -1;

    /* The user's copy is the one LMR reads, so it is written first. */
    if (snprintf(path, sizeof path, "%s/%s", user_dir, name) >= (int)sizeof path) return -1;
    f = fopen(path, "w");
    if (!f) return -1;
    fprintf(f, "%s %03d %d %s\n", r->uid, r->eid, r->people, r->timestamp);
    if (fclose(f) != 0) return -1;

    if (snprintf(path, sizeof path, "%s/%s", event_dir, name) < (int)sizeof path) {
        f = fopen(path, "w");
        if (f) {
            fprintf(f, "%s %03d %d %s\n", r->uid, r->eid, r->people, r->timestamp);
            fclose(f);
        }
    }
    return 0;
}

static int cmp_name_desc(const void *a, const void *b) {
    return strcmp(*(char *const *)b, *(char *const *)a);
}

int reservation_list_by_user(const char *uid, Reservation **out, int *count, int limit) {
    char dir[PATH_MAX];
    DIR *d;
    struct dirent *e;
    char **names = NULL;
    int total = 0, cap = 0, wanted, i, kept = 0;
    Reservation *list = NULL;

    *out = NULL;
    *count = 0;
    if (!valid_uid(uid) || limit <= 0) return -1;
    if (snprintf(dir, sizeof dir, "%s/%s/RESERVED", USERS_DIR, uid) >= (int)sizeof dir) return -1;

    d = opendir(dir);
    if (!d) return 0;                       /* the user never reserved anything */

    /* Collect the file names first: they encode the instant of the
       reservation, so sorting them is enough to find the most recent ones. */
    while ((e = readdir(d)) != NULL) {
        char *copy;
        if (e->d_name[0] != 'R') continue;
        if (total == cap) {
            int new_cap = cap ? cap * 2 : 32;
            char **grown = realloc(names, (size_t)new_cap * sizeof *names);
            if (!grown) { closedir(d); goto out_of_memory; }
            names = grown;
            cap = new_cap;
        }
        copy = strdup(e->d_name);
        if (!copy) { closedir(d); goto out_of_memory; }
        names[total++] = copy;
    }
    closedir(d);

    if (total == 0) { free(names); return 0; }
    if (total > 1) qsort(names, (size_t)total, sizeof *names, cmp_name_desc);

    wanted = total < limit ? total : limit;
    list = malloc((size_t)wanted * sizeof *list);
    if (!list) goto out_of_memory;

    for (i = 0; i < wanted; i++) {
        char path[PATH_MAX], uid_buf[32], stamp[64];
        int eid, people;
        FILE *f;

        if (snprintf(path, sizeof path, "%s/%s", dir, names[i]) >= (int)sizeof path) continue;
        f = fopen(path, "r");
        if (!f) continue;
        if (fscanf(f, "%31s %d %d %63[^\n]", uid_buf, &eid, &people, stamp) == 4) {
            memset(&list[kept], 0, sizeof list[kept]);
            snprintf(list[kept].uid, sizeof list[kept].uid, "%s", uid_buf);
            snprintf(list[kept].timestamp, sizeof list[kept].timestamp, "%s", stamp);
            list[kept].eid = eid;
            list[kept].people = people;
            kept++;
        }
        fclose(f);
    }

    for (i = 0; i < total; i++) free(names[i]);
    free(names);

    if (kept == 0) { free(list); return 0; }
    *out = list;
    *count = kept;
    return 0;

out_of_memory:
    for (i = 0; i < total; i++) free(names[i]);
    free(names);
    free(list);
    return -1;
}

/* ========================================================================
 * Upload staging
 * ==================================================================== */

int storage_stage_open(char *path, size_t cap) {
    static unsigned counter = 0;

    if (snprintf(path, cap, "%s/upload-%ld-%u.tmp", STAGE_DIR, (long)getpid(), counter++) >= (int)cap)
        return -1;
    return open(path, O_WRONLY | O_CREAT | O_TRUNC | O_EXCL, 0600);
}
