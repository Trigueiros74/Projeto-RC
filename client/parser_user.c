/* parser_user.c - validation of what the user types at the prompt.
 *
 * Every argument is checked here, before a single byte reaches the network:
 * the server would reject a malformed request anyway, and catching it locally
 * gives the user a message that says which field was wrong.
 */

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "parser_user.h"

/* ========================================================================
 * Field validators
 * ==================================================================== */

static int all_digits(const char *s, size_t len) {
    size_t i;
    for (i = 0; i < len; i++)
        if (!isdigit((unsigned char)s[i])) return 0;
    return 1;
}

int valid_uid(const char *s) {
    return s && strlen(s) == UID_LEN && all_digits(s, UID_LEN);
}

int valid_password(const char *s) {
    size_t i;
    if (!s || strlen(s) != PASSWORD_LEN) return 0;
    for (i = 0; i < PASSWORD_LEN; i++)
        if (!isalnum((unsigned char)s[i])) return 0;
    return 1;
}

int valid_eid(const char *s) {
    return s && strlen(s) == EID_LEN && all_digits(s, EID_LEN) && atoi(s) >= 1;
}

int valid_event_name(const char *s) {
    size_t len, i;
    if (!s) return 0;
    len = strlen(s);
    if (len == 0 || len > EVENT_NAME_MAX) return 0;
    for (i = 0; i < len; i++)
        if (!isalnum((unsigned char)s[i])) return 0;
    return 1;
}

/* dd-mm-yyyy hh:mm, with a real calendar check. */
int valid_event_date(const char *s) {
    int day, month, year, hour, minute, month_days;

    if (!s || strlen(s) != EVENT_DATE_LEN) return 0;
    if (s[2] != '-' || s[5] != '-' || s[10] != ' ' || s[13] != ':') return 0;
    if (!all_digits(s, 2) || !all_digits(s + 3, 2) || !all_digits(s + 6, 4) ||
        !all_digits(s + 11, 2) || !all_digits(s + 14, 2)) return 0;

    day    = (s[0]  - '0') * 10 + (s[1]  - '0');
    month  = (s[3]  - '0') * 10 + (s[4]  - '0');
    year   = (s[6]  - '0') * 1000 + (s[7] - '0') * 100 + (s[8] - '0') * 10 + (s[9] - '0');
    hour   = (s[11] - '0') * 10 + (s[12] - '0');
    minute = (s[14] - '0') * 10 + (s[15] - '0');

    if (month < 1 || month > 12 || year < 1900) return 0;
    if (hour > 23 || minute > 59) return 0;

    month_days = 31;
    if (month == 4 || month == 6 || month == 9 || month == 11) month_days = 30;
    else if (month == 2)
        month_days = ((year % 4 == 0 && year % 100 != 0) || year % 400 == 0) ? 29 : 28;

    return day >= 1 && day <= month_days;
}

/* At most 24 characters of "nnn...nnn.xxx". The same check guards the file
   name the server sends back in a SED reply, so a rogue server cannot talk the
   client into writing outside the current directory. */
int valid_fname(const char *s) {
    size_t len, i;
    const char *dot;

    if (!s) return 0;
    len = strlen(s);
    if (len == 0 || len > FILENAME_MAX_LEN) return 0;
    if (strcmp(s, ".") == 0 || strcmp(s, "..") == 0) return 0;

    dot = strrchr(s, '.');
    if (!dot || dot == s || strlen(dot + 1) != 3) return 0;

    for (i = 0; i < len; i++) {
        char c = s[i];
        if (!(isalnum((unsigned char)c) || c == '-' || c == '_' || c == '.')) return 0;
    }
    return 1;
}

/* ========================================================================
 * Tokenising
 * ==================================================================== */

/* Copies the next whitespace-delimited word of *cursor into `out`, advancing
   the cursor. Returns 0 when a word that fits was found. */
static int next_token(const char **cursor, char *out, size_t cap) {
    const char *p = *cursor;
    size_t len;

    while (*p == ' ' || *p == '\t') p++;
    if (*p == '\0' || *p == '\n') return 1;

    len = strcspn(p, " \t\n");
    if (len >= cap) return 1;                 /* argument longer than allowed */

    memcpy(out, p, len);
    out[len] = '\0';
    *cursor = p + len;
    return 0;
}

static int no_more_tokens(const char *cursor) {
    while (*cursor == ' ' || *cursor == '\t' || *cursor == '\n') cursor++;
    return *cursor == '\0';
}

/* A token buffer big enough to hold any oversized argument the user may type,
   so that "too long" is reported as such instead of being truncated. */
#define TOKEN_MAX 128

/* ========================================================================
 * Command parsers
 * ==================================================================== */

int parse_no_args(const char *args) {
    if (!no_more_tokens(args)) {
        fprintf(stderr, "This command takes no arguments.\n");
        return 1;
    }
    return 0;
}

