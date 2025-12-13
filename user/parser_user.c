#include "parser_user.h"
#include <stdio.h>
#include <string.h>
#include <ctype.h>

int parse_login(char *buffer, char *request) {
    char uid[20], pass[20];
    char extra[2];

    if(sscanf(buffer, "%*s %19s %19s %1s", uid, pass, extra) != 2) {
        fprintf(stderr, "Invalid arguments!\n");
        return 1;
    }

    // Basic validation: assume alphanumeric
    for(char *p = uid; *p; p++) {
        if(!isalnum(*p)) {
            fprintf(stderr, "Invalid UID!\n");
            return 1;
        }
    }
    for(char *p = pass; *p; p++) {
        if(!isalnum(*p)) {
            fprintf(stderr, "Invalid password!\n");
            return 1;
        }
    }

    sprintf(request, "LIN %s %s\n", uid, pass);

    return 0;
}

int parse_changePass(char *buffer, char *request, char *UID) {
    char old_pass[20], new_pass[20];
    char extra[2];

    if(sscanf(buffer, "%*s %19s %19s %1s", old_pass, new_pass, extra) != 2) {
        fprintf(stderr, "Invalid arguments!\n");
        return 1;
    }

    // Basic validation: assume alphanumeric
    for(char *p = old_pass; *p; p++) {
        if(!isalnum(*p)) {
            fprintf(stderr, "Invalid old password!\n");
            return 1;
        }
    }
    for(char *p = new_pass; *p; p++) {
        if(!isalnum(*p)) {
            fprintf(stderr, "Invalid new password!\n");
            return 1;
        }
    }

    sprintf(request, "CPS %s %s %s\n", UID, old_pass, new_pass);

    return 0;
}

int parse_unregister(char *buffer, char *request, char *UID) {
    (void)buffer;
    char *password = get_user_password(UID);
    if (!password) {
        fprintf(stderr, "User not logged in or password unknown!\n");
        return 1;
    }
    sprintf(request, "UNR %s %s\n", UID, password);
    return 0;
}

int parse_logout(char *buffer, char *request, char *UID) {
    (void)buffer;
    char *password = get_user_password(UID);
    if (!password) {
        fprintf(stderr, "User not logged in or password unknown!\n");
        return 1;
    }
    sprintf(request, "LOU %s %s\n", UID, password);
    return 0;
}


int parse_create(char *buffer, char *request, char *UID) {
    char name[50], event_fname[50], event_date_in[11];
    char event_date_out[11];
    unsigned int num_attendees;
    char extra[2];

    if(sscanf(buffer, "%*s %49s %49s %10s %u %1s", name, event_fname, event_date_in, &num_attendees, extra) != 4) {
        fprintf(stderr, "Invalid arguments!\n");
        return 1;
    }

    // Accept user input as DD-MM-YYYY (per spec) and send it unchanged
    if (strlen(event_date_in) != 10 || event_date_in[2] != '-' || event_date_in[5] != '-') {
        fprintf(stderr, "Invalid date format! Use DD-MM-YYYY.\n");
        return 1;
    }
    // Validate digits
    for (int i = 0; i < 10; ++i) {
        if (i == 2 || i == 5) continue;
        if (!isdigit((unsigned char)event_date_in[i])) {
            fprintf(stderr, "Invalid date format! Use DD-MM-YYYY.\n");
            return 1;
        }
    }
    // copy input directly to output (server expects dd-mm-yyyy now)
    strncpy(event_date_out, event_date_in, sizeof event_date_out - 1);
    event_date_out[sizeof event_date_out - 1] = '\0';

    char *password = get_user_password(UID);
    if (!password) {
        fprintf(stderr, "User not logged in or password unknown!\n");
        return 1;
    }

    sprintf(request, "CRE %s %s %s %s %u %s\n", UID, password, name, event_date_out, num_attendees, event_fname);

    return 0;
}

int parse_close(char *buffer, char *request, char *UID) {
    char eid_str[4];
    char extra[2];

    if (sscanf(buffer, "%*s %3s %1s", eid_str, extra) != 1) {
        fprintf(stderr, "Invalid arguments!\n");
        return 1;
    }
    /* require exactly 3 digits for EID */
    if (strlen(eid_str) != 3) {
        fprintf(stderr, "EID must be 3 digits (e.g. 001)\n");
        return 1;
    }
    for (char *p = eid_str; *p; ++p) if (!isdigit((unsigned char)*p)) { fprintf(stderr, "Invalid EID\n"); return 1; }

    char *password = get_user_password(UID);
    if (!password) {
        fprintf(stderr, "User not logged in or password unknown!\n");
        return 1;
    }
    sprintf(request, "CLS %s %s %s\n", UID, password, eid_str);

    return 0;
}

int parse_myevents(char *buffer, char *request, char *UID) {
    (void)buffer;
    char *password = get_user_password(UID);
    if (!password) {
        fprintf(stderr, "User not logged in or password unknown!\n");
        return 1;
    }
    sprintf(request, "LME %s %s\n", UID, password);
    return 0;
}

int parse_list(char *buffer, char *request) {
    (void)buffer;  
    sprintf(request, "LST\n"); 
    return 0;
}

int parse_show(char *buffer, char *request) {
    char eid_str[4];
    char extra[2];

    if (sscanf(buffer, "%*s %3s %1s", eid_str, extra) != 1) {
        fprintf(stderr, "Invalid arguments!\n");
        return 1;
    }
    if (strlen(eid_str) != 3) {
        fprintf(stderr, "EID must be 3 digits (e.g. 001)\n");
        return 1;
    }
    for (char *p = eid_str; *p; ++p) if (!isdigit((unsigned char)*p)) { fprintf(stderr, "Invalid EID\n"); return 1; }

    sprintf(request, "SED %s\n", eid_str);

    return 0;
}

int parse_reserve(char *buffer, char *request, char *UID) {
    char eid_str[4];
    unsigned int value;
    char extra[2];

    if (sscanf(buffer, "%*s %3s %u %1s", eid_str, &value, extra) != 2) {
        fprintf(stderr, "Invalid arguments!\n");
        return 1;
    }
    if (strlen(eid_str) != 3) {
        fprintf(stderr, "EID must be 3 digits (e.g. 001)\n");
        return 1;
    }
    for (char *p = eid_str; *p; ++p) if (!isdigit((unsigned char)*p)) { fprintf(stderr, "Invalid EID\n"); return 1; }

    if (value == 0) {
        fprintf(stderr, "Invalid reservation value!\n");
        return 1;
    }

    char *password = get_user_password(UID);
    if (!password) {
        fprintf(stderr, "User not logged in or password unknown!\n");
        return 1;
    }
    sprintf(request, "RID %s %s %s %u\n", UID, password, eid_str, value);

    return 0;
}

int parse_myreservations(char *buffer, char *request, char *UID) {
    (void)buffer;
    char *password = get_user_password(UID);
    if (!password) {
        fprintf(stderr, "User not logged in or password unknown!\n");
        return 1;
    }
    sprintf(request, "LMR %s %s\n", UID, password);
    return 0;
}