/* commands.c - simple in-memory implementations of the ES command handlers
 *
 * Matches the server code's expected signatures:
 *  - UDP: int cmd_xxx(char *response, ...); fills response and returns 0 on success, 1 on fatal error.
 *  - TCP: int cmd_xxx(char **response_buf, size_t *response_len, ...); allocates response_buf (malloc),
 *         sets response_len and returns 0 on success, nonzero on error.
 *
 * NOTE: includes basic memory limits tuned for testing. Not production-grade.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "commands.h"

static User users[MAX_USERS];
static Event events[MAX_EVENTS];
static Reservation reservations[MAX_RESERVATIONS];
static int next_eid = 1;

/* Helpers */
static User *find_user(const char *uid) {
    for (int i = 0; i < MAX_USERS; ++i) {
        if (users[i].used && strcmp(users[i].uid, uid) == 0) return &users[i];
    }
    return NULL;
}

static Event *find_event_by_eid_str(const char *eidstr) {
    if (!eidstr) return NULL;
    if (strlen(eidstr) != 3) return NULL;
    int eid = atoi(eidstr);
    if (eid <= 0) return NULL;
    for (int i = 0; i < MAX_EVENTS; ++i) {
        if (events[i].used && events[i].eid == eid) return &events[i];
    }
    return NULL;
}

static Event *create_event_slot(void) {
    for (int i = 0; i < MAX_EVENTS; ++i) {
        if (!events[i].used) {
            events[i].used = 1;
            events[i].fdata = NULL;
            events[i].seats_reserved = 0;
            events[i].closed = 0;
            events[i].eid = next_eid++;
            if (next_eid > 999) next_eid = 1; /* wrap-around (very simple) */
            return &events[i];
        }
    }
    return NULL;
}

static void free_event(Event *e) {
    if (!e) return;
    if (e->fdata) {
        free(e->fdata);
        e->fdata = NULL;
    }
    memset(e, 0, sizeof(Event));
}

static Reservation *create_reservation_slot(void) {
    for (int i = 0; i < MAX_RESERVATIONS; ++i) {
        if (!reservations[i].used) {
            reservations[i].used = 1;
            return &reservations[i];
        }
    }
    return NULL;
}

/* Basic lexicographic date comparator (dd-mm-yyyy). Returns:
   -1 if date1 < date2 (date1 in past), 0 if equal, 1 if date1 > date2.
   This is implemented by reformatting to yyyy-mm-dd and comparing strings.
*/
static int cmp_date(const char *d1, const char *d2) {
    if (!d1 || !d2) return 0;
    char b1[11], b2[11];
    if (strlen(d1) != 10 || strlen(d2) != 10) return 0;
    /* convert dd-mm-yyyy -> yyyy-mm-dd in temp buffers */
    snprintf(b1, sizeof b1, "%.4s-%.2s-%.2s", d1+6, d1+3, d1+0);
    snprintf(b2, sizeof b2, "%.4s-%.2s-%.2s", d2+6, d2+3, d2+0);
    int cmp = strcmp(b1, b2);
    if (cmp < 0) return -1;
    if (cmp > 0) return 1;
    return 0;
}

/* For quick tests we set "current date" to a fixed value (dd-mm-yyyy).
    You can change this constant to simulate dates in the past/future for events.
*/
static const char *CURRENT_DATE = "21-11-2025"; /* dd-mm-yyyy */

/* ---------- UDP command handlers ---------- */

/* LIN UID password -> replies: RLI OK | RLI NOK | RLI REG */
int cmd_lin(char *response, const char *uid, const char *password) {
    if (!response || !uid || !password) return 1;
    User *u = find_user(uid);
    if (!u) {
        /* register new user and log in */
        for (int i = 0; i < MAX_USERS; ++i) {
            if (!users[i].used) {
                users[i].used = 1;
                strncpy(users[i].uid, uid, 6); users[i].uid[6] = '\0';
                strncpy(users[i].password, password, sizeof(users[i].password)-1);
                users[i].logged_in = 1;
                sprintf(response, "RLI REG\n");
                return 0;
            }
        }
        sprintf(response, "RLI NOK\n"); /* no space for new users */
        return 0;
    }
    /* user exists */
    if (strcmp(u->password, password) != 0) {
        sprintf(response, "RLI NOK\n");
        return 0;
    }
    u->logged_in = 1;
    sprintf(response, "RLI OK\n");
    return 0;
}

