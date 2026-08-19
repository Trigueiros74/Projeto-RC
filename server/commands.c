/* commands.c - business logic behind every protocol message of the ES server.
 *
 * Each handler takes the storage lock for the duration of its work: shared for
 * the read-only commands (LST, SED, LME, LMR) and exclusive for the ones that
 * change the state (LIN, LOU, UNR, CRE, CLS, RID, CPS). Because handlers run
 * in different processes, that lock is what keeps a reservation from being
 * counted twice or a password from being half written.
 */

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "commands.h"

#define STREAM_CHUNK 65536

/* ========================================================================
 * Socket helpers
 * ==================================================================== */

int write_all(int fd, const char *buf, size_t len) {
    while (len > 0) {
        ssize_t n = write(fd, buf, len);
        if (n == -1) {
            if (errno == EINTR) continue;
            return -1;
        }
        buf += n;
        len -= (size_t)n;
    }
    return 0;
}

/* Sends a short reply such as "RCL OK\n" and records its status word. */
static int reply_status(int fd, const char *command, const char *status, char out[8]) {
    char line[32];
    int n = snprintf(line, sizeof line, "%s %s\n", command, status);
    snprintf(out, 8, "%s", status);
    return write_all(fd, line, (size_t)n);
}

/* ========================================================================
 * UDP handlers
 * ==================================================================== */

static size_t udp_status(char *reply, const char *command, const char *status) {
    return (size_t)snprintf(reply, UDP_REPLY_MAX, "%s %s\n", command, status);
}

/* LIN UID password -> RLI OK | RLI NOK | RLI REG */
size_t cmd_lin(char *reply, const char *uid, const char *password) {
    User u;
    size_t len;

    if (storage_lock(1) == -1) return udp_status(reply, "RLI", "NOK");

    if (user_load(uid, &u) == -1) {
        /* Unknown UID: this is the user's first login, so register them. */
        if (user_register(uid, password) == -1 || user_set_login(uid, 1) == -1)
            len = udp_status(reply, "RLI", "NOK");
        else
            len = udp_status(reply, "RLI", "REG");
    } else if (strcmp(u.password, password) != 0) {
        len = udp_status(reply, "RLI", "NOK");
    } else if (user_set_login(uid, 1) == -1) {
        len = udp_status(reply, "RLI", "NOK");
    } else {
        len = udp_status(reply, "RLI", "OK");
    }

    storage_unlock();
    return len;
}

/* LOU UID password -> RLO OK | RLO NOK | RLO UNR | RLO WRP */
size_t cmd_lou(char *reply, const char *uid, const char *password) {
    User u;
    size_t len;

    if (storage_lock(1) == -1) return udp_status(reply, "RLO", "NOK");

    if (user_load(uid, &u) == -1)                     len = udp_status(reply, "RLO", "UNR");
    else if (strcmp(u.password, password) != 0)       len = udp_status(reply, "RLO", "WRP");
    else if (!u.logged_in)                            len = udp_status(reply, "RLO", "NOK");
    else if (user_set_login(uid, 0) == -1)            len = udp_status(reply, "RLO", "NOK");
    else                                              len = udp_status(reply, "RLO", "OK");

    storage_unlock();
    return len;
}

/* UNR UID password -> RUR OK | RUR NOK | RUR UNR | RUR WRP */
size_t cmd_unr(char *reply, const char *uid, const char *password) {
    User u;
    size_t len;

    if (storage_lock(1) == -1) return udp_status(reply, "RUR", "NOK");

    if (user_load(uid, &u) == -1)                     len = udp_status(reply, "RUR", "UNR");
    else if (strcmp(u.password, password) != 0)       len = udp_status(reply, "RUR", "WRP");
    else if (!u.logged_in)                            len = udp_status(reply, "RUR", "NOK");
    else if (user_unregister(uid) == -1)              len = udp_status(reply, "RUR", "NOK");
    else                                              len = udp_status(reply, "RUR", "OK");

    storage_unlock();
    return len;
}

