/* commands.c - the User side of the two protocols.
 *
 * UDP requests are small and are retransmitted a few times before the client
 * gives up. TCP requests may carry a file of up to 10 MB in either direction,
 * so both the upload and the download are streamed: neither the file being
 * created nor the one being shown is ever held in memory in full.
 */

#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>

#include "commands.h"

#define UDP_ATTEMPTS     3
#define UDP_TIMEOUT_SEC  5
#define TCP_TIMEOUT_SEC  15
#define UDP_REPLY_MAX    8192
#define STREAM_CHUNK     65536

#define ERR_SERVER  "Cannot reach the server. Ending session.\n"
#define ERR_REPLY   "The server sent a reply that does not follow the protocol. Ending session.\n"

/* ========================================================================
 * Low level I/O
 * ==================================================================== */

static int write_all(int fd, const char *buf, size_t len) {
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

/* ---- buffered reader over a TCP socket ---- */

typedef struct {
    int    fd;
    char   buf[STREAM_CHUNK];
    size_t len;
    size_t pos;
} Reader;

static void reader_init(Reader *r, int fd) {
    r->fd = fd;
    r->len = 0;
    r->pos = 0;
}

/* 1 = more data, 0 = end of stream, -1 = error. */
static int reader_fill(Reader *r) {
    ssize_t n;
    if (r->pos < r->len) return 1;
    do {
        n = read(r->fd, r->buf, sizeof r->buf);
    } while (n == -1 && errno == EINTR);
    if (n <= 0) return n == 0 ? 0 : -1;
    r->pos = 0;
    r->len = (size_t)n;
    return 1;
}

static int reader_getc(Reader *r, char *c) {
    int rc = reader_fill(r);
    if (rc != 1) return rc;
    *c = r->buf[r->pos++];
    return 1;
}

/* Reads one field, ending at a space or at the newline that closes the reply.
   `delim` receives the terminator ('\0' at end of stream). */
static int reader_field(Reader *r, char *out, size_t cap, char *delim) {
    size_t len = 0;
    *delim = '\0';

    for (;;) {
        char c;
        int rc = reader_getc(r, &c);
        if (rc == -1) return -1;
        if (rc == 0) break;
        if (c == ' ' || c == '\n') { *delim = c; break; }
        if (len + 1 >= cap) return -1;
        out[len++] = c;
    }
    out[len] = '\0';
    return 0;
}

/* Copies exactly `count` bytes to `out_fd`. */
static int reader_copy(Reader *r, int out_fd, long count) {
    while (count > 0) {
        size_t avail, take;
        if (reader_fill(r) != 1) return -1;
        avail = r->len - r->pos;
        take = (long)avail < count ? avail : (size_t)count;
        if (write_all(out_fd, r->buf + r->pos, take) == -1) return -1;
        r->pos += take;
        count -= (long)take;
    }
    return 0;
}

/* ---- connection set-up ---- */

static int tcp_connect(struct addrinfo *res) {
    struct timeval timeout;
    int fd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);

    if (fd == -1) return -1;

    /* A server that stops replying must not hang the prompt for ever. */
    timeout.tv_sec = TCP_TIMEOUT_SEC;
    timeout.tv_usec = 0;
    (void)setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof timeout);
    (void)setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof timeout);

    if (connect(fd, res->ai_addr, res->ai_addrlen) == -1) {
        close(fd);
        return -1;
    }
    return fd;
}

/* Sends a datagram and waits for the answer, retransmitting when the reply
   does not arrive in time. Returns the reply length, or -1. */
static ssize_t udp_exchange(int fd_udp, struct addrinfo *res, const char *request,
                            char *reply, size_t reply_cap) {
    int attempt;

    for (attempt = 0; attempt < UDP_ATTEMPTS; attempt++) {
        ssize_t received;

        if (sendto(fd_udp, request, strlen(request), 0, res->ai_addr, res->ai_addrlen) == -1)
            return -1;

        received = recvfrom(fd_udp, reply, reply_cap - 1, 0, NULL, NULL);
        if (received >= 0) {
            reply[received] = '\0';
            return received;
        }
        if (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR)
            return -1;                        /* a real error, not a timeout */
    }
    return -1;
}

