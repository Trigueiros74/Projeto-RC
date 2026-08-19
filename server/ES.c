/* ES.c - Event-reservation Server.
 *
 *   ./ES [-p ESport] [-v]
 *
 * The server listens on the same port for UDP and TCP. A select() loop waits
 * on both sockets; datagrams are answered inline, while each TCP connection is
 * handed to a forked child so that a 10 MB upload never keeps the next request
 * waiting. The processes share nothing but the ESDIR tree, and every handler
 * brackets its work with the storage lock (see storage.c).
 */

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include "commands.h"
#include "parser_server.h"
#include "storage.h"

#define GROUP_NUMBER      68
#define DEFAULT_PORT      "58068"          /* 58000 + GROUP_NUMBER */
#define PORT_MAX_LEN      6
#define TCP_BACKLOG       64
#define TCP_TIMEOUT_SEC   15               /* a stalled peer must not pin a child */
#define UDP_DATAGRAM_MAX  8192
#define DRAIN_BUDGET      (1L << 20)       /* bytes read from a bad peer before giving up */
#define USAGE             "Usage: ./ES [-p ESport] [-v]\n"

static volatile sig_atomic_t stop_requested = 0;

static void on_terminate(int sig) {
    (void)sig;
    stop_requested = 1;
}

/* ========================================================================
 * Buffered reader over a TCP socket
 *
 * Reading a request one byte at a time costs one syscall per byte, which for a
 * 10 MB attachment is 10 million of them. The reader below fills a buffer and
 * serves fields and bulk data out of it.
 * ==================================================================== */