/* LME UID password -> RME status[ EID state]* */
size_t cmd_lme(char *reply, const char *uid, const char *password) {
    User u;
    Event *events = NULL;
    int count = 0, i;
    size_t len;

    if (storage_lock(0) == -1) return udp_status(reply, "RME", "NOK");

    if (user_load(uid, &u) == -1)                { len = udp_status(reply, "RME", "NOK"); goto done; }
    if (strcmp(u.password, password) != 0)       { len = udp_status(reply, "RME", "WRP"); goto done; }
    if (!u.logged_in)                            { len = udp_status(reply, "RME", "NLG"); goto done; }

    if (event_list_by_owner(uid, &events, &count) == -1 || count == 0) {
        len = udp_status(reply, "RME", "NOK");
        goto done;
    }

    len = (size_t)snprintf(reply, UDP_REPLY_MAX, "RME OK");
    for (i = 0; i < count; i++) {
        int written = snprintf(reply + len, UDP_REPLY_MAX - len, " %03d %d",
                               events[i].eid, event_state(&events[i]));
        if (written < 0 || (size_t)written >= UDP_REPLY_MAX - len - 1) break;
        len += (size_t)written;
    }
    reply[len++] = '\n';
    reply[len] = '\0';

done:
    free(events);
    storage_unlock();
    return len;
}

/* LMR UID password -> RMR status[ EID date value]*, newest 50 reservations */
size_t cmd_lmr(char *reply, const char *uid, const char *password) {
    User u;
    Reservation *list = NULL;
    int count = 0, i;
    size_t len;

    if (storage_lock(0) == -1) return udp_status(reply, "RMR", "NOK");

    if (user_load(uid, &u) == -1)                { len = udp_status(reply, "RMR", "NOK"); goto done; }
    if (strcmp(u.password, password) != 0)       { len = udp_status(reply, "RMR", "WRP"); goto done; }
    if (!u.logged_in)                            { len = udp_status(reply, "RMR", "NLG"); goto done; }

    if (reservation_list_by_user(uid, &list, &count, RESERVATIONS_LISTED_MAX) == -1 || count == 0) {
        len = udp_status(reply, "RMR", "NOK");
        goto done;
    }

    len = (size_t)snprintf(reply, UDP_REPLY_MAX, "RMR OK");
    for (i = 0; i < count; i++) {
        int written = snprintf(reply + len, UDP_REPLY_MAX - len, " %03d %s %d",
                               list[i].eid, list[i].timestamp, list[i].people);
        if (written < 0 || (size_t)written >= UDP_REPLY_MAX - len - 1) break;
        len += (size_t)written;
    }
    reply[len++] = '\n';
    reply[len] = '\0';

done:
    free(list);
    storage_unlock();
    return len;
}

/* ========================================================================
 * TCP handlers
 * ==================================================================== */

/* CRE ... -> RCE OK EID | RCE NOK | RCE NLG | RCE WRP
   `staged_path` is the temporary file the upload was streamed into; it is
   moved into the event directory on success and removed otherwise. */
int cmd_cre(int fd, const char *uid, const char *password, const char *name,
            const char *event_date, int attendance_size, const char *fname,
            const char *staged_path, char status[8]) {
    User u;
    Event ev;
    char now[EVENT_DATE_LEN + 1], line[32];
    int eid, rc, n;

    if (storage_lock(1) == -1) {
        unlink(staged_path);
        return reply_status(fd, "RCE", "NOK", status);
    }

    if (user_load(uid, &u) == -1)          { rc = reply_status(fd, "RCE", "NLG", status); goto done; }
    if (strcmp(u.password, password) != 0) { rc = reply_status(fd, "RCE", "WRP", status); goto done; }
    if (!u.logged_in)                      { rc = reply_status(fd, "RCE", "NLG", status); goto done; }

    /* An event cannot be scheduled for an instant that already passed. */
    now_datetime(now);
    if (date_cmp(event_date, now) < 0)     { rc = reply_status(fd, "RCE", "NOK", status); goto done; }

    eid = event_next_eid();
    if (eid == -1)                         { rc = reply_status(fd, "RCE", "NOK", status); goto done; }

    memset(&ev, 0, sizeof ev);
    ev.eid = eid;
    snprintf(ev.owner, sizeof ev.owner, "%s", uid);
    snprintf(ev.name, sizeof ev.name, "%s", name);
    snprintf(ev.event_date, sizeof ev.event_date, "%s", event_date);
    snprintf(ev.fname, sizeof ev.fname, "%s", fname);
    ev.attendance_size = attendance_size;
    ev.seats_reserved = 0;

    if (event_create(&ev, staged_path) == -1) { rc = reply_status(fd, "RCE", "NOK", status); goto done; }

    snprintf(status, 8, "OK");
    n = snprintf(line, sizeof line, "RCE OK %03d\n", eid);
    rc = write_all(fd, line, (size_t)n);
    storage_unlock();
    return rc;

done:
    unlink(staged_path);                  /* the upload was refused */
    storage_unlock();
    return rc;
}