/* Splits "RXX STATUS rest…\n" into its command word, its status and whatever
   follows. Returns 0 when the reply has at least those two words. */
static int split_reply(char *reply, char **command, char **status, char **rest) {
    char *p = reply;
    size_t len = strlen(reply);

    if (len == 0 || reply[len - 1] != '\n') return -1;
    reply[len - 1] = '\0';                    /* drop the terminator */

    *command = p;
    p = strchr(p, ' ');
    if (!p) return -1;
    *p++ = '\0';

    *status = p;
    p = strchr(p, ' ');
    if (p) { *p++ = '\0'; *rest = p; }
    else   { *rest = NULL; }

    return 0;
}

/* ========================================================================
 * UDP commands
 * ==================================================================== */

int cmd_login(Session *s, int fd_udp, struct addrinfo *res, const char *uid, const char *password) {
    char request[64], reply[UDP_REPLY_MAX];
    char *command, *status, *rest;

    snprintf(request, sizeof request, "LIN %s %s\n", uid, password);

    if (udp_exchange(fd_udp, res, request, reply, sizeof reply) < 0) {
        fprintf(stderr, ERR_SERVER);
        return 1;
    }
    if (split_reply(reply, &command, &status, &rest) != 0 || strcmp(command, "RLI") != 0 || rest) {
        fprintf(stderr, ERR_REPLY);
        return 1;
    }

    if (strcmp(status, "OK") == 0 || strcmp(status, "REG") == 0) {
        snprintf(s->uid, sizeof s->uid, "%s", uid);
        snprintf(s->password, sizeof s->password, "%s", password);
        s->logged_in = 1;
        printf(strcmp(status, "OK") == 0 ? "Login successful. UID: %s\n"
                                         : "New user registered. UID: %s\n", s->uid);
    } else if (strcmp(status, "NOK") == 0) {
        printf("Incorrect login attempt.\n");
    } else if (strcmp(status, "ERR") == 0) {
        fprintf(stderr, "The server rejected the login request.\n");
    } else {
        fprintf(stderr, ERR_REPLY);
        return 1;
    }
    return 0;
}

int cmd_logout(Session *s, int fd_udp, struct addrinfo *res) {
    char request[64], reply[UDP_REPLY_MAX];
    char *command, *status, *rest;

    snprintf(request, sizeof request, "LOU %s %s\n", s->uid, s->password);

    if (udp_exchange(fd_udp, res, request, reply, sizeof reply) < 0) {
        fprintf(stderr, ERR_SERVER);
        return 1;
    }
    if (split_reply(reply, &command, &status, &rest) != 0 || strcmp(command, "RLO") != 0 || rest) {
        fprintf(stderr, ERR_REPLY);
        return 1;
    }

    if (strcmp(status, "OK") == 0) {
        printf("Logout successful.\n");
        memset(s, 0, sizeof *s);
    } else if (strcmp(status, "NOK") == 0) {
        printf("User not logged in.\n");
        memset(s, 0, sizeof *s);              /* the server disagrees; follow it */
    } else if (strcmp(status, "UNR") == 0) {
        printf("User not registered.\n");
        memset(s, 0, sizeof *s);
    } else if (strcmp(status, "WRP") == 0) {
        printf("Incorrect password.\n");
    } else if (strcmp(status, "ERR") == 0) {
        fprintf(stderr, "The server rejected the logout request.\n");
    } else {
        fprintf(stderr, ERR_REPLY);
        return 1;
    }
    return 0;
}