typedef struct {
    int    fd;
    char   buf[65536];
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

/* Reads one field, which ends at a space or at the newline that closes the
   message. `delim` receives the character that terminated it ('\0' at end of
   stream). Returns 0 on success and -1 when the field overflows `cap` or the
   connection breaks. */
static int reader_field(Reader *r, char *out, size_t cap, char *delim) {
    size_t len = 0;
    *delim = '\0';

    for (;;) {
        char c;
        int rc = reader_getc(r, &c);
        if (rc == -1) return -1;
        if (rc == 0) break;                       /* end of stream */
        if (c == ' ' || c == '\n') { *delim = c; break; }
        if (len + 1 >= cap) return -1;            /* field longer than allowed */
        out[len++] = c;
    }
    out[len] = '\0';
    return 0;
}

/* Copies exactly `count` bytes to `out_fd`; pass -1 to discard them. */
static int reader_drain(Reader *r, int out_fd, long count) {
    while (count > 0) {
        size_t avail, take;
        if (reader_fill(r) != 1) return -1;
        avail = r->len - r->pos;
        take = (long)avail < count ? avail : (size_t)count;
        if (out_fd != -1 && write_all(out_fd, r->buf + r->pos, take) == -1) return -1;
        r->pos += take;
        count -= (long)take;
    }
    return 0;
}

/* Consumes whatever the peer still has to say, so that the reply is not lost
   to a connection reset. Bounded, because the peer may be misbehaving. */
static void reader_discard_rest(Reader *r) {
    long budget = DRAIN_BUDGET;
    while (budget > 0) {
        size_t avail;
        if (reader_fill(r) != 1) return;
        avail = r->len - r->pos;
        r->pos = r->len;
        budget -= (long)avail;
    }
}

/* ========================================================================
 * Verbose logging
 * ==================================================================== */

static void describe_peer(const struct sockaddr *addr, socklen_t addrlen,
                          char *host, size_t host_cap, char *port, size_t port_cap) {
    if (getnameinfo(addr, addrlen, host, (socklen_t)host_cap, port, (socklen_t)port_cap,
                    NI_NUMERICHOST | NI_NUMERICSERV) != 0) {
        snprintf(host, host_cap, "?");
        snprintf(port, port_cap, "?");
    }
}

static void log_request(int verbose, const char *transport, const char *host, const char *port,
                        const char *command, const char *uid, const char *status) {
    if (!verbose) return;
    printf("[%s %s:%s] %s uid=%s -> %s\n", transport, host, port, command,
           uid && *uid ? uid : "-", status);
    fflush(stdout);
}

/* Answers a TCP request with a bare status line and records it. */
static void reply_and_log(int fd, const char *prefix, const char *status, int verbose,
                          const char *host, const char *port, const char *command,
                          const char *uid) {
    char line[32];
    int n = snprintf(line, sizeof line, "%s %s\n", prefix, status);
    write_all(fd, line, (size_t)n);
    log_request(verbose, "TCP", host, port, command, uid, status);
}

/* ========================================================================
 * UDP
 * ==================================================================== */

static void serve_udp(int fd_udp, int verbose) {
    char datagram[UDP_DATAGRAM_MAX];
    char reply[UDP_REPLY_MAX];
    char command[8] = "";
    char uid[UID_LEN + 1] = "";
    char password[PASSWORD_LEN + 1] = "";
    char host[NI_MAXHOST], port[NI_MAXSERV];
    struct sockaddr_in peer;
    socklen_t peerlen = sizeof peer;
    ssize_t received;
    size_t reply_len;
    size_t command_len;

    received = recvfrom(fd_udp, datagram, sizeof datagram - 1, 0,
                        (struct sockaddr *)&peer, &peerlen);
    if (received == -1) {
        if (errno != EINTR && errno != EAGAIN && errno != EWOULDBLOCK)
            fprintf(stderr, "ES: recvfrom: %s\n", strerror(errno));
        return;
    }
    datagram[received] = '\0';

    describe_peer((struct sockaddr *)&peer, peerlen, host, sizeof host, port, sizeof port);

    /* The command word is everything up to the first space or newline. */
    command_len = strcspn(datagram, " \n");
    if (command_len == 0 || command_len >= sizeof command) {
        reply_len = (size_t)snprintf(reply, sizeof reply, "ERR\n");
        log_request(verbose, "UDP", host, port, "?", NULL, "ERR");
    } else {
        memcpy(command, datagram, command_len);
        command[command_len] = '\0';

        if (strcmp(command, "LIN") == 0) {
            reply_len = parse_lin(datagram, uid, password)
                            ? (size_t)snprintf(reply, sizeof reply, "RLI ERR\n")
                            : cmd_lin(reply, uid, password);
        } else if (strcmp(command, "LOU") == 0) {
            reply_len = parse_lou(datagram, uid, password)
                            ? (size_t)snprintf(reply, sizeof reply, "RLO ERR\n")
                            : cmd_lou(reply, uid, password);
        } else if (strcmp(command, "UNR") == 0) {
            reply_len = parse_unr(datagram, uid, password)
                            ? (size_t)snprintf(reply, sizeof reply, "RUR ERR\n")
                            : cmd_unr(reply, uid, password);
        } else if (strcmp(command, "LME") == 0) {
            reply_len = parse_lme(datagram, uid, password)
                            ? (size_t)snprintf(reply, sizeof reply, "RME ERR\n")
                            : cmd_lme(reply, uid, password);
        } else if (strcmp(command, "LMR") == 0) {
            reply_len = parse_lmr(datagram, uid, password)
                            ? (size_t)snprintf(reply, sizeof reply, "RMR ERR\n")
                            : cmd_lmr(reply, uid, password);
        } else {
            reply_len = (size_t)snprintf(reply, sizeof reply, "ERR\n");
        }

        if (verbose) {
            char status[16] = "ERR";
            sscanf(reply, "%*s %15s", status);
            log_request(verbose, "UDP", host, port, command, uid, status);
        }
    }

    if (sendto(fd_udp, reply, reply_len, 0, (struct sockaddr *)&peer, peerlen) == -1)
        fprintf(stderr, "ES: sendto: %s\n", strerror(errno));
}

/* ========================================================================
 * TCP
 * ==================================================================== */

/* CRE UID password name event_date attendance_size Fname Fsize Fdata\n
   The attachment is streamed into a staging file as it arrives, so the server
   holds at most one buffer's worth of it in memory whatever its size. */
static void serve_cre(Reader *r, int fd, int verbose, const char *host, const char *port) {
    char uid[UID_LEN + 2] = "", password[PASSWORD_LEN + 2] = "";
    char name[EVENT_NAME_MAX + 2] = "", event_date[EVENT_DATE_LEN + 1] = "";
    char fname[FILENAME_MAX_LEN + 2] = "", attendance_str[8] = "", fsize_str[16] = "";
    char staged[1024], status[8] = "ERR";
    int attendance = 0, staged_fd = -1, i;
    long fsize = 0;
    char delim = '\0';

    /* Metadata. A field that is too long, or not followed by a space, is a
       protocol error; we still read on so the reply is not lost to a reset. */
    if (reader_field(r, uid, sizeof uid, &delim) == -1 || delim != ' ') goto malformed;
    if (reader_field(r, password, sizeof password, &delim) == -1 || delim != ' ') goto malformed;
    if (reader_field(r, name, sizeof name, &delim) == -1 || delim != ' ') goto malformed;

    /* event_date is the one field that contains a space, so it is read by
       length rather than by delimiter. */
    for (i = 0; i < EVENT_DATE_LEN; i++) {
        if (reader_getc(r, &event_date[i]) != 1) goto malformed;
    }
    event_date[EVENT_DATE_LEN] = '\0';
    if (reader_getc(r, &delim) != 1 || delim != ' ') goto malformed;

    if (reader_field(r, attendance_str, sizeof attendance_str, &delim) == -1 || delim != ' ') goto malformed;
    if (reader_field(r, fname, sizeof fname, &delim) == -1 || delim != ' ') goto malformed;
    if (reader_field(r, fsize_str, sizeof fsize_str, &delim) == -1 || delim != ' ') goto malformed;

    /* Fsize is checked before a single byte is stored: an oversized or
       nonsensical value must not make the server allocate or write anything. */
    if (!valid_fsize_field(fsize_str, &fsize)) goto malformed;

    staged_fd = storage_stage_open(staged, sizeof staged);
    if (staged_fd == -1) {
        reader_discard_rest(r);
        reply_and_log(fd, "RCE", "NOK", verbose, host, port, "CRE", uid);
        return;
    }
    if (reader_drain(r, staged_fd, fsize) == -1) {   /* peer died mid-upload */
        close(staged_fd);
        unlink(staged);
        return;
    }
    close(staged_fd);
    staged_fd = -1;

    if (reader_getc(r, &delim) != 1 || delim != '\n') { unlink(staged); goto malformed; }

    if (!valid_uid_field(uid) || !valid_password_field(password) ||
        !valid_event_name_field(name) || !valid_event_date_field(event_date) ||
        !valid_attendance_field(attendance_str, &attendance) || !valid_fname_field(fname)) {
        unlink(staged);
        goto malformed;
    }

    cmd_cre(fd, uid, password, name, event_date, attendance, fname, staged, status);
    log_request(verbose, "TCP", host, port, "CRE", uid, status);
    return;

malformed:
    if (staged_fd != -1) close(staged_fd);
    reader_discard_rest(r);
    reply_and_log(fd, "RCE", "ERR", verbose, host, port, "CRE", uid);
}

/* Reads the fields of a plain text request and checks it ends right after the
   last one. Returns 0 when the shape matches. */
static int read_text_fields(Reader *r, char *const out[], const size_t cap[], int n) {
    char delim = '\0';
    int i;

    for (i = 0; i < n; i++) {
        if (reader_field(r, out[i], cap[i], &delim) == -1) return -1;
        if (out[i][0] == '\0') return -1;
        if (delim != (i == n - 1 ? '\n' : ' ')) return -1;
    }
    return 0;
}

static void serve_tcp_connection(int fd, int verbose, const char *host, const char *port) {
    Reader reader;
    char command[8] = "";
    char status[8] = "ERR";
    char delim = '\0';

    reader_init(&reader, fd);

    if (reader_field(&reader, command, sizeof command, &delim) == -1 || command[0] == '\0') {
        reader_discard_rest(&reader);
        write_all(fd, "ERR\n", 4);
        log_request(verbose, "TCP", host, port, "?", NULL, "ERR");
        return;
    }

    /* A known command with the wrong shape is answered with its own ERR
       status, as the protocol requires; only an unknown command word gets the
       bare "ERR". */
    if (strcmp(command, "CRE") == 0) {
        if (delim == ' ') {
            serve_cre(&reader, fd, verbose, host, port);
        } else {
            reader_discard_rest(&reader);
            reply_and_log(fd, "RCE", "ERR", verbose, host, port, "CRE", NULL);
        }
    } else if (strcmp(command, "CLS") == 0) {
        char uid[UID_LEN + 2], password[PASSWORD_LEN + 2], eid[5];
        char *const fields[] = { uid, password, eid };
        const size_t caps[] = { sizeof uid, sizeof password, sizeof eid };

        if (delim == ' ' && read_text_fields(&reader, fields, caps, 3) == 0 &&
            valid_uid_field(uid) && valid_password_field(password) && valid_eid_field(eid)) {
            cmd_cls(fd, uid, password, eid, status);
            log_request(verbose, "TCP", host, port, "CLS", uid, status);
        } else {
            reader_discard_rest(&reader);
            reply_and_log(fd, "RCL", "ERR", verbose, host, port, "CLS", NULL);
        }
    } else if (strcmp(command, "LST") == 0) {
        if (delim == '\n') {
            cmd_lst(fd, status);
            log_request(verbose, "TCP", host, port, "LST", NULL, status);
        } else {
            reader_discard_rest(&reader);
            reply_and_log(fd, "RLS", "ERR", verbose, host, port, "LST", NULL);
        }
    } else if (strcmp(command, "SED") == 0) {
        char eid[5];
        char *const fields[] = { eid };
        const size_t caps[] = { sizeof eid };

        if (delim == ' ' && read_text_fields(&reader, fields, caps, 1) == 0 && valid_eid_field(eid)) {
            cmd_sed(fd, eid, status);
            log_request(verbose, "TCP", host, port, "SED", NULL, status);
        } else {
            reader_discard_rest(&reader);
            reply_and_log(fd, "RSE", "ERR", verbose, host, port, "SED", NULL);
        }
    } else if (strcmp(command, "RID") == 0) {
        char uid[UID_LEN + 2], password[PASSWORD_LEN + 2], eid[5], people_str[8];
        char *const fields[] = { uid, password, eid, people_str };
        const size_t caps[] = { sizeof uid, sizeof password, sizeof eid, sizeof people_str };
        int people = 0;

        if (delim == ' ' && read_text_fields(&reader, fields, caps, 4) == 0 &&
            valid_uid_field(uid) && valid_password_field(password) &&
            valid_eid_field(eid) && valid_people_field(people_str, &people)) {
            cmd_rid(fd, uid, password, eid, people, status);
            log_request(verbose, "TCP", host, port, "RID", uid, status);
        } else {
            reader_discard_rest(&reader);
            reply_and_log(fd, "RRI", "ERR", verbose, host, port, "RID", NULL);
        }
    } else if (strcmp(command, "CPS") == 0) {
        char uid[UID_LEN + 2], oldpass[PASSWORD_LEN + 2], newpass[PASSWORD_LEN + 2];
        char *const fields[] = { uid, oldpass, newpass };
        const size_t caps[] = { sizeof uid, sizeof oldpass, sizeof newpass };

        if (delim == ' ' && read_text_fields(&reader, fields, caps, 3) == 0 &&
            valid_uid_field(uid) && valid_password_field(oldpass) && valid_password_field(newpass)) {
            cmd_cps(fd, uid, oldpass, newpass, status);
            log_request(verbose, "TCP", host, port, "CPS", uid, status);
        } else {
            reader_discard_rest(&reader);
            reply_and_log(fd, "RCP", "ERR", verbose, host, port, "CPS", NULL);
        }
    } else {
        reader_discard_rest(&reader);
        write_all(fd, "ERR\n", 4);
        log_request(verbose, "TCP", host, port, command, NULL, "ERR");
    }
}

/* ========================================================================
 * Set-up
 * ==================================================================== */

static int parse_port(const char *arg, char *out, size_t cap) {
    char *end;
    long value;

    if (!arg || *arg == '\0') return -1;
    value = strtol(arg, &end, 10);
    if (*end != '\0' || value < 1 || value > 65535) return -1;
    return snprintf(out, cap, "%ld", value) < (int)cap ? 0 : -1;
}

static int parse_arguments(int argc, char *argv[], char *port, size_t port_cap, int *verbose) {
    int i;

    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-v") == 0) {
            *verbose = 1;
        } else if (strcmp(argv[i], "-p") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "ES: -p needs a port number.\n" USAGE);
                return -1;
            }
            if (parse_port(argv[++i], port, port_cap) == -1) {
                fprintf(stderr, "ES: '%s' is not a valid port (1-65535).\n" USAGE, argv[i]);
                return -1;
            }
        } else {
            fprintf(stderr, "ES: unexpected argument '%s'.\n" USAGE, argv[i]);
            return -1;
        }
    }
    return 0;
}

