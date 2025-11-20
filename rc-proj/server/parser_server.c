/* parser_server.c - parsers for Event Reservation protocol
   Returns 0 on success, 1 on parse/validation error.
*/

#include "parser_server.h"
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <stdlib.h>

#define MAX_PASSWORD_LEN 127
#define MAX_FSIZE (10 * 1024 * 1024) /* 10 MiB */

/* Keep original validate_string_spaces semantics:
   returns 1 on invalid spacing, 0 otherwise.
   The function expects the buffer to end with '\n'. */
int validate_string_spaces(const char *str, int limit) {
    int space_flag = 0;
    int count = 0;

    if (!str) return 1;
    size_t len = strlen(str);
    if (len == 0 || str[len - 1] != '\n')
        return 1;

    for (size_t i = 0; i < len - 1; i++) {
        if (isspace((unsigned char)str[i])) {
            if (space_flag)
                return 1; 
            count++;
            space_flag = 1;
        } else {
            space_flag = 0;
        }

        if (limit != 0 && count == limit)
            break;
    }

    if (isspace((unsigned char)str[0]) || (isspace((unsigned char)str[len - 2]) && limit == 0))
        return 1;

    return 0;
}

/* helpers */
static int is_number_str(const char *s) {
    if (!s || *s == '\0') return 0;
    for (const char *p = s; *p; ++p) if(!isdigit((unsigned char)*p)) return 0;
    return 1;
}

static int validate_uid_str(const char *s) {
    /* expects string of exactly 6 digits */
    if (!s) return 0;
    if (strlen(s) != 6) return 0;
    return is_number_str(s);
}

static int validate_eid_str(const char *s) {
    /* expects exactly 3 digits */
    if (!s) return 0;
    if (strlen(s) != 3) return 0;
    return is_number_str(s);
}

static int validate_name_str(const char *s) {
    /* single word, up to 10 alphanumeric chars only */
    if (!s) return 0;
    size_t len = strlen(s);
    if (len == 0 || len > 10) return 0;
    for (size_t i = 0; i < len; ++i)
        if (!isalnum((unsigned char)s[i])) return 0;
    return 1;
}

static int validate_date_str(const char *s) {
    /* mm-dd-yyyy basic validation */
    if (!s) return 0;
    if (strlen(s) != 10) return 0; /* "mm-dd-yyyy" */
    if (s[2] != '-' || s[5] != '-') return 0;
    char mm[3] = {s[0], s[1], '\0'};
    char dd[3] = {s[3], s[4], '\0'};
    char yyyy[5] = {s[6], s[7], s[8], s[9], '\0'};
    if (!is_number_str(mm) || !is_number_str(dd) || !is_number_str(yyyy)) return 0;
    int mi = atoi(mm), di = atoi(dd), yi = atoi(yyyy);
    if (mi < 1 || mi > 12) return 0;
    if (yi < 1900 || yi > 9999) return 0;
    int mdays = 31;
    if (mi == 4 || mi == 6 || mi == 9 || mi == 11) mdays = 30;
    else if (mi == 2) {
        int leap = ( (yi % 4 == 0 && yi % 100 != 0) || (yi % 400 == 0) );
        mdays = leap ? 29 : 28;
    }
    if (di < 1 || di > mdays) return 0;
    return 1;
}

static int validate_fname_str(const char *s) {
    if (!s) return 0;
    size_t len = strlen(s);
    if (len == 0 || len > 24) return 0;
    /* must contain a dot and 3-letter extension */
    const char *dot = strrchr(s, '.');
    if (!dot) return 0;
    size_t ext_len = strlen(dot+1);
    if (ext_len != 3) return 0;
    for (const char *p = s; *p; ++p) {
        if (!(isalnum((unsigned char)*p) || *p == '-' || *p == '_' || *p == '.'))
            return 0;
    }
    return 1;
}

/* ----------------- UDP parsers -----------------
   LIN UID password\n
   LOU UID password\n
   UNR UID password\n
   LME UID password\n
   LMR UID password\n
   For these, UID is 6 digits; password is a non-space token.
*/
int parse_lin(char *buffer, char *uid, char *password) {
    char extra[2];
    if (sscanf(buffer, "%*s %6s %127s %1s", uid, password, extra) != 2)
        return 1;
    if (validate_string_spaces(buffer, 0) == 1) return 1;
    if (!validate_uid_str(uid)) return 1;
    return 0;
}