int cmd_unregister(Session *s, int fd_udp, struct addrinfo *res) {
    char request[64], reply[UDP_REPLY_MAX];
    char *command, *status, *rest;

    snprintf(request, sizeof request, "UNR %s %s\n", s->uid, s->password);

    if (udp_exchange(fd_udp, res, request, reply, sizeof reply) < 0) {
        fprintf(stderr, ERR_SERVER);
        return 1;
    }
    if (split_reply(reply, &command, &status, &rest) != 0 || strcmp(command, "RUR") != 0 || rest) {
        fprintf(stderr, ERR_REPLY);
        return 1;
    }

    if (strcmp(status, "OK") == 0) {
        printf("Unregister successful. You are now logged out.\n");
        memset(s, 0, sizeof *s);
    } else if (strcmp(status, "NOK") == 0) {
        printf("User not logged in.\n");
    } else if (strcmp(status, "UNR") == 0) {
        printf("User not registered.\n");
        memset(s, 0, sizeof *s);
    } else if (strcmp(status, "WRP") == 0) {
        printf("Incorrect password.\n");
    } else if (strcmp(status, "ERR") == 0) {
        fprintf(stderr, "The server rejected the unregister request.\n");
    } else {
        fprintf(stderr, ERR_REPLY);
        return 1;
    }
    return 0;
}

static const char *event_state_text(int state) {
    switch (state) {
        case 0:  return "past";
        case 1:  return "open";
        case 2:  return "sold out";
        case 3:  return "closed";
        default: return "unknown";
    }
}

/* RME OK[ EID state]* */
int cmd_myevents(const Session *s, int fd_udp, struct addrinfo *res) {
    char request[64], reply[UDP_REPLY_MAX];
    char *command, *status, *rest;

    snprintf(request, sizeof request, "LME %s %s\n", s->uid, s->password);

    if (udp_exchange(fd_udp, res, request, reply, sizeof reply) < 0) {
        fprintf(stderr, ERR_SERVER);
        return 1;
    }
    if (split_reply(reply, &command, &status, &rest) != 0 || strcmp(command, "RME") != 0) {
        fprintf(stderr, ERR_REPLY);
        return 1;
    }

    if (strcmp(status, "OK") == 0) {
        char eid[16];
        int state, consumed, listed = 0;

        printf("Events created by %s:\n", s->uid);
        while (rest && *rest && sscanf(rest, "%15s %d%n", eid, &state, &consumed) == 2) {
            printf("  %s  %s\n", eid, event_state_text(state));
            rest += consumed;
            listed++;
        }
        if (listed == 0) {
            fprintf(stderr, ERR_REPLY);
            return 1;
        }
    } else if (strcmp(status, "NOK") == 0) {
        printf("You have not created any events.\n");
    } else if (strcmp(status, "NLG") == 0) {
        printf("User not logged in.\n");
    } else if (strcmp(status, "WRP") == 0) {
        printf("Incorrect password.\n");
    } else if (strcmp(status, "ERR") == 0) {
        fprintf(stderr, "The server rejected the myevents request.\n");
    } else {
        fprintf(stderr, ERR_REPLY);
        return 1;
    }
    return 0;
}

/* RMR OK[ EID dd-mm-yyyy hh:mm:ss value]* */
int cmd_myreservations(const Session *s, int fd_udp, struct addrinfo *res) {
    char request[64], reply[UDP_REPLY_MAX];
    char *command, *status, *rest;

    snprintf(request, sizeof request, "LMR %s %s\n", s->uid, s->password);

    if (udp_exchange(fd_udp, res, request, reply, sizeof reply) < 0) {
        fprintf(stderr, ERR_SERVER);
        return 1;
    }
    if (split_reply(reply, &command, &status, &rest) != 0 || strcmp(command, "RMR") != 0) {
        fprintf(stderr, ERR_REPLY);
        return 1;
    }

    if (strcmp(status, "OK") == 0) {
        char eid[16], date[16], time_of_day[16];
        int seats, consumed, listed = 0;

        printf("Reservations made by %s (most recent first):\n", s->uid);
        /* The timestamp is two space-separated words, hence the four fields. */
        while (rest && *rest &&
               sscanf(rest, "%15s %15s %15s %d%n", eid, date, time_of_day, &seats, &consumed) == 4) {
            printf("  event %s  %s %s  %d seat%s\n", eid, date, time_of_day, seats,
                   seats == 1 ? "" : "s");
            rest += consumed;
            listed++;
        }
        if (listed == 0) {
            fprintf(stderr, ERR_REPLY);
            return 1;
        }
    } else if (strcmp(status, "NOK") == 0) {
        printf("You have not made any reservations.\n");
    } else if (strcmp(status, "NLG") == 0) {
        printf("User not logged in.\n");
    } else if (strcmp(status, "WRP") == 0) {
        printf("Incorrect password.\n");
    } else if (strcmp(status, "ERR") == 0) {
        fprintf(stderr, "The server rejected the myreservations request.\n");
    } else {
        fprintf(stderr, ERR_REPLY);
        return 1;
    }
    return 0;
}