/* Creates a bound socket of the requested type on `port`. */
static int bind_socket(const char *port, int socktype, int protocol) {
    struct addrinfo hints, *res = NULL;
    int fd, err, reuse = 1;

    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_INET;
    hints.ai_socktype = socktype;
    hints.ai_flags = AI_PASSIVE;

    err = getaddrinfo(NULL, port, &hints, &res);
    if (err != 0) {
        fprintf(stderr, "ES: getaddrinfo: %s\n", gai_strerror(err));
        return -1;
    }

    fd = socket(res->ai_family, res->ai_socktype, protocol);
    if (fd == -1) {
        fprintf(stderr, "ES: socket: %s\n", strerror(errno));
        freeaddrinfo(res);
        return -1;
    }

    /* Without SO_REUSEADDR a restart fails while old connections linger. */
    (void)setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof reuse);

    if (bind(fd, res->ai_addr, res->ai_addrlen) == -1) {
        fprintf(stderr, "ES: bind on port %s: %s\n", port, strerror(errno));
        close(fd);
        freeaddrinfo(res);
        return -1;
    }

    freeaddrinfo(res);
    return fd;
}

static void install_signal_handlers(void) {
    struct sigaction sa;

    /* A client that closes early must not take the server down with a SIGPIPE
       when the reply is written to the dead socket. */
    signal(SIGPIPE, SIG_IGN);
    /* Finished children are reaped by the kernel instead of piling up. */
    signal(SIGCHLD, SIG_IGN);

    memset(&sa, 0, sizeof sa);
    sa.sa_handler = on_terminate;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);
}

