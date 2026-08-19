/* user.c - the User application of the Event Reservation platform.
 *
 *   ./user [-n ESIP] [-p ESport]
 *
 * Reads one command per line from the standard input and turns it into the
 * matching protocol request: UDP for the short user-management messages, TCP
 * for everything that moves a file or a listing.
 */

#include <errno.h>
#include <netdb.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>

#include "commands.h"
#include "parser_user.h"

#define GROUP_NUMBER    68
#define DEFAULT_PORT    "58068"       /* 58000 + GROUP_NUMBER */
#define DEFAULT_HOST    "localhost"
#define HOST_MAX_LEN    255           /* a host name, not only a dotted quad */
#define PORT_MAX_LEN    5
#define UDP_TIMEOUT_SEC 5
#define USAGE           "Usage: ./user [-n ESIP] [-p ESport]\n"

/* ========================================================================
 * Start-up
 * ==================================================================== */

static int parse_port(const char *arg, char *out, size_t cap) {
    char *end;
    long value;

    if (!arg || *arg == '\0') return -1;
    value = strtol(arg, &end, 10);
    if (*end != '\0' || value < 1 || value > 65535) return -1;
    return snprintf(out, cap, "%ld", value) < (int)cap ? 0 : -1;
}

static int parse_arguments(int argc, char *argv[], char *host, size_t host_cap,
                           char *port, size_t port_cap) {
    int i;

    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-n") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "user: -n needs an address.\n" USAGE);
                return -1;
            }
            /* Bounded, because a host name is far longer than an IPv4 literal
               and the old fixed 16-byte buffer could not hold one. */
            if (snprintf(host, host_cap, "%s", argv[++i]) >= (int)host_cap) {
                fprintf(stderr, "user: the address is too long.\n");
                return -1;
            }
        } else if (strcmp(argv[i], "-p") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "user: -p needs a port number.\n" USAGE);
                return -1;
            }
            if (parse_port(argv[++i], port, port_cap) == -1) {
                fprintf(stderr, "user: '%s' is not a valid port (1-65535).\n" USAGE, argv[i]);
                return -1;
            }
        } else {
            fprintf(stderr, "user: unexpected argument '%s'.\n" USAGE, argv[i]);
            return -1;
        }
    }
    return 0;
}

static int resolve(const char *host, const char *port, int socktype, struct addrinfo **out) {
    struct addrinfo hints;
    int err;

    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_INET;
    hints.ai_socktype = socktype;

    err = getaddrinfo(host, port, &hints, out);
    if (err != 0) {
        fprintf(stderr, "user: cannot resolve %s:%s: %s\n", host, port, gai_strerror(err));
        return -1;
    }
    return 0;
}

/* ========================================================================
 * Input
 * ==================================================================== */

/* Reads one line. Returns 0 on success, 1 at end of input, and -1 when the
   line was longer than the buffer — in which case the rest of it is dropped so
   that the leftovers are not mistaken for the next command. */
static int read_line(char *line, size_t cap) {
    if (fgets(line, (int)cap, stdin) == NULL) return 1;

    if (strchr(line, '\n') == NULL && !feof(stdin)) {
        int c;
        while ((c = getchar()) != '\n' && c != EOF) { }
        return -1;
    }
    return 0;
}

/* Splits the line into its command word and the rest of the arguments. */
static void split_command(const char *line, char *command, size_t cap, const char **args) {
    size_t len;

    while (*line == ' ' || *line == '\t') line++;
    len = strcspn(line, " \t\n");
    if (len >= cap) len = cap - 1;
    memcpy(command, line, len);
    command[len] = '\0';
    *args = line + strcspn(line, " \t\n");
}

static void print_help(void) {
    printf(
        "Commands:\n"
        "  login <UID> <password>                              register or log in\n"
        "  changePass <oldPassword> <newPassword>              change the password\n"
        "  logout                                              end the session\n"
        "  unregister                                          remove the account\n"
        "  create <name> <file> <dd-mm-yyyy> <hh:mm> <seats>   create an event\n"
        "  close <EID>                                         stop accepting reservations\n"
        "  myevents | mye                                      events you created\n"
        "  list                                                every event\n"
        "  show <EID>                                          details and description file\n"
        "  reserve <EID> <seats>                               book seats\n"
        "  myreservations | myr                                your latest reservations\n"
        "  exit                                                leave the application\n");
}

/* ========================================================================
 * Main loop
 * ==================================================================== */

static int require_login(const Session *s) {
    if (!s->logged_in) {
        fprintf(stderr, "Please login first.\n");
        return 0;
    }
    return 1;
}

