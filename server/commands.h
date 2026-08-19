#ifndef COMMANDS_H
#define COMMANDS_H

#include <stddef.h>

#include "storage.h"

/* Longest UDP reply the server may produce: LST-style listings are sent over
   TCP, so the bound only has to cover RME (999 events) and RMR (50
   reservations). */
#define UDP_REPLY_MAX 8192

/* ---- UDP handlers -------------------------------------------------------
 * They format the complete reply (terminated by '\n') into `reply`, which
 * holds UDP_REPLY_MAX bytes, and return its length. */
size_t cmd_lin(char *reply, const char *uid, const char *password);
size_t cmd_lou(char *reply, const char *uid, const char *password);
size_t cmd_unr(char *reply, const char *uid, const char *password);
size_t cmd_lme(char *reply, const char *uid, const char *password);
size_t cmd_lmr(char *reply, const char *uid, const char *password);

/* ---- TCP handlers -------------------------------------------------------
 * These write their reply straight to the socket, because it may carry a file
 * of up to 10 MB that must never be buffered in full. They return 0 when the
 * whole reply reached the client and -1 otherwise; `status` receives the
 * protocol status word for the verbose log. */
int cmd_cre(int fd, const char *uid, const char *password, const char *name,
            const char *event_date, int attendance_size, const char *fname,
            const char *staged_path, char status[8]);
int cmd_cls(int fd, const char *uid, const char *password, const char *eid, char status[8]);
int cmd_lst(int fd, char status[8]);
int cmd_sed(int fd, const char *eid, char status[8]);
int cmd_rid(int fd, const char *uid, const char *password, const char *eid, int people,
            char status[8]);
int cmd_cps(int fd, const char *uid, const char *oldpass, const char *newpass, char status[8]);

/* Sends `len` bytes, retrying on short writes and on EINTR. */
int write_all(int fd, const char *buf, size_t len);

#endif /* COMMANDS_H */