int main(int argc, char *argv[]) {
    char port[PORT_MAX_LEN + 1] = DEFAULT_PORT;
    int verbose = 0;
    int fd_udp = -1, fd_tcp = -1, maxfd;
    fd_set watched;

    if (parse_arguments(argc, argv, port, sizeof port, &verbose) == -1) return 1;

    install_signal_handlers();

    if (storage_init() == -1) {
        fprintf(stderr, "ES: cannot initialise the ESDIR store: %s\n", strerror(errno));
        return 1;
    }

    fd_udp = bind_socket(port, SOCK_DGRAM, IPPROTO_UDP);
    if (fd_udp == -1) return 1;

    fd_tcp = bind_socket(port, SOCK_STREAM, IPPROTO_TCP);
    if (fd_tcp == -1) { close(fd_udp); return 1; }

    if (listen(fd_tcp, TCP_BACKLOG) == -1) {
        fprintf(stderr, "ES: listen: %s\n", strerror(errno));
        close(fd_udp);
        close(fd_tcp);
        return 1;
    }

    printf("ES listening on port %s (UDP and TCP)%s\n", port, verbose ? ", verbose mode" : "");
    fflush(stdout);

    maxfd = (fd_udp > fd_tcp ? fd_udp : fd_tcp) + 1;

    while (!stop_requested) {
        fd_set ready;

        FD_ZERO(&watched);
        FD_SET(fd_udp, &watched);
        FD_SET(fd_tcp, &watched);
        ready = watched;

        if (select(maxfd, &ready, NULL, NULL, NULL) == -1) {
            if (errno == EINTR) continue;          /* a signal, not a failure */
            fprintf(stderr, "ES: select: %s\n", strerror(errno));
            break;
        }

        if (FD_ISSET(fd_udp, &ready)) serve_udp(fd_udp, verbose);

        if (FD_ISSET(fd_tcp, &ready)) {
            struct sockaddr_in peer;
            socklen_t peerlen = sizeof peer;
            char host[NI_MAXHOST], port_str[NI_MAXSERV];
            struct timeval timeout;
            pid_t child;
            int conn = accept(fd_tcp, (struct sockaddr *)&peer, &peerlen);

            if (conn == -1) {
                if (errno != EINTR && errno != ECONNABORTED)
                    fprintf(stderr, "ES: accept: %s\n", strerror(errno));
                continue;
            }

            describe_peer((struct sockaddr *)&peer, peerlen,
                          host, sizeof host, port_str, sizeof port_str);

            child = fork();
            if (child == -1) {
                fprintf(stderr, "ES: fork: %s\n", strerror(errno));
                close(conn);
                continue;
            }

            if (child == 0) {
                /* Child: only the connection matters from here on. */
                close(fd_udp);
                close(fd_tcp);
                storage_after_fork();

                /* A peer that connects and then goes quiet would otherwise
                   hold this process for ever. */
                timeout.tv_sec = TCP_TIMEOUT_SEC;
                timeout.tv_usec = 0;
                (void)setsockopt(conn, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof timeout);
                (void)setsockopt(conn, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof timeout);

                serve_tcp_connection(conn, verbose, host, port_str);
                close(conn);
                _exit(0);
            }

            close(conn);
        }
    }

    printf("\nES shutting down.\n");
    close(fd_udp);
    close(fd_tcp);
    return 0;
}