int parse_login(const char *args, char *uid, char *password) {
    char uid_token[TOKEN_MAX], pass_token[TOKEN_MAX];

    if (next_token(&args, uid_token, sizeof uid_token) != 0 ||
        next_token(&args, pass_token, sizeof pass_token) != 0 ||
        !no_more_tokens(args)) {
        fprintf(stderr, "Usage: login <UID> <password>\n");
        return 1;
    }
    if (!valid_uid(uid_token)) {
        fprintf(stderr, "Invalid UID: expected %d digits.\n", UID_LEN);
        return 1;
    }
    if (!valid_password(pass_token)) {
        fprintf(stderr, "Invalid password: expected %d letters or digits.\n", PASSWORD_LEN);
        return 1;
    }

    strcpy(uid, uid_token);
    strcpy(password, pass_token);
    return 0;
}

int parse_change_pass(const char *args, char *old_password, char *new_password) {
    char old_token[TOKEN_MAX], new_token[TOKEN_MAX];

    if (next_token(&args, old_token, sizeof old_token) != 0 ||
        next_token(&args, new_token, sizeof new_token) != 0 ||
        !no_more_tokens(args)) {
        fprintf(stderr, "Usage: changePass <oldPassword> <newPassword>\n");
        return 1;
    }
    if (!valid_password(old_token) || !valid_password(new_token)) {
        fprintf(stderr, "Passwords must have %d letters or digits.\n", PASSWORD_LEN);
        return 1;
    }

    strcpy(old_password, old_token);
    strcpy(new_password, new_token);
    return 0;
}

/* create <name> <event_fname> <dd-mm-yyyy> <hh:mm> <num_attendees> */
int parse_create(const char *args, char *name, char *fname, char *event_date, int *attendees) {
    char name_token[TOKEN_MAX], fname_token[TOKEN_MAX];
    char date_token[TOKEN_MAX], time_token[TOKEN_MAX], count_token[TOKEN_MAX];
    char date_buf[EVENT_DATE_LEN + 1];
    char *end;
    long count;

    if (next_token(&args, name_token, sizeof name_token) != 0 ||
        next_token(&args, fname_token, sizeof fname_token) != 0 ||
        next_token(&args, date_token, sizeof date_token) != 0 ||
        next_token(&args, time_token, sizeof time_token) != 0 ||
        next_token(&args, count_token, sizeof count_token) != 0 ||
        !no_more_tokens(args)) {
        fprintf(stderr, "Usage: create <name> <file> <dd-mm-yyyy> <hh:mm> <num_attendees>\n");
        return 1;
    }

    if (!valid_event_name(name_token)) {
        fprintf(stderr, "Invalid event name: up to %d letters or digits.\n", EVENT_NAME_MAX);
        return 1;
    }
    if (!valid_fname(fname_token)) {
        fprintf(stderr, "Invalid file name: up to %d characters ending in a 3-letter extension.\n",
                FILENAME_MAX_LEN);
        return 1;
    }
    if (snprintf(date_buf, sizeof date_buf, "%s %s", date_token, time_token) >= (int)sizeof date_buf ||
        !valid_event_date(date_buf)) {
        fprintf(stderr, "Invalid date: expected dd-mm-yyyy hh:mm.\n");
        return 1;
    }

    count = strtol(count_token, &end, 10);
    if (*end != '\0' || count < ATTENDANCE_MIN || count > ATTENDANCE_MAX) {
        fprintf(stderr, "Invalid number of attendees: expected %d to %d.\n",
                ATTENDANCE_MIN, ATTENDANCE_MAX);
        return 1;
    }

    strcpy(name, name_token);
    strcpy(fname, fname_token);
    strcpy(event_date, date_buf);
    *attendees = (int)count;
    return 0;
}

int parse_eid(const char *args, char *eid) {
    char token[TOKEN_MAX];

    if (next_token(&args, token, sizeof token) != 0 || !no_more_tokens(args)) {
        fprintf(stderr, "Usage: <command> <EID>\n");
        return 1;
    }
    if (!valid_eid(token)) {
        fprintf(stderr, "Invalid EID: expected %d digits, from 001 to 999.\n", EID_LEN);
        return 1;
    }

    strcpy(eid, token);
    return 0;
}

int parse_reserve(const char *args, char *eid, int *people) {
    char eid_token[TOKEN_MAX], count_token[TOKEN_MAX];
    char *end;
    long count;

    if (next_token(&args, eid_token, sizeof eid_token) != 0 ||
        next_token(&args, count_token, sizeof count_token) != 0 ||
        !no_more_tokens(args)) {
        fprintf(stderr, "Usage: reserve <EID> <number of seats>\n");
        return 1;
    }
    if (!valid_eid(eid_token)) {
        fprintf(stderr, "Invalid EID: expected %d digits, from 001 to 999.\n", EID_LEN);
        return 1;
    }

    count = strtol(count_token, &end, 10);
    if (*end != '\0' || count < PEOPLE_MIN || count > PEOPLE_MAX) {
        fprintf(stderr, "Invalid number of seats: expected %d to %d.\n", PEOPLE_MIN, PEOPLE_MAX);
        return 1;
    }

    strcpy(eid, eid_token);
    *people = (int)count;
    return 0;
}
