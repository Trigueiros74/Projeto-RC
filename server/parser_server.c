/* parser_server.c - validation of the messages the ES server receives.
 *
 * The protocol is strict: fields are separated by exactly one space and the
 * message ends with a single '\n'. Anything else earns an ERR reply, which is
 * why the parsers below scan the message by hand instead of relying on
 * sscanf(), whose "%s" happily swallows runs of whitespace.
 */

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#include "parser_server.h"
#include "storage.h"

/* ========================================================================
 * Field validators
 * ==================================================================== */

static int all_digits(const char *s, size_t len) {
    size_t i;
    for (i = 0; i < len; i++)
        if (!isdigit((unsigned char)s[i])) return 0;
    return 1;
}

int valid_uid_field(const char *s) {
    return s && strlen(s) == UID_LEN && all_digits(s, UID_LEN);
}

int valid_password_field(const char *s) {
    size_t i;
    if (!s || strlen(s) != PASSWORD_LEN) return 0;
    for (i = 0; i < PASSWORD_LEN; i++)
        if (!isalnum((unsigned char)s[i])) return 0;
    return 1;
}

int valid_eid_field(const char *s) {
    if (!s || strlen(s) != 3 || !all_digits(s, 3)) return 0;
    return atoi(s) >= EID_MIN && atoi(s) <= EID_MAX;
}

int valid_event_name_field(const char *s) {
    size_t len, i;
    if (!s) return 0;
    len = strlen(s);
    if (len == 0 || len > EVENT_NAME_MAX) return 0;
    for (i = 0; i < len; i++)
        if (!isalnum((unsigned char)s[i])) return 0;
    return 1;
}

/* dd-mm-yyyy hh:mm, with a calendar check so that 31-02-2026 is rejected. */
int valid_event_date_field(const char *s) {
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

/* "nnn...nnn.xxx": alphanumerics plus '-', '_' and '.', a 3-letter extension
   and at most 24 characters in total. The name also has to stay a single path
   component, since the server uses it verbatim to build a file path. */
int valid_fname_field(const char *s) {
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

/* Bounded numeric fields are parsed by hand: strtol on an unbounded digit run
   would be the easy way to overflow the reply with a nonsense value. */
static int parse_bounded_number(const char *s, size_t max_digits, long min, long max, long *out) {
    size_t len;
    long value;

    if (!s) return 0;
    len = strlen(s);
    if (len == 0 || len > max_digits || !all_digits(s, len)) return 0;

    value = strtol(s, NULL, 10);
    if (value < min || value > max) return 0;
    *out = value;
    return 1;
}

int valid_attendance_field(const char *s, int *out) {
    long value;
    if (!parse_bounded_number(s, 3, ATTENDANCE_MIN, ATTENDANCE_MAX, &value)) return 0;
    *out = (int)value;
    return 1;
}

int valid_people_field(const char *s, int *out) {
    long value;
    if (!parse_bounded_number(s, 3, 1, 999, &value)) return 0;
    *out = (int)value;
    return 1;
}

int valid_fsize_field(const char *s, long *out) {
    return parse_bounded_number(s, FSIZE_MAX_DIGITS, 0, FDATA_MAX_SIZE, out);
}

/* ========================================================================
 * UDP message parsers
 *
 * The TCP side reads its messages field by field straight off the socket (see
 * ES.c), because a CRE request carries binary data that cannot be treated as
 * a text line. Datagrams, in contrast, always arrive whole.
 * ==================================================================== */

/* Splits "CMD f1 ... fn\n" into exactly n fields. Returns 0 when the message
   matches that shape exactly: no repeated spaces, no missing or extra field,
   nothing after the terminating newline. */
static int split_fields(const char *buffer, int n, char *const out[], const size_t cap[]) {
    const char *p = buffer;
    int i;

    while (*p && *p != ' ' && *p != '\n') p++;   /* the command word */

    for (i = 0; i < n; i++) {
        const char *start;
        size_t len;

        if (*p != ' ') return 1;
        p++;
        start = p;
        while (*p && *p != ' ' && *p != '\n') p++;
        len = (size_t)(p - start);
        if (len == 0 || len >= cap[i]) return 1;
        memcpy(out[i], start, len);
        out[i][len] = '\0';
    }

    return (p[0] == '\n' && p[1] == '\0') ? 0 : 1;
}

/* All five UDP requests carry the same two fields. */
static int parse_credentials(const char *buffer, char *uid, char *password) {
    char *const fields[] = { uid, password };
    const size_t caps[] = { UID_LEN + 1, PASSWORD_LEN + 1 };

    if (split_fields(buffer, 2, fields, caps) != 0) return 1;
    if (!valid_uid_field(uid) || !valid_password_field(password)) return 1;
    return 0;
}

int parse_lin(const char *buffer, char *uid, char *password) { return parse_credentials(buffer, uid, password); }
int parse_lou(const char *buffer, char *uid, char *password) { return parse_credentials(buffer, uid, password); }
int parse_unr(const char *buffer, char *uid, char *password) { return parse_credentials(buffer, uid, password); }
int parse_lme(const char *buffer, char *uid, char *password) { return parse_credentials(buffer, uid, password); }
int parse_lmr(const char *buffer, char *uid, char *password) { return parse_credentials(buffer, uid, password); }