/* CLS UID password EID -> RCL OK | NOK | NLG | NOE | EOW | SLD | PST | CLO */
int cmd_cls(int fd, const char *uid, const char *password, const char *eidstr, char status[8]) {
    User u;
    Event ev;
    char now[EVENT_DATE_LEN + 1];
    int rc;

    if (storage_lock(1) == -1) return reply_status(fd, "RCL", "NOK", status);

    if (user_load(uid, &u) == -1)          { rc = reply_status(fd, "RCL", "NOK", status); goto done; }
    if (strcmp(u.password, password) != 0) { rc = reply_status(fd, "RCL", "NOK", status); goto done; }
    if (!u.logged_in)                      { rc = reply_status(fd, "RCL", "NLG", status); goto done; }

    if (event_load(atoi(eidstr), &ev) == -1) { rc = reply_status(fd, "RCL", "NOE", status); goto done; }
    if (strcmp(ev.owner, uid) != 0)          { rc = reply_status(fd, "RCL", "EOW", status); goto done; }
    if (ev.closed)                           { rc = reply_status(fd, "RCL", "CLO", status); goto done; }

    now_datetime(now);
    if (date_cmp(ev.event_date, now) < 0)    { rc = reply_status(fd, "RCL", "PST", status); goto done; }
    if (ev.seats_reserved >= ev.attendance_size) { rc = reply_status(fd, "RCL", "SLD", status); goto done; }

    rc = (event_close(ev.eid) == -1) ? reply_status(fd, "RCL", "NOK", status)
                                     : reply_status(fd, "RCL", "OK", status);

done:
    storage_unlock();
    return rc;
}

/* LST -> RLS NOK | RLS OK[ EID name state event_date]* */
int cmd_lst(int fd, char status[8]) {
    Event *events = NULL;
    int count = 0, i, rc;
    char *out = NULL;
    size_t len = 0, cap;

    if (storage_lock(0) == -1) return reply_status(fd, "RLS", "NOK", status);

    if (event_list_all(&events, &count) == -1) { rc = reply_status(fd, "RLS", "NOK", status); goto done; }
    if (count == 0)                            { rc = reply_status(fd, "RLS", "NOK", status); goto done; }

    /* "RLS OK" + at most 999 records of " EID name state date" + "\n". */
    cap = 8 + (size_t)count * (EVENT_NAME_MAX + EVENT_DATE_LEN + 12) + 2;
    out = malloc(cap);
    if (!out) { rc = reply_status(fd, "RLS", "NOK", status); goto done; }

    len = (size_t)snprintf(out, cap, "RLS OK");
    for (i = 0; i < count; i++) {
        int written = snprintf(out + len, cap - len, " %03d %s %d %s",
                               events[i].eid, events[i].name, event_state(&events[i]),
                               events[i].event_date);
        /* `cap` is sized for the worst case, so this never trips; the guard is
           here so that a future change to the record format cannot turn into
           an overflow of the newline below. */
        if (written < 0 || (size_t)written >= cap - len - 1) break;
        len += (size_t)written;
    }
    out[len++] = '\n';

    snprintf(status, 8, "OK");
    rc = write_all(fd, out, len);

done:
    free(out);
    free(events);
    storage_unlock();
    return rc;
}

/* SED EID -> RSE NOK | RSE OK UID name date size reserved Fname Fsize Fdata
   The description file is streamed straight from disk, so a 10 MB attachment
   never has to fit in a buffer. */