/* ========================================================================
 * TCP commands
 * ==================================================================== */

/* Sends `request`, then reads the single status line the server answers with.
   `reply` receives the whole line, terminator included. */
static int tcp_status_exchange(struct addrinfo *res, const char *request,
                               char *reply, size_t reply_cap) {
    Reader reader;
    size_t len = 0;
    int fd = tcp_connect(res);

    if (fd == -1) return -1;

    if (write_all(fd, request, strlen(request)) == -1) { close(fd); return -1; }
    shutdown(fd, SHUT_WR);                    /* tell the server the request is over */

    reader_init(&reader, fd);
    for (;;) {
        char c;
        int rc = reader_getc(&reader, &c);
        if (rc == -1) { close(fd); return -1; }
        if (rc == 0) break;
        if (len + 1 >= reply_cap) { close(fd); return -1; }
        reply[len++] = c;
        if (c == '\n') break;
    }
    reply[len] = '\0';

    close(fd);
    return len > 0 ? 0 : -1;
}

int cmd_change_pass(Session *s, struct addrinfo *res, const char *old_password,
                    const char *new_password) {
    char request[64], reply[256];
    char *command, *status, *rest;

    snprintf(request, sizeof request, "CPS %s %s %s\n", s->uid, old_password, new_password);

    if (tcp_status_exchange(res, request, reply, sizeof reply) != 0) {
        fprintf(stderr, ERR_SERVER);
        return 1;
    }
    if (split_reply(reply, &command, &status, &rest) != 0 || strcmp(command, "RCP") != 0 || rest) {
        fprintf(stderr, ERR_REPLY);
        return 1;
    }

    if (strcmp(status, "OK") == 0) {
        /* Only now does the cached password change, so a refused request
           cannot leave the client signing requests with the wrong one. */
        snprintf(s->password, sizeof s->password, "%s", new_password);
        printf("Password changed successfully.\n");
    } else if (strcmp(status, "NID") == 0) {
        printf("User does not exist.\n");
    } else if (strcmp(status, "NLG") == 0) {
        printf("User not logged in.\n");
    } else if (strcmp(status, "NOK") == 0) {
        printf("Incorrect password.\n");
    } else if (strcmp(status, "ERR") == 0) {
        fprintf(stderr, "The server rejected the changePass request.\n");
    } else {
        fprintf(stderr, ERR_REPLY);
        return 1;
    }
    return 0;
}

/* CRE UID password name event_date attendance Fname Fsize Fdata
   The file is copied from disk to the socket in chunks, so creating an event
   with a 10 MB attachment costs one buffer, not ten megabytes. */