int parse_lou(char *buffer, char *uid, char *password) {
    char extra[2];
    if (sscanf(buffer, "%*s %6s %127s %1s", uid, password, extra) != 2)
        return 1;
    if (validate_string_spaces(buffer, 0) == 1) return 1;
    if (!validate_uid_str(uid)) return 1;
    return 0;
}

int parse_unr(char *buffer, char *uid, char *password) {
    char extra[2];
    if (sscanf(buffer, "%*s %6s %127s %1s", uid, password, extra) != 2)
        return 1;
    if (validate_string_spaces(buffer, 0) == 1) return 1;
    if (!validate_uid_str(uid)) return 1;
    return 0;
}

int parse_lme(char *buffer, char *uid, char *password) {
    char extra[2];
    if (sscanf(buffer, "%*s %6s %127s %1s", uid, password, extra) != 2)
        return 1;
    if (validate_string_spaces(buffer, 0) == 1) return 1;
    if (!validate_uid_str(uid)) return 1;
    return 0;
}

int parse_lmr(char *buffer, char *uid, char *password) {
    char extra[2];
    if (sscanf(buffer, "%*s %6s %127s %1s", uid, password, extra) != 2)
        return 1;
    if (validate_string_spaces(buffer, 0) == 1) return 1;
    if (!validate_uid_str(uid)) return 1;
    return 0;
}

/* ----------------- TCP parsers -----------------
   parse_cre_header:
     header (up to first '\n') format:
     CRE UID password name event_date attendance_size Fname\n
     This function parses only the header line and validates the fields.
*/
int parse_cre_header(char *header, char *uid, char *password, char *name, char *event_date, int *attendance_size, char *fname) {
    char extra[2];
    int asize;
    int ret = sscanf(header, "%*s %6s %127s %10s %10s %d %24s %1s",
                     uid, password, name, event_date, &asize, fname, extra);
    if (ret != 6) return 1;
    if (!validate_uid_str(uid)) return 1;
    if (strlen(password) == 0) return 1;
    if (!validate_name_str(name)) return 1;
    if (!validate_date_str(event_date)) return 1;
    if (asize < 10 || asize > 999) return 1;
    if (!validate_fname_str(fname)) return 1;
    *attendance_size = asize;
    return 0;
}

/* CLS UID password EID\n */
int parse_cls(char *buffer, char *uid, char *password, char *eid) {
    char extra[2];
    if (sscanf(buffer, "%*s %6s %127s %3s %1s", uid, password, eid, extra) != 3)
        return 1;
    if (validate_string_spaces(buffer, 0) == 1) return 1;
    if (!validate_uid_str(uid)) return 1;
    if (!validate_eid_str(eid)) return 1;
    return 0;
}

/* LST\n  (no args) */
int parse_lst(char *buffer) {
    char extra[2];
    int r = sscanf(buffer, "%*s %1s", extra);
    if (r != 0 && r != -1) return 1;
    if (validate_string_spaces(buffer, 0) == 1) return 1;
    return 0;
}

/* SED EID\n */
int parse_sed(char *buffer, char *eid) {
    char extra[2];
    if (sscanf(buffer, "%*s %3s %1s", eid, extra) != 1)
        return 1;
    if (validate_string_spaces(buffer, 0) == 1) return 1;
    if (!validate_eid_str(eid)) return 1;
    return 0;
}

/* RID UID password EID people\n */
int parse_rid(char *buffer, char *uid, char *password, char *eid, int *people) {
    char extra[2];
    if (sscanf(buffer, "%*s %6s %127s %3s %d %1s", uid, password, eid, people, extra) != 4)
        return 1;
    if (validate_string_spaces(buffer, 0) == 1) return 1;
    if (!validate_uid_str(uid)) return 1;
    if (!validate_eid_str(eid)) return 1;
    if (*people < 1 || *people > 999) return 1;
    return 0;
}

/* CPS UID oldPassword newPassword\n */
int parse_cps(char *buffer, char *uid, char *oldpass, char *newpass) {
    char extra[2];
    if (sscanf(buffer, "%*s %6s %127s %127s %1s", uid, oldpass, newpass, extra) != 3)
        return 1;
    if (validate_string_spaces(buffer, 0) == 1) return 1;
    if (!validate_uid_str(uid)) return 1;
    if (strlen(oldpass) == 0 || strlen(newpass) == 0) return 1;
    return 0;
}