/* LOU UID password -> RLO OK | RLO NOK | RLO UNR | RLO WRP */
int cmd_lou(char *response, const char *uid, const char *password) {
    if (!response || !uid || !password) return 1;
    User *u = find_user(uid);
    if (!u) {
        sprintf(response, "RLO UNR\n");
        return 0;
    }
    if (strcmp(u->password, password) != 0) {
        sprintf(response, "RLO WRP\n");
        return 0;
    }
    if (u->logged_in) {
        u->logged_in = 0;
        sprintf(response, "RLO OK\n");
    } else {
        sprintf(response, "RLO NOK\n");
    }
    return 0;
}

/* UNR UID password -> RUR OK | RUR NOK | RUR UNR | RUR WRP */
int cmd_unr(char *response, const char *uid, const char *password) {
    if (!response || !uid || !password) return 1;
    User *u = find_user(uid);
    if (!u) {
        sprintf(response, "RUR UNR\n");
        return 0;
    }
    if (strcmp(u->password, password) != 0) {
        sprintf(response, "RUR WRP\n");
        return 0;
    }
    if (!u->logged_in) {
        sprintf(response, "RUR NOK\n");
        return 0;
    }
    /* unregister user: remove user and any reservations they have (do not delete events they created) */
    for (int i = 0; i < MAX_USERS; ++i) {
        if (users[i].used && strcmp(users[i].uid, uid) == 0) {
            users[i].used = 0;
            break;
        }
    }
    /* remove reservations by this user */
    for (int i = 0; i < MAX_RESERVATIONS; ++i) {
        if (reservations[i].used && strcmp(reservations[i].uid, uid) == 0) {
            Event *ev = NULL;
            for (int j = 0; j < MAX_EVENTS; ++j) if (events[j].used && events[j].eid == reservations[i].eid) ev = &events[j];
            if (ev) ev->seats_reserved -= reservations[i].people;
            reservations[i].used = 0;
        }
    }
    sprintf(response, "RUR OK\n");
    return 0;
}

/* LME UID password -> RME status [EID state]* */
int cmd_lme(char *response, const char *uid, const char *password) {
    if (!response || !uid || !password) return 1;
    User *u = find_user(uid);
    if (!u) {
        sprintf(response, "RME NOK\n");
        return 0;
    }
    if (strcmp(u->password, password) != 0) {
        sprintf(response, "RME WRP\n");
        return 0;
    }
    if (!u->logged_in) {
        sprintf(response, "RME NLG\n");
        return 0;
    }
    /* collect events owned by this user */
    char tmp[2048];
    tmp[0] = '\0';
    int count = 0;
    for (int i = 0; i < MAX_EVENTS; ++i) {
        if (events[i].used && strcmp(events[i].owner, uid) == 0) {
            int state = 1; /* default future open */
            if (events[i].closed) state = 3;
            else if (events[i].seats_reserved >= events[i].attendance_size) state = 2;
            else {
                /* compare date to current */
                if (cmp_date(events[i].event_date, CURRENT_DATE) < 0) state = 0;
                else state = 1;
            }
            char part[64];
            snprintf(part, sizeof part, " %03d %d", events[i].eid, state);
            strncat(tmp, part, sizeof tmp - strlen(tmp) - 1);
            count++;
        }
    }
    if (count == 0) {
        sprintf(response, "RME NOK\n");
        return 0;
    }
    snprintf(response, 2048, "RME OK%s\n", tmp);
    return 0;
}