static int interact(int fd_udp, struct addrinfo *res_udp, struct addrinfo *res_tcp) {
    Session session;
    char line[INPUT_LINE_MAX];
    char command[32];

    memset(&session, 0, sizeof session);

    for (;;) {
        const char *args;
        int status = read_line(line, sizeof line);

        if (status == 1) {                    /* end of input behaves like `exit` */
            if (session.logged_in)
                fprintf(stderr, "\nStill logged in: run logout before leaving.\n");
            return 0;
        }
        if (status == -1) {
            fprintf(stderr, "Command too long (at most %d characters).\n", INPUT_LINE_MAX - 1);
            continue;
        }

        split_command(line, command, sizeof command, &args);
        if (command[0] == '\0') continue;     /* an empty line is not an error */

        if (strcmp(command, "login") == 0) {
            char uid[UID_LEN + 1], password[PASSWORD_LEN + 1];
            if (session.logged_in) {
                fprintf(stderr, "Already logged in as %s. Run logout first.\n", session.uid);
                continue;
            }
            if (parse_login(args, uid, password) == 0 &&
                cmd_login(&session, fd_udp, res_udp, uid, password) == 1) return 1;
        }
        else if (strcmp(command, "changePass") == 0) {
            char old_password[PASSWORD_LEN + 1], new_password[PASSWORD_LEN + 1];
            if (!require_login(&session)) continue;
            if (parse_change_pass(args, old_password, new_password) == 0 &&
                cmd_change_pass(&session, res_tcp, old_password, new_password) == 1) return 1;
        }
        else if (strcmp(command, "logout") == 0) {
            if (!require_login(&session)) continue;
            if (parse_no_args(args) == 0 && cmd_logout(&session, fd_udp, res_udp) == 1) return 1;
        }
        else if (strcmp(command, "unregister") == 0) {
            if (!require_login(&session)) continue;
            if (parse_no_args(args) == 0 && cmd_unregister(&session, fd_udp, res_udp) == 1) return 1;
        }
        else if (strcmp(command, "create") == 0) {
            char name[EVENT_NAME_MAX + 1], fname[FILENAME_MAX_LEN + 1];
            char event_date[EVENT_DATE_LEN + 1];
            int attendees;
            if (!require_login(&session)) continue;
            if (parse_create(args, name, fname, event_date, &attendees) == 0 &&
                cmd_create(&session, res_tcp, name, fname, event_date, attendees) == 1) return 1;
        }
        else if (strcmp(command, "close") == 0) {
            char eid[EID_LEN + 1];
            if (!require_login(&session)) continue;
            if (parse_eid(args, eid) == 0 && cmd_close(&session, res_tcp, eid) == 1) return 1;
        }
        else if (strcmp(command, "myevents") == 0 || strcmp(command, "mye") == 0) {
            if (!require_login(&session)) continue;
            if (parse_no_args(args) == 0 && cmd_myevents(&session, fd_udp, res_udp) == 1) return 1;
        }
        else if (strcmp(command, "myreservations") == 0 || strcmp(command, "myr") == 0) {
            if (!require_login(&session)) continue;
            if (parse_no_args(args) == 0 && cmd_myreservations(&session, fd_udp, res_udp) == 1) return 1;
        }
        else if (strcmp(command, "reserve") == 0) {
            char eid[EID_LEN + 1];
            int people;
            if (!require_login(&session)) continue;
            if (parse_reserve(args, eid, &people) == 0 &&
                cmd_reserve(&session, res_tcp, eid, people) == 1) return 1;
        }
        else if (strcmp(command, "list") == 0) {
            if (parse_no_args(args) == 0 && cmd_list(res_tcp) == 1) return 1;
        }
        else if (strcmp(command, "show") == 0) {
            char eid[EID_LEN + 1];
            if (parse_eid(args, eid) == 0 && cmd_show(res_tcp, eid) == 1) return 1;
        }
        else if (strcmp(command, "exit") == 0) {
            if (session.logged_in) {
                fprintf(stderr, "Still logged in: run logout before leaving.\n");
                continue;
            }
            if (parse_no_args(args) == 0) return 0;
        }
        else if (strcmp(command, "help") == 0) {
            print_help();
        }
        else {
            fprintf(stderr, "Unknown command '%s'. Type help for the list.\n", command);
        }
    }
}

int main(int argc, char *argv[]) {
    char host[HOST_MAX_LEN + 1] = DEFAULT_HOST;
    char port[PORT_MAX_LEN + 1] = DEFAULT_PORT;
    struct addrinfo *res_udp = NULL, *res_tcp = NULL;
    struct timeval timeout;
    int fd_udp, status;

    if (parse_arguments(argc, argv, host, sizeof host, port, sizeof port) == -1) return 1;

    /* Writing to a socket the server already closed must report an error, not
       kill the application. */
    signal(SIGPIPE, SIG_IGN);

    if (resolve(host, port, SOCK_DGRAM, &res_udp) == -1) return 1;
    if (resolve(host, port, SOCK_STREAM, &res_tcp) == -1) {
        freeaddrinfo(res_udp);
        return 1;
    }

    fd_udp = socket(res_udp->ai_family, res_udp->ai_socktype, res_udp->ai_protocol);
    if (fd_udp == -1) {
        fprintf(stderr, "user: socket: %s\n", strerror(errno));
        freeaddrinfo(res_udp);
        freeaddrinfo(res_tcp);
        return 1;
    }

    /* Datagrams get lost; the timeout is what turns that into a retransmission
       instead of a hang. */
    timeout.tv_sec = UDP_TIMEOUT_SEC;
    timeout.tv_usec = 0;
    if (setsockopt(fd_udp, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof timeout) == -1) {
        fprintf(stderr, "user: setsockopt: %s\n", strerror(errno));
        close(fd_udp);
        freeaddrinfo(res_udp);
        freeaddrinfo(res_tcp);
        return 1;
    }

    status = interact(fd_udp, res_udp, res_tcp);

    close(fd_udp);
    freeaddrinfo(res_udp);
    freeaddrinfo(res_tcp);
    return status;
}
