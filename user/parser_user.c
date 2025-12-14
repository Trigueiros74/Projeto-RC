#include "parser_user.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

int parse_login(char *buffer, char *request) {
    char uid[20], pass[20];
    char extra[2];

    if(sscanf(buffer, "%*s %19s %19s %1s", uid, pass, extra) != 2) {
        fprintf(stderr, "Invalid arguments!\n");
        return 1;
    }

    // Validate UID: must be exactly 6 digits
    if(strlen(uid) != 6) {
        fprintf(stderr, "Invalid UID! Must be exactly 6 digits.\n");
        return 1;
    }
    for(char *p = uid; *p; p++) {
        if(!isdigit(*p)) {
            fprintf(stderr, "Invalid UID! Must contain only digits.\n");
            return 1;
        }
    }

    // Validate password: must be exactly 8 alphanumeric characters
    if(strlen(pass) != 8) {
        fprintf(stderr, "Invalid password! Must be exactly 8 alphanumeric characters.\n");
        return 1;
    }
    for(char *p = pass; *p; p++) {
        if(!isalnum(*p)) {
            fprintf(stderr, "Invalid password! Must contain only letters and digits.\n");
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

    // Validate old password: must be exactly 8 alphanumeric characters
    if(strlen(old_pass) != 8) {
        fprintf(stderr, "Invalid old password! Must be exactly 8 alphanumeric characters.\n");
        return 1;
    }
    for(char *p = old_pass; *p; p++) {
        if(!isalnum(*p)) {
            fprintf(stderr, "Invalid old password! Must contain only letters and digits.\n");
            return 1;
        }
    }

    // Validate new password: must be exactly 8 alphanumeric characters
    if(strlen(new_pass) != 8) {
        fprintf(stderr, "Invalid new password! Must be exactly 8 alphanumeric characters.\n");
        return 1;
    }
    for(char *p = new_pass; *p; p++) {
        if(!isalnum(*p)) {
            fprintf(stderr, "Invalid new password! Must contain only letters and digits.\n");
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
    char name[50], event_fname[50], event_datetime[20];
    char date_part[12], time_part[6];
    unsigned int num_attendees;
    
    // Parse: create name event_fname DD-MM-YYYY HH:MM num_attendees
    // Date and time are separate tokens due to space between them
    if (sscanf(buffer, "create %49s %49s %11s %5s %u", 
               name, event_fname, date_part, time_part, &num_attendees) != 5) {
        fprintf(stderr, "Invalid arguments!\n");
        return 1;
    }
    
    // Combine date and time into single string
    snprintf(event_datetime, sizeof(event_datetime), "%s %s", date_part, time_part);

    // Validate event name: 1-10 alphanumeric chars
    size_t name_len = strlen(name);
    if (name_len == 0 || name_len > 10) {
        fprintf(stderr, "Invalid event name! Must be 1-10 characters.\n");
        return 1;
    }
    for (char *p = name; *p; p++) {
        if (!isalnum((unsigned char)*p)) {
            fprintf(stderr, "Invalid event name! Must be alphanumeric only.\n");
            return 1;
        }
    }

    // Validate filename: 1-24 chars, alphanumeric + '-', '_', '.', must have 3-letter extension
    size_t fname_len = strlen(event_fname);
    if (fname_len == 0 || fname_len > 24) {
        fprintf(stderr, "Invalid filename! Must be 1-24 characters.\n");
        return 1;
    }
    char *dot = strrchr(event_fname, '.');
    if (!dot || strlen(dot+1) != 3) {
        fprintf(stderr, "Invalid filename! Must have 3-letter extension.\n");
        return 1;
    }
    for (char *p = event_fname; *p; p++) {
        if (!(isalnum((unsigned char)*p) || *p == '-' || *p == '_' || *p == '.')) {
            fprintf(stderr, "Invalid filename! Only alphanumeric, '-', '_', '.' allowed.\n");
            return 1;
        }
    }

    // Validate date+time: DD-MM-YYYY HH:MM (16 chars)
    if (strlen(event_datetime) != 16 || 
        event_datetime[2] != '-' || event_datetime[5] != '-' ||
        event_datetime[10] != ' ' || event_datetime[13] != ':') {
        fprintf(stderr, "Invalid format! Use DD-MM-YYYY HH:MM.\n");
        return 1;
    }
    
    // Validate digits
    for (int i = 0; i < 16; ++i) {
        if (i == 2 || i == 5 || i == 10 || i == 13) continue;
        if (!isdigit((unsigned char)event_datetime[i])) {
            fprintf(stderr, "Invalid format! Use DD-MM-YYYY HH:MM.\n");
            return 1;
        }
    }

    // Extract and validate components
    int dd = (event_datetime[0]-'0')*10 + (event_datetime[1]-'0');
    int mm = (event_datetime[3]-'0')*10 + (event_datetime[4]-'0');
    int yyyy = (event_datetime[6]-'0')*1000 + (event_datetime[7]-'0')*100 + 
               (event_datetime[8]-'0')*10 + (event_datetime[9]-'0');
    int hh = (event_datetime[11]-'0')*10 + (event_datetime[12]-'0');
    int min = (event_datetime[14]-'0')*10 + (event_datetime[15]-'0');
    
    if (mm < 1 || mm > 12) {
        fprintf(stderr, "Invalid month! Must be 01-12.\n");
        return 1;
    }
    if (yyyy < 1900 || yyyy > 9999) {
        fprintf(stderr, "Invalid year! Must be 1900-9999.\n");
        return 1;
    }
    
    int max_day = 31;
    if (mm == 4 || mm == 6 || mm == 9 || mm == 11) max_day = 30;
    else if (mm == 2) {
        int leap = ((yyyy % 4 == 0 && yyyy % 100 != 0) || (yyyy % 400 == 0));
        max_day = leap ? 29 : 28;
    }
    if (dd < 1 || dd > max_day) {
        fprintf(stderr, "Invalid day %d for month %d.\n", dd, mm);
        return 1;
    }
    
    if (hh < 0 || hh > 23) {
        fprintf(stderr, "Invalid hour! Must be 00-23.\n");
        return 1;
    }
    if (min < 0 || min > 59) {
        fprintf(stderr, "Invalid minutes! Must be 00-59.\n");
        return 1;
    }

    // Validate num_attendees
    if (num_attendees < 10 || num_attendees > 999) {
        fprintf(stderr, "Invalid attendees! Must be 10-999.\n");
        return 1;
    }

    char *password = get_user_password(UID);
    if (!password) {
        fprintf(stderr, "User not logged in!\n");
        return 1;
    }

    // Format: CRE UID password name event_date attendance_size Fname\n
    // Note: We pass event_fname back so cmd_create can append it with Fsize Fdata
    sprintf(request, "CRE %s %s %s %s %u %s\n", UID, password, name, event_datetime, num_attendees, event_fname);
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

    // Validate reservation value: must be between 1 and 999
    if (value < 1 || value > 999) {
        fprintf(stderr, "Invalid reservation value! Must be between 1 and 999.\n");
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