int cmd_create(const Session *s, struct addrinfo *res, const char *name, const char *fname,
               const char *event_date, int attendees) {
    char header[256], reply[256];
    char *command, *status, *rest;
    struct stat st;
    Reader reader;
    int file_fd = -1, fd = -1, hlen;
    long remaining;
    size_t len = 0;

    file_fd = open(fname, O_RDONLY);
    if (file_fd == -1) {
        fprintf(stderr, "Cannot open '%s': %s\n", fname, strerror(errno));
        return 0;
    }
    if (fstat(file_fd, &st) == -1 || !S_ISREG(st.st_mode)) {
        fprintf(stderr, "'%s' is not a regular file.\n", fname);
        close(file_fd);
        return 0;
    }
    if ((long)st.st_size > FDATA_MAX_SIZE) {
        fprintf(stderr, "'%s' is %ld bytes; the protocol allows at most %ld.\n",
                fname, (long)st.st_size, FDATA_MAX_SIZE);
        close(file_fd);
        return 0;
    }

    fd = tcp_connect(res);
    if (fd == -1) {
        close(file_fd);
        fprintf(stderr, ERR_SERVER);
        return 1;
    }

    hlen = snprintf(header, sizeof header, "CRE %s %s %s %s %d %s %ld ",
                    s->uid, s->password, name, event_date, attendees, fname, (long)st.st_size);
    if (write_all(fd, header, (size_t)hlen) == -1) goto transport_error;

    remaining = (long)st.st_size;
    while (remaining > 0) {
        char chunk[STREAM_CHUNK];
        size_t want = remaining < (long)sizeof chunk ? (size_t)remaining : sizeof chunk;
        ssize_t n = read(file_fd, chunk, want);
        if (n == -1) {
            if (errno == EINTR) continue;
            goto transport_error;
        }
        if (n == 0) break;                    /* the file shrank while we read it */
        if (write_all(fd, chunk, (size_t)n) == -1) goto transport_error;
        remaining -= n;
    }
    if (remaining != 0) goto transport_error;

    if (write_all(fd, "\n", 1) == -1) goto transport_error;
    close(file_fd);
    file_fd = -1;
    shutdown(fd, SHUT_WR);

    reader_init(&reader, fd);
    for (;;) {
        char c;
        int rc = reader_getc(&reader, &c);
        if (rc == -1) goto transport_error;
        if (rc == 0) break;
        if (len + 1 >= sizeof reply) goto transport_error;
        reply[len++] = c;
        if (c == '\n') break;
    }
    reply[len] = '\0';
    close(fd);
    fd = -1;

    if (split_reply(reply, &command, &status, &rest) != 0 || strcmp(command, "RCE") != 0) {
        fprintf(stderr, ERR_REPLY);
        return 1;
    }

    if (strcmp(status, "OK") == 0) {
        if (!rest || strlen(rest) != 3) {
            fprintf(stderr, ERR_REPLY);
            return 1;
        }
        printf("Event created. EID: %s\n", rest);
    } else if (strcmp(status, "NOK") == 0) {
        printf("The event could not be created.\n");
    } else if (strcmp(status, "NLG") == 0) {
        printf("User not logged in.\n");
    } else if (strcmp(status, "WRP") == 0) {
        printf("Incorrect password.\n");
    } else if (strcmp(status, "ERR") == 0) {
        fprintf(stderr, "The server rejected the create request.\n");
    } else {
        fprintf(stderr, ERR_REPLY);
        return 1;
    }
    return 0;

transport_error:
    if (file_fd != -1) close(file_fd);
    if (fd != -1) close(fd);
    fprintf(stderr, ERR_SERVER);
    return 1;
}

int cmd_close(const Session *s, struct addrinfo *res, const char *eid) {
    char request[64], reply[256];
    char *command, *status, *rest;

    snprintf(request, sizeof request, "CLS %s %s %s\n", s->uid, s->password, eid);

    if (tcp_status_exchange(res, request, reply, sizeof reply) != 0) {
        fprintf(stderr, ERR_SERVER);
        return 1;
    }
    if (split_reply(reply, &command, &status, &rest) != 0 || strcmp(command, "RCL") != 0 || rest) {
        fprintf(stderr, ERR_REPLY);
        return 1;
    }

    if      (strcmp(status, "OK")  == 0) printf("Event %s closed. No more reservations are accepted.\n", eid);
    else if (strcmp(status, "NOK") == 0) printf("Unknown user or incorrect password.\n");
    else if (strcmp(status, "NLG") == 0) printf("User not logged in.\n");
    else if (strcmp(status, "NOE") == 0) printf("Event %s does not exist.\n", eid);
    else if (strcmp(status, "EOW") == 0) printf("Event %s belongs to another user.\n", eid);
    else if (strcmp(status, "SLD") == 0) printf("Event %s is already sold out.\n", eid);
    else if (strcmp(status, "PST") == 0) printf("Event %s has already taken place.\n", eid);
    else if (strcmp(status, "CLO") == 0) printf("Event %s was already closed.\n", eid);
    else if (strcmp(status, "ERR") == 0) fprintf(stderr, "The server rejected the close request.\n");
    else {
        fprintf(stderr, ERR_REPLY);
        return 1;
    }
    return 0;
}