/* LMR UID password -> RMR status [EID date value]* */
int cmd_lmr(char *response, const char *uid, const char *password) {
    if (!response || !uid || !password) return 1;
    User *u = find_user(uid);
    if (!u) {
        sprintf(response, "RMR NOK\n");
        return 0;
    }
    if (strcmp(u->password, password) != 0) {
        sprintf(response, "RMR WRP\n");
        return 0;
    }
    if (!u->logged_in) {
        sprintf(response, "RMR NLG\n");
        return 0;
    }
    char tmp[2048];
    tmp[0] = '\0';
    int count = 0;
    for (int i = 0; i < MAX_RESERVATIONS; ++i) {
        if (reservations[i].used && strcmp(reservations[i].uid, uid) == 0) {
            Event *ev = NULL;
            for (int j = 0; j < MAX_EVENTS; ++j) if (events[j].used && events[j].eid == reservations[i].eid) ev = &events[j];
            if (ev) {
                char part[128];
                snprintf(part, sizeof part, " %03d %s %d", ev->eid, ev->event_date, reservations[i].people);
                strncat(tmp, part, sizeof tmp - strlen(tmp) -1);
                count++;
            }
        }
    }
    if (count == 0) {
        sprintf(response, "RMR NOK\n");
        return 0;
    }
    snprintf(response, 2048, "RMR OK%s\n", tmp);
    return 0;
}

/* ---------- TCP command handlers ---------- */

/* cmd_cre: creates event, stores file on server, returns "RCE OK EID\n" or error codes.
   signature: int cmd_cre(char **response_buf, size_t *response_len, const char *uid, const char *password,
                         const char *name, const char *event_date, int attendance_size,
                         const char *fname, int fsize, const char *fdata);
*/
int cmd_cre(char **response_buf, size_t *response_len, const char *uid, const char *password,
            const char *name, const char *event_date, int attendance_size, const char *fname,
            int fsize, const char *fdata)
{
    if (!response_buf || !response_len || !uid || !password || !name || !event_date || !fname) return 1;

    User *u = find_user(uid);
    if (!u) {
        /* not registered or bad user -> NLG? Spec: if user not logged in reply NLG. For not registered, WRP? We'll reply NLG for simplicity */
        *response_buf = strdup("RCE NLG\n");
        *response_len = strlen(*response_buf);
        return 0;
    }
    if (strcmp(u->password, password) != 0) {
        *response_buf = strdup("RCE WRP\n");
        *response_len = strlen(*response_buf);
        return 0;
    }
    if (!u->logged_in) {
        *response_buf = strdup("RCE NLG\n");
        *response_len = strlen(*response_buf);
        return 0;
    }

    /* reject events with dates in the past: per protocol return NOK (creation failed) */
    if (cmp_date(event_date, CURRENT_DATE) < 0) {
        *response_buf = strdup("RCE NOK\n");
        *response_len = strlen(*response_buf);
        return 0;
    }

    /* create event slot */
    Event *ev = create_event_slot();
    if (!ev) {
        *response_buf = strdup("RCE NOK\n");
        *response_len = strlen(*response_buf);
        return 0;
    }
    ev->eid = ev->eid; /* assigned in create_event_slot */
    strncpy(ev->owner, uid, 6); ev->owner[6] = '\0';
    strncpy(ev->name, name, sizeof ev->name - 1);
    strncpy(ev->event_date, event_date, sizeof ev->event_date - 1);
    ev->attendance_size = attendance_size;
    ev->seats_reserved = 0;
    strncpy(ev->fname, fname, sizeof ev->fname - 1);
    ev->fsize = fsize;
    if (fsize > 0) {
        if (fsize > MAX_FDATA_SIZE) {
            free_event(ev);
            *response_buf = strdup("RCE NOK\n");
            *response_len = strlen(*response_buf);
            return 0;
        }
        ev->fdata = malloc(fsize);
        if (!ev->fdata) {
            free_event(ev);
            *response_buf = strdup("RCE NOK\n");
            *response_len = strlen(*response_buf);
            return 0;
        }
        memcpy(ev->fdata, fdata, fsize);
    } else {
        ev->fdata = NULL;
    }

    /* success: return OK + EID as 3 digits */
    char tmp[64];
    snprintf(tmp, sizeof tmp, "RCE OK %03d\n", ev->eid);
    *response_buf = strdup(tmp);
    *response_len = strlen(tmp);
    return 0;
}

