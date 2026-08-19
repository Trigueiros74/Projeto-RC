#ifndef COMMANDS_H
#define COMMANDS_H

#include <netdb.h>

#include "parser_user.h"

/* The credentials of the user currently logged in. The application keeps them
   because every request after `login` has to carry the UID and the password
   again, and the user is not asked to retype them. */
typedef struct {
    char uid[UID_LEN + 1];
    char password[PASSWORD_LEN + 1];
    int  logged_in;
} Session;

/* Every handler returns 0 when the exchange completed — including when the
   server refused the request — and 1 when the session cannot go on, which is
   the case for a broken connection or a reply that violates the protocol. */

/* UDP */
int cmd_login(Session *s, int fd_udp, struct addrinfo *res, const char *uid, const char *password);
int cmd_logout(Session *s, int fd_udp, struct addrinfo *res);
int cmd_unregister(Session *s, int fd_udp, struct addrinfo *res);
int cmd_myevents(const Session *s, int fd_udp, struct addrinfo *res);
int cmd_myreservations(const Session *s, int fd_udp, struct addrinfo *res);

/* TCP */
int cmd_change_pass(Session *s, struct addrinfo *res, const char *old_password,
                    const char *new_password);
int cmd_create(const Session *s, struct addrinfo *res, const char *name, const char *fname,
               const char *event_date, int attendees);
int cmd_close(const Session *s, struct addrinfo *res, const char *eid);
int cmd_list(struct addrinfo *res);
int cmd_show(struct addrinfo *res, const char *eid);
int cmd_reserve(const Session *s, struct addrinfo *res, const char *eid, int people);

#endif /* COMMANDS_H */