/* RLS OK[ EID name state event_date]*, where event_date is two words. */
int cmd_list(struct addrinfo *res) {
    char reply[UDP_REPLY_MAX * 8];             /* 999 records of ~40 bytes */
    char *command, *status, *rest;

    if (tcp_status_exchange(res, "LST\n", reply, sizeof reply) != 0) {
        fprintf(stderr, ERR_SERVER);
        return 1;
    }
    if (split_reply(reply, &command, &status, &rest) != 0 || strcmp(command, "RLS") != 0) {
        fprintf(stderr, ERR_REPLY);
        return 1;
    }

    if (strcmp(status, "OK") == 0) {
        char eid[16], name[64], date[16], time_of_day[16];
        int state, consumed, listed = 0;

        printf("Events:\n");
        while (rest && *rest &&
               sscanf(rest, "%15s %63s %d %15s %15s%n",
                      eid, name, &state, date, time_of_day, &consumed) == 5) {
            printf("  %s  %-10s  %s %s  (%s)\n", eid, name, date, time_of_day,
                   event_state_text(state));
            rest += consumed;
            listed++;
        }
        if (listed == 0) {
            fprintf(stderr, ERR_REPLY);
            return 1;
        }
    } else if (strcmp(status, "NOK") == 0) {
        printf("No events have been created yet.\n");
    } else if (strcmp(status, "ERR") == 0) {
        fprintf(stderr, "The server rejected the list request.\n");
    } else {
        fprintf(stderr, ERR_REPLY);
        return 1;
    }
    return 0;
}

/* SED EID -> RSE OK UID name event_date attendance reserved Fname Fsize Fdata
   The attachment is written to disk as it arrives, which is what lets `show`
   handle a 10 MB image without a 10 MB buffer — and, because it is written
   with write() and not fprintf("%s"), without stopping at the first zero byte
   of a PNG or a JPEG. */