/* cmd_cls: close event
   signature: int cmd_cls(char **response_buf, size_t *response_len, const char *uid, const char *password, const char *eid);
*/
int cmd_cls(char **response_buf, size_t *response_len, const char *uid, const char *password, const char *eidstr) {
    if (!response_buf || !response_len || !uid || !password || !eidstr) return 1;
    User *u = find_user(uid);
    if (!u) {
        *response_buf = strdup("RCL NOK\n");
        *response_len = strlen(*response_buf);
        return 0;
    }
    if (strcmp(u->password, password) != 0) {
        *response_buf = strdup("RCL NOK\n");
        *response_len = strlen(*response_buf);
        return 0;
    }
    if (!u->logged_in) {
        *response_buf = strdup("RCL NLG\n");
        *response_len = strlen(*response_buf);
        return 0;
    }
    Event *ev = find_event_by_eid_str(eidstr);
    if (!ev) {
        *response_buf = strdup("RCL NOE\n");
        *response_len = strlen(*response_buf);
        return 0;
    }
    if (strcmp(ev->owner, uid) != 0) {
        *response_buf = strdup("RCL EOW\n");
        *response_len = strlen(*response_buf);
        return 0;
    }
    /* check if sold out */
    if (ev->seats_reserved >= ev->attendance_size) {
        *response_buf = strdup("RCL SLD\n");
        *response_len = strlen(*response_buf);
        return 0;
    }
    /* check if in past */
    if (cmp_date(ev->event_date, CURRENT_DATE) < 0) {
        *response_buf = strdup("RCL PST\n");
        *response_len = strlen(*response_buf);
        return 0;
    }
    if (ev->closed) {
        *response_buf = strdup("RCL CLO\n");
        *response_len = strlen(*response_buf);
        return 0;
    }
    /* close it */
    ev->closed = 1;
    *response_buf = strdup("RCL OK\n");
    *response_len = strlen(*response_buf);
    return 0;
}

/* cmd_lst: list events
   signature: int cmd_lst(char **response_buf, size_t *response_len);
   reply: RLS status[ EID name state event_date ]*
*/
int cmd_lst(char **response_buf, size_t *response_len) {
    if (!response_buf || !response_len) return 1;
    char tmp[8192];
    tmp[0] = '\0';
    int count = 0;
    for (int i = 0; i < MAX_EVENTS; ++i) {
        if (!events[i].used) continue;
        int state;
        if (events[i].closed) state = 3;
        else if (events[i].seats_reserved >= events[i].attendance_size) state = 2;
        else if (cmp_date(events[i].event_date, CURRENT_DATE) < 0) state = 0;
        else state = 1;
        char part[128];
        snprintf(part, sizeof part, " %03d %s %d %s", events[i].eid, events[i].name, state, events[i].event_date);
        strncat(tmp, part, sizeof tmp - strlen(tmp) - 1);
        count++;
    }
    if (count == 0) {
        *response_buf = strdup("RLS NOK\n");
        *response_len = strlen(*response_buf);
        return 0;
    }
    char header[32];
    snprintf(header, sizeof header, "RLS OK");
    char *out = malloc(strlen(header) + strlen(tmp) + 2);
    if (!out) return 1;
    strcpy(out, header);
    strcat(out, tmp);
    strcat(out, "\n");
    *response_buf = out;
    *response_len = strlen(out);
    return 0;
}

/* cmd_sed: show event + file
   signature: int cmd_sed(char **response_buf, size_t *response_len, const char *eid);
   reply: RSE status [UID name event_date attendance_size Seats_reserved Fname Fsize Fdata]
*/
int cmd_sed(char **response_buf, size_t *response_len, const char *eidstr) {
    if (!response_buf || !response_len || !eidstr) return 1;
    Event *ev = find_event_by_eid_str(eidstr);
    if (!ev) {
        *response_buf = strdup("RSE NOK\n");
        *response_len = strlen(*response_buf);
        return 0;
    }
    /* build header with metadata then append file contents */
    char header[512];
    snprintf(header, sizeof header, "RSE OK %s %s %s %d %d %s %d ",
             ev->owner, ev->name, ev->event_date, ev->attendance_size, ev->seats_reserved, ev->fname, ev->fsize);
    size_t hlen = strlen(header);
    size_t total = hlen + ev->fsize + 2;
    char *out = malloc(total);
    if (!out) return 1;
    memcpy(out, header, hlen);
    if (ev->fsize > 0 && ev->fdata) {
        memcpy(out + hlen, ev->fdata, ev->fsize);
        hlen += ev->fsize;
    }
    out[hlen] = '\n';
    out[hlen+1] = '\0';
    *response_buf = out;
    *response_len = hlen + 1;
    return 0;
}