int cmd_sed(int fd, const char *eidstr, char status[8]) {
    Event ev;
    char path[1024], header[256];
    int desc_fd = -1, rc, hlen;
    long remaining;

    if (storage_lock(0) == -1) return reply_status(fd, "RSE", "NOK", status);

    if (event_load(atoi(eidstr), &ev) == -1)              { rc = reply_status(fd, "RSE", "NOK", status); goto done; }
    if (event_description_path(&ev, path, sizeof path))   { rc = reply_status(fd, "RSE", "NOK", status); goto done; }

    desc_fd = open(path, O_RDONLY);
    if (desc_fd == -1)                                    { rc = reply_status(fd, "RSE", "NOK", status); goto done; }

    hlen = snprintf(header, sizeof header, "RSE OK %s %s %s %d %d %s %ld ",
                    ev.owner, ev.name, ev.event_date, ev.attendance_size,
                    ev.seats_reserved, ev.fname, ev.fsize);
    rc = write_all(fd, header, (size_t)hlen);
    if (rc == -1) goto done;

    remaining = ev.fsize;
    while (remaining > 0) {
        char chunk[STREAM_CHUNK];
        size_t want = remaining < (long)sizeof chunk ? (size_t)remaining : sizeof chunk;
        ssize_t n = read(desc_fd, chunk, want);
        if (n == -1) {
            if (errno == EINTR) continue;
            rc = -1;
            goto done;
        }
        if (n == 0) break;                   /* the file shrank underneath us */
        if (write_all(fd, chunk, (size_t)n) == -1) { rc = -1; goto done; }
        remaining -= n;
    }

    snprintf(status, 8, "OK");
    rc = write_all(fd, "\n", 1);

done:
    if (desc_fd != -1) close(desc_fd);
    storage_unlock();
    return rc;
}

/* RID UID password EID people -> RRI ACC | REJ n | CLS | SLD | PST | NLG | WRP | NOK */
int cmd_rid(int fd, const char *uid, const char *password, const char *eidstr, int people,
            char status[8]) {
    User u;
    Event ev;
    Reservation r;
    char now[EVENT_DATE_LEN + 1], line[32];
    int remaining, rc;

    if (storage_lock(1) == -1) return reply_status(fd, "RRI", "NOK", status);

    if (user_load(uid, &u) == -1)            { rc = reply_status(fd, "RRI", "NOK", status); goto done; }
    if (strcmp(u.password, password) != 0)   { rc = reply_status(fd, "RRI", "WRP", status); goto done; }
    if (!u.logged_in)                        { rc = reply_status(fd, "RRI", "NLG", status); goto done; }

    if (event_load(atoi(eidstr), &ev) == -1) { rc = reply_status(fd, "RRI", "NOK", status); goto done; }
    if (ev.closed)                           { rc = reply_status(fd, "RRI", "CLS", status); goto done; }

    now_datetime(now);
    if (date_cmp(ev.event_date, now) < 0)    { rc = reply_status(fd, "RRI", "PST", status); goto done; }

    remaining = ev.attendance_size - ev.seats_reserved;
    if (remaining <= 0)                      { rc = reply_status(fd, "RRI", "SLD", status); goto done; }
    if (people > remaining) {
        int n = snprintf(line, sizeof line, "RRI REJ %d\n", remaining);
        snprintf(status, 8, "REJ");
        rc = write_all(fd, line, (size_t)n);
        goto done;
    }

    memset(&r, 0, sizeof r);
    snprintf(r.uid, sizeof r.uid, "%s", uid);
    r.eid = ev.eid;
    r.people = people;
    now_timestamp(r.timestamp);

    /* Seat count and reservation record must land together; the exclusive lock
       is what makes the pair atomic with respect to the other handlers. */
    if (reservation_add(&r) == -1 ||
        event_set_reserved(ev.eid, ev.seats_reserved + people) == -1) {
        rc = reply_status(fd, "RRI", "NOK", status);
        goto done;
    }

    rc = reply_status(fd, "RRI", "ACC", status);

done:
    storage_unlock();
    return rc;
}

/* CPS UID oldPassword newPassword -> RCP OK | RCP NID | RCP NLG | RCP NOK */
int cmd_cps(int fd, const char *uid, const char *oldpass, const char *newpass, char status[8]) {
    User u;
    int rc;

    if (storage_lock(1) == -1) return reply_status(fd, "RCP", "NOK", status);

    if (user_load(uid, &u) == -1)          { rc = reply_status(fd, "RCP", "NID", status); goto done; }
    if (!u.logged_in)                      { rc = reply_status(fd, "RCP", "NLG", status); goto done; }
    if (strcmp(u.password, oldpass) != 0)  { rc = reply_status(fd, "RCP", "NOK", status); goto done; }

    rc = (user_set_password(uid, newpass) == -1) ? reply_status(fd, "RCP", "NOK", status)
                                                 : reply_status(fd, "RCP", "OK", status);

done:
    storage_unlock();
    return rc;
}