int cmd_show(struct addrinfo *res, const char *eid) {
    char request[32];
    char command[8], status[8], uid[UID_LEN + 2], name[EVENT_NAME_MAX + 2];
    char event_date[EVENT_DATE_LEN + 1], attendance[8], reserved[8];
    char fname[FILENAME_MAX_LEN + 2], fsize_str[16];
    char cwd[1024];
    Reader reader;
    char delim = '\0';
    char *end;
    long fsize;
    int fd, out_fd = -1, i;

    snprintf(request, sizeof request, "SED %s\n", eid);

    fd = tcp_connect(res);
    if (fd == -1) { fprintf(stderr, ERR_SERVER); return 1; }

    if (write_all(fd, request, strlen(request)) == -1) { close(fd); fprintf(stderr, ERR_SERVER); return 1; }
    shutdown(fd, SHUT_WR);

    reader_init(&reader, fd);

    if (reader_field(&reader, command, sizeof command, &delim) == -1 || strcmp(command, "RSE") != 0)
        goto bad_reply;
    if (reader_field(&reader, status, sizeof status, &delim) == -1) goto bad_reply;

    if (strcmp(status, "OK") != 0) {
        close(fd);
        if      (strcmp(status, "NOK") == 0) printf("Event %s does not exist or has no file to send.\n", eid);
        else if (strcmp(status, "ERR") == 0) fprintf(stderr, "The server rejected the show request.\n");
        else { fprintf(stderr, ERR_REPLY); return 1; }
        return 0;
    }
    if (delim != ' ') goto bad_reply;

    if (reader_field(&reader, uid, sizeof uid, &delim) == -1 || delim != ' ') goto bad_reply;
    if (reader_field(&reader, name, sizeof name, &delim) == -1 || delim != ' ') goto bad_reply;

    /* event_date is the one field holding a space, so it is read by length. */
    for (i = 0; i < EVENT_DATE_LEN; i++)
        if (reader_getc(&reader, &event_date[i]) != 1) goto bad_reply;
    event_date[EVENT_DATE_LEN] = '\0';
    if (reader_getc(&reader, &delim) != 1 || delim != ' ') goto bad_reply;

    if (reader_field(&reader, attendance, sizeof attendance, &delim) == -1 || delim != ' ') goto bad_reply;
    if (reader_field(&reader, reserved, sizeof reserved, &delim) == -1 || delim != ' ') goto bad_reply;
    if (reader_field(&reader, fname, sizeof fname, &delim) == -1 || delim != ' ') goto bad_reply;
    if (reader_field(&reader, fsize_str, sizeof fsize_str, &delim) == -1 || delim != ' ') goto bad_reply;

    /* The file name decides where the client writes, so it is checked against
       the protocol before it is used, not after. */
    if (!valid_fname(fname)) goto bad_reply;

    fsize = strtol(fsize_str, &end, 10);
    if (*end != '\0' || fsize < 0 || fsize > FDATA_MAX_SIZE) goto bad_reply;

    out_fd = open(fname, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (out_fd == -1) {
        fprintf(stderr, "Cannot write '%s': %s\n", fname, strerror(errno));
        close(fd);
        return 0;
    }
    if (reader_copy(&reader, out_fd, fsize) == -1) {
        close(out_fd);
        unlink(fname);
        goto bad_reply;
    }
    close(out_fd);
    out_fd = -1;

    if (reader_getc(&reader, &delim) != 1 || delim != '\n') {
        unlink(fname);
        goto bad_reply;
    }
    close(fd);

    printf("Event %s\n", eid);
    printf("  name       %s\n", name);
    printf("  owner      %s\n", uid);
    printf("  date       %s\n", event_date);
    printf("  seats      %s reserved out of %s\n", reserved, attendance);
    printf("  file       %s (%ld bytes)\n", fname, fsize);
    if (getcwd(cwd, sizeof cwd) != NULL)
        printf("  stored in  %s\n", cwd);
    return 0;

bad_reply:
    if (out_fd != -1) close(out_fd);
    close(fd);
    fprintf(stderr, ERR_REPLY);
    return 1;
}

int cmd_reserve(const Session *s, struct addrinfo *res, const char *eid, int people) {
    char request[64], reply[256];
    char *command, *status, *rest;

    snprintf(request, sizeof request, "RID %s %s %s %d\n", s->uid, s->password, eid, people);

    if (tcp_status_exchange(res, request, reply, sizeof reply) != 0) {
        fprintf(stderr, ERR_SERVER);
        return 1;
    }
    if (split_reply(reply, &command, &status, &rest) != 0 || strcmp(command, "RRI") != 0) {
        fprintf(stderr, ERR_REPLY);
        return 1;
    }

    if (strcmp(status, "ACC") == 0) {
        printf("Reservation accepted: %d seat%s in event %s.\n", people, people == 1 ? "" : "s", eid);
    } else if (strcmp(status, "REJ") == 0) {
        char *end;
        long remaining = rest ? strtol(rest, &end, 10) : -1;
        if (!rest || *end != '\0' || remaining < 0) {
            fprintf(stderr, ERR_REPLY);
            return 1;
        }
        printf("Reservation refused: only %ld seat%s left in event %s.\n",
               remaining, remaining == 1 ? "" : "s", eid);
    }
    else if (strcmp(status, "CLS") == 0) printf("Event %s is closed and no longer accepts reservations.\n", eid);
    else if (strcmp(status, "SLD") == 0) printf("Event %s is sold out.\n", eid);
    else if (strcmp(status, "PST") == 0) printf("Event %s has already taken place.\n", eid);
    else if (strcmp(status, "NLG") == 0) printf("User not logged in.\n");
    else if (strcmp(status, "WRP") == 0) printf("Incorrect password.\n");
    else if (strcmp(status, "NOK") == 0) printf("Event %s is not available.\n", eid);
    else if (strcmp(status, "ERR") == 0) fprintf(stderr, "The server rejected the reserve request.\n");
    else {
        fprintf(stderr, ERR_REPLY);
        return 1;
    }
    return 0;
}