/* cmd_rid: reserve seats
   signature: int cmd_rid(char **response_buf, size_t *response_len, const char *uid, const char *password, const char *eid, int people)
   replies: RRI ACC | RRI REJ n_seats | RRI CLS | RRI SLD | RRI PST | RRI WRP | RRI NOK
*/
int cmd_rid(char **response_buf, size_t *response_len, const char *uid, const char *password, const char *eidstr, int people) {
    if (!response_buf || !response_len || !uid || !password || !eidstr) return 1;
    User *u = find_user(uid);
    if (!u) {
        *response_buf = strdup("RRI NOK\n");
        *response_len = strlen(*response_buf);
        return 0;
    }
    if (strcmp(u->password, password) != 0) {
        *response_buf = strdup("RRI WRP\n");
        *response_len = strlen(*response_buf);
        return 0;
    }
    if (!u->logged_in) {
        *response_buf = strdup("RRI NLG\n");
        *response_len = strlen(*response_buf);
        return 0;
    }
    Event *ev = find_event_by_eid_str(eidstr);
    if (!ev) {
        *response_buf = strdup("RRI NOK\n");
        *response_len = strlen(*response_buf);
        return 0;
    }
    /* check PST */
    if (cmp_date(ev->event_date, CURRENT_DATE) < 0) {
        *response_buf = strdup("RRI PST\n");
        *response_len = strlen(*response_buf);
        return 0;
    }
    /* closed */
    if (ev->closed) {
        *response_buf = strdup("RRI CLS\n");
        *response_len = strlen(*response_buf);
        return 0;
    }
    int remaining = ev->attendance_size - ev->seats_reserved;
    if (remaining <= 0) {
        *response_buf = strdup("RRI SLD\n");
        *response_len = strlen(*response_buf);
        return 0;
    }
    if (people > remaining) {
        char tmp[64];
        snprintf(tmp, sizeof tmp, "RRI REJ %d\n", remaining);
        *response_buf = strdup(tmp);
        *response_len = strlen(*response_buf);
        return 0;
    }
    /* accept reservation */
    ev->seats_reserved += people;
    Reservation *r = create_reservation_slot();
    if (!r) {
        *response_buf = strdup("RRI NOK\n");
        *response_len = strlen(*response_buf);
        return 0;
    }
    strncpy(r->uid, uid, 6); r->uid[6] = '\0';
    r->eid = ev->eid;
    r->people = people;
    char tmp[64];
    snprintf(tmp, sizeof tmp, "RRI ACC %d\n", ev->seats_reserved);
    *response_buf = strdup(tmp);
    *response_len = strlen(*response_buf);
    return 0;
}

/* cmd_cps: change password
   signature: int cmd_cps(char **response_buf, size_t *response_len, const char *uid, const char *oldpass, const char *newpass)
   replies: RCP OK | RCP NLG | RCP NOK | RCP NID
*/
int cmd_cps(char **response_buf, size_t *response_len, const char *uid, const char *oldpass, const char *newpass) {
    if (!response_buf || !response_len || !uid || !oldpass || !newpass) return 1;
    User *u = find_user(uid);
    if (!u) {
        *response_buf = strdup("RCP NID\n");
        *response_len = strlen(*response_buf);
        return 0;
    }
    if (!u->logged_in) {
        *response_buf = strdup("RCP NLG\n");
        *response_len = strlen(*response_buf);
        return 0;
    }
    if (strcmp(u->password, oldpass) != 0) {
        *response_buf = strdup("RCP NOK\n");
        *response_len = strlen(*response_buf);
        return 0;
    }
    strncpy(u->password, newpass, sizeof(u->password)-1);
    *response_buf = strdup("RCP OK\n");
    *response_len = strlen(*response_buf);
    return 0;
}

