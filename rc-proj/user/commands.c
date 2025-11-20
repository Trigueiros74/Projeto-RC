/* commands.c
   Commands implementation for the User application (Event Reservation).
   Follows the style and structure of the provided game commands.c.
*/

#include "commands.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <ctype.h>

#define TRY_LIMIT 3
#define RESPONSE_LEN 32

/* Declarations for functions defined in user.c */
void add_user_session(const char *UID, const char *password);
void remove_user_session(const char *UID);

/* --- reuse helpers from your project (validate_string_spaces, send_udp_request,
      send_tcp_request, create_file) --- */
/* If you already have these in another file, remove duplicates. I include them
   here to keep the file self-contained in case you want to compile only this file. */

/* NOTE: If these functions are already compiled elsewhere, comment these out
   and use the versions from your project. */
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

int send_udp_request(char *request, int fd_udp, struct addrinfo *res, char *response) {
    int error = 1;
    ssize_t bytes_sent = sendto(fd_udp, request, strlen(request) * sizeof(char), 0, res->ai_addr,res->ai_addrlen);
    if(bytes_sent < 0) return 1;

    for (int tries = 0; tries < TRY_LIMIT ; tries++) {
        ssize_t bytes_received = recvfrom(fd_udp, response, RESPONSE_LEN, 0, NULL, NULL);

        if(bytes_received >= 0) {
            response[bytes_received] = '\0';
            error = 0;
            break;
        }
    }

    if(error) return 1;

    return 0;
}

int send_tcp_request(char *request, struct addrinfo *res, char *response, size_t response_cap) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if(fd == -1) return 1;

    if(connect(fd, res->ai_addr, res->ai_addrlen) == -1) {
        close(fd);
        return 1;
    }

    size_t to_write = strlen(request);
    size_t written = 0;
    while(written < to_write) {
        ssize_t n = write(fd, request + written, to_write - written);
        if(n <= 0) {
            close(fd);
            return 1;
        }
        written += n;
    }

    /* signal EOF to the server so it can finish reading the request */
    shutdown(fd, SHUT_WR);

    ssize_t total_read = 0;
    while(1) {
        ssize_t n = read(fd, response + total_read, response_cap - total_read - 1);
        if(n < 0) {
            close(fd);
            return 1;
        }
        if(n == 0) break; 
        total_read += n;
        if(total_read >= response_cap - 1) break; 
    }
    response[total_read] = '\0';

    close(fd);
    return 0;
}

/* send a buffer of exact length over TCP and receive response */
int send_tcp_request_bytes(const char *request_buf, size_t req_len, struct addrinfo *res, char *response, size_t response_cap) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if(fd == -1) return 1;

    if(connect(fd, res->ai_addr, res->ai_addrlen) == -1) {
        close(fd);
        return 1;
    }

    size_t to_write = req_len;
    size_t written = 0;
    while(written < to_write) {
        ssize_t n = write(fd, request_buf + written, to_write - written);
        if(n <= 0) {
            close(fd);
            return 1;
        }
        written += n;
    }

    /* signal EOF to the server so it can finish reading the request */
    shutdown(fd, SHUT_WR);

    ssize_t total_read = 0;
    while(1) {
        ssize_t n = read(fd, response + total_read, response_cap - total_read - 1);
        if(n < 0) {
            close(fd);
            return 1;
        }
        if(n == 0) break;
        total_read += n;
        if(total_read >= (ssize_t)response_cap - 1) break;
    }
    response[total_read] = '\0';

    close(fd);
    return 0;
}

int create_file(char *f_name, char *f_data) {
    FILE *file = fopen(f_name, "w");
    if(file == NULL)
        return 1;

    fprintf(file, "%s", f_data);
    fclose(file);
    return 0;
}

/* --- Command implementations --- */

/* cmd_login
   request already formatted by parser_user.c, fd_udp/socket and res provided.
   Expects server response: "RLG <STATUS> [UID]\n"
   STATUS: OK (successful login), NEW (new user registered), NOK (incorrect login)
           ERR (invalid args)
   If server provides a UID after OK/NEW, it will be parsed into *user_id.
*/
int cmd_login(char *request, unsigned int *user_id, int *logged_in, int fd_udp, struct addrinfo *res, const char *password) {
    char response[RESPONSE_LEN];
    char res_cmd[4];
    char status[6];
    char uid_str[16];

    if(send_udp_request(request, fd_udp, res, response) == 1) {
        fprintf(stderr, "Error while communicating with server! Ending session.\n");
        return 1;
    }

    if(sscanf(response, "%3s %5s %15s", res_cmd, status, uid_str) < 2 ||
       validate_string_spaces(response, 0) == 1) {
        fprintf(stderr, "Invalid response from server! Ending session.\n");
        return 1;
    }

    if(strcmp(res_cmd, "RLI") != 0) {
        fprintf(stderr, "Invalid response from server! Ending session.\n");
        return 1;
    }

    if(strcmp(status, "OK") == 0 || strcmp(status, "REG") == 0) {
        unsigned int uid;
        /* server may or may not return the UID token; if not present, extract from the original request */
        if(sscanf(uid_str, "%u", &uid) != 1) {
            if(sscanf(request, "%*s %15s", uid_str) != 1) {
                fprintf(stderr, "Invalid UID from server/request! Ending session.\n");
                return 1;
            }
            if(sscanf(uid_str, "%u", &uid) != 1) {
                fprintf(stderr, "Invalid UID from server/request! Ending session.\n");
                return 1;
            }
        }
        *user_id = uid;
        *logged_in = 1;
        printf("%s. UID: %u\n", strcmp(status, "OK") == 0 ? "Login successful" : "New user registered", *user_id);
    } else if(strcmp(status, "NOK") == 0) {
        printf("Incorrect login attempt.\n");
    } else {
        fprintf(stderr, "Invalid response from server! Ending session.\n");
        return 1;
    }

    return 0;
}


/* cmd_changePass
   Uses TCP. Request should be formatted like: "CPW UID oldPass newPass\n"
   Expects response: "RCP <STATUS>\n" where STATUS in {OK, UNKNOWN, NOK, ERR}
*/
int cmd_changePass(char *request, struct addrinfo *res) {
    char *response = (char *) malloc(sizeof(char) * 512);
    if (response == NULL) {
        fprintf(stderr, "Error while communicating with server! Ending session.\n");
        return 1;
    }

    char res_cmd[4];
    char status[8];
    char extra[2];

    if(send_tcp_request(request, res, response, 512) == 1) {
        fprintf(stderr, "Error while communicating with server! Ending session.\n");
        free(response);
        return 1;
    }

    if(sscanf(response, "%3s %7s %1s", res_cmd, status, extra) < 2) {
        fprintf(stderr, "Invalid response from server! Ending session.\n");
        free(response);
        return 1;
    }

    if(strcmp(res_cmd, "RCP") != 0) {
        fprintf(stderr, "Invalid response from server! Ending session.\n");
        free(response);
        return 1;
    }

    if(validate_string_spaces(response, 0) == 1) {
        fprintf(stderr, "Invalid response from server! Ending session.\n");
        free(response);
        return 1;
    }

    if(strcmp(status, "OK") == 0) {
        printf("Password changed successfully.\n");
    } else if(strcmp(status, "NID") == 0) {
        printf("User does not exist.\n");
    } else if(strcmp(status, "NLG") == 0) {
        printf("User not logged in.\n");
    } else if(strcmp(status, "NOK") == 0) {
        printf("Incorrect password.\n");
    } else if(strcmp(status, "ERR") == 0) {
        fprintf(stderr, "Invalid arguments!\n");
    } else {
        fprintf(stderr, "Invalid response from server! Ending session.\n");
        free(response);
        return 1;
    }

    free(response);
    return 0;
}

/* cmd_unregister
   Uses UDP. Request: "UNR UID password\n"
   Response: "RUN <STATUS>\n" STATUS: OK, UNKNOWN, NOK, ERR
   On OK, the client should also treat user as logged out.
*/
int cmd_unregister(char *request, unsigned int *user_id, int *logged_in, int fd_udp, struct addrinfo *res) {
    char response[RESPONSE_LEN];
    char res_cmd[4];
    char status[8];
    char extra[2];

    if(send_udp_request(request, fd_udp, res, response) == 1) {
        fprintf(stderr, "Error while communicating with server! Ending session.\n");
        return 1;
    }

    if(sscanf(response, "%3s %7s %1s", res_cmd, status, extra) < 2 ||
       validate_string_spaces(response, 0) == 1) {
        fprintf(stderr, "Invalid response from server! Ending session.\n");
        return 1;
    }

    if(strcmp(res_cmd, "RUR") != 0) {
        fprintf(stderr, "Invalid response from server! Ending session.\n");
        return 1;
    }

    if(strcmp(status, "OK") == 0) {
        printf("Unregister successful. You are now logged out.\n");
        char uid_str[16];
        sprintf(uid_str, "%u", *user_id);
        remove_user_session(uid_str);
        *user_id = 0;
        *logged_in = 0;
    } else if(strcmp(status, "NOK") == 0) {
        printf("User not logged in.\n");
    } else if(strcmp(status, "UNR") == 0) {
        printf("User not registered.\n");
    } else if(strcmp(status, "WRP") == 0) {
        printf("Incorrect password.\n");
    } else {
        fprintf(stderr, "Invalid response from server! Ending session.\n");
        return 1;
    }

    return 0;
}

/* cmd_logout
   Uses UDP. Request: "LGO UID\n"
   Response: "RLO <STATUS>\n" STATUS: OK, NOK, UNR, WRP
*/
int cmd_logout(char *request, unsigned int *user_id, int *logged_in, int fd_udp, struct addrinfo *res) {
    char response[RESPONSE_LEN];
    char res_cmd[4];
    char status[8];
    char extra[2];

    if(send_udp_request(request, fd_udp, res, response) == 1) {
        fprintf(stderr, "Error while communicating with server! Ending session.\n");
        return 1;
    }

    if(sscanf(response, "%3s %7s %1s", res_cmd, status, extra) < 2 ||
       validate_string_spaces(response, 0) == 1) {
        fprintf(stderr, "Invalid response from server! Ending session.\n");
        return 1;
    }

    if(strcmp(res_cmd, "RLO") != 0) {
        fprintf(stderr, "Invalid response from server! Ending session.\n");
        return 1;
    }

    if(strcmp(status, "OK") == 0) {
        printf("Logout successful.\n");
        char uid_str[16];
        sprintf(uid_str, "%u", *user_id);
        remove_user_session(uid_str);
        *user_id = 0;
        *logged_in = 0;
    } else if(strcmp(status, "NOK") == 0) {
        printf("User not logged in.\n");
    } else if(strcmp(status, "UNR") == 0) {
        printf("User not registered.\n");
    } else if(strcmp(status, "WRP") == 0) {
        printf("Incorrect password.\n");
    } else {
        fprintf(stderr, "Invalid response from server! Ending session.\n");
        return 1;
    }

    return 0;
}

/* cmd_create
   Uses TCP. Request: "CRT UID name event_fname event_date num_attendees\n"
   Server responds: "RCR <STATUS> EID\n" where STATUS = OK or ERR/UNKNOWN/NOK
   If OK, EID is 3-digit assigned id.
   The entire file contents are expected to be appended in the TCP response (like earlier st/sb pattern).
*/
int cmd_create(char *request, struct addrinfo *res) {
    char res_cmd[4];
    char status[8];
    unsigned int eid = 0;

    /* parse header fields from request to get filename and ensure formatting */
    char uid[16];
    char password[128];
    char name[64];
    char event_date[16];
    unsigned int num_attendees = 0;
    char fname[256];

    if(sscanf(request, "CRE %15s %127s %63s %15s %u %255s", uid, password, name, event_date, &num_attendees, fname) != 6) {
        fprintf(stderr, "Invalid create request header!\n");
        return 1;
    }

    /* open file */
    FILE *f = fopen(fname, "rb");
    if(!f) {
        fprintf(stderr, "Cannot open file '%s'\n", fname);
        return 1;
    }
    if(fseek(f, 0, SEEK_END) != 0) { fclose(f); return 1; }
    long fsize_l = ftell(f);
    if(fsize_l < 0) { fclose(f); return 1; }
    size_t fsize = (size_t) fsize_l;
    rewind(f);
    char *fdata = malloc(fsize);
    if(fsize > 0 && !fdata) { fclose(f); fprintf(stderr, "Out of memory\n"); return 1; }
    if(fsize > 0) {
        if(fread(fdata, 1, fsize, f) != fsize) { fclose(f); free(fdata); fprintf(stderr, "Error reading file\n"); return 1; }
    }
    fclose(f);

    /* build payload: header (request already ends with '\n'), then: <fsize><space><file-bytes> */
    char fsize_str[32];
    int fslen = snprintf(fsize_str, sizeof fsize_str, "%zu", fsize);
    size_t header_len = strlen(request);
    size_t payload_len = header_len + (size_t)fslen + 1 + fsize;
    char *payload = malloc(payload_len);
    if(!payload) { free(fdata); fprintf(stderr, "Out of memory\n"); return 1; }
    size_t off = 0;
    memcpy(payload + off, request, header_len); off += header_len;
    memcpy(payload + off, fsize_str, fslen); off += fslen;
    payload[off++] = ' ';
    if(fsize > 0) { memcpy(payload + off, fdata, fsize); off += fsize; }

    /* send binary payload */
    char *response = malloc(1024);
    if(!response) { free(fdata); free(payload); fprintf(stderr, "Out of memory\n"); return 1; }
    if(send_tcp_request_bytes(payload, payload_len, res, response, 1024) == 1) {
        fprintf(stderr, "Error while communicating with server! Ending session.\n");
        free(response); free(fdata); free(payload);
        return 1;
    }

    /* try to parse "RCE OK EID" or "RCE ERR" */
    if(sscanf(response, "%3s %7s %u", res_cmd, status, &eid) < 2) {
        fprintf(stderr, "Invalid response from server! Ending session.\n");
        free(response); free(fdata); free(payload);
        return 1;
    }

    if(strcmp(res_cmd, "RCE") != 0) {
        fprintf(stderr, "Invalid response from server! Ending session.\n");
        free(response); free(fdata); free(payload);
        return 1;
    }

    if(strcmp(status, "OK") == 0) {
        printf("Event created successfully. EID: %03u\n", eid);
    } else if(strcmp(status, "ERR") == 0) {
        fprintf(stderr, "Invalid arguments!\n");
        free(response); free(fdata); free(payload);
        return 1;
    } else if(strcmp(status, "NGL") == 0) {
        printf("User not logged in.\n");
    } else if(strcmp(status, "NOK") == 0) {
        printf("Event could not be created.\n");
    } else if(strcmp(status, "WRP") == 0) {
        printf("Incorrect password.\n");
    } else {
        fprintf(stderr, "Invalid response from server! Ending session.\n");
        free(response); free(fdata); free(payload);
        return 1;
    }

    free(response); free(fdata); free(payload);
    return 0;
}

/* cmd_close
   Uses TCP. Request: "CLS UID EID\n"
   Response: "RCL <STATUS> [INFO]\n" STATUS: OK, EXPIRED, SOLDOUT, ERR, UNKNOWN, NOK
*/
int cmd_close(char *request, struct addrinfo *res) {
    char *response = (char *) malloc(sizeof(char) * 512);
    if(response == NULL) {
        fprintf(stderr, "Error while communicating with server! Ending session.\n");
        return 1;
    }

    char res_cmd[4];
    char status[10];

    if(send_tcp_request(request, res, response, 512) == 1) {
        fprintf(stderr, "Error while communicating with server! Ending session.\n");
        free(response);
        return 1;
    }

    if(sscanf(response, "%3s %9s", res_cmd, status) < 2) {
        fprintf(stderr, "Invalid response from server! Ending session.\n");
        free(response);
        return 1;
    }

    if(strcmp(res_cmd, "RCL") != 0) {
        fprintf(stderr, "Invalid response from server! Ending session.\n");
        free(response);
        return 1;
    }

    if(validate_string_spaces(response, 0) == 1) {
        fprintf(stderr, "Invalid response from server! Ending session.\n");
        free(response);
        return 1;
    }

    if(strcmp(status, "OK") == 0) {
        printf("Event closed successfully. No more reservations accepted.\n");
    } else if(strcmp(status, "NOK") == 0) {
        printf("Unknown user or incorrect password.\n");
    } else if(strcmp(status, "NLG") == 0) {
        printf("User not logged in.\n");
    } else if(strcmp(status, "NOE") == 0) {
        printf("Event does not exist.\n");
    } else if(strcmp(status, "EOW") == 0) {
        printf("You are not the owner of this event.\n");
    } else if(strcmp(status, "SLD") == 0) {
        printf("Event is already sold out.\n");
    } else if(strcmp(status, "PST") == 0) {
        printf("Event date already passed.\n");
    } else if(strcmp(status, "CLO") == 0) {
        printf("Event was already closed.\n");
    } else if(strcmp(status, "ERR") == 0) {
        fprintf(stderr, "Invalid arguments!\n");
    } else {
        fprintf(stderr, "Invalid response from server! Ending session.\n");
        free(response);
        return 1;
    }

    free(response);
    return 0;
}

/* cmd_myevents (mye)
   Uses UDP. Request: "MYE UID\n"
   Response: "RME <STATUS> [list-data]\n" STATUS: OK, EMPTY, UNKNOWN, ERR
   If OK, server includes a textual list which we print (response buffer small).
*/
int cmd_myevents(char *request, int fd_udp, struct addrinfo *res) {
    char response[512]; /* list might be longer but UDP server should fit in datagram; adjust as needed */
    char res_cmd[4];
    char status[8];

    if(send_udp_request(request, fd_udp, res, response) == 1) {
        fprintf(stderr, "Error while communicating with server! Ending session.\n");
        return 1;
    }

    if(sscanf(response, "%3s %7s", res_cmd, status) < 2 ||
       validate_string_spaces(response, 0) == 1) {
        fprintf(stderr, "Invalid response from server! Ending session.\n");
        return 1;
    }

    if(strcmp(res_cmd, "RME") != 0) {
        fprintf(stderr, "Invalid response from server! Ending session.\n");
        return 1;
    }

    if(strcmp(status, "OK") == 0) {
        /* Print everything after the two tokens */
        char *payload = strchr(response, ' ');
        if (payload != NULL) {
            payload = strchr(payload + 1, ' ');
            if (payload != NULL) {
                printf("%s\n", payload + 1);
            } else {
                printf("(no events)\n");
            }
        } else {
            printf("(no events)\n");
        }
    } else if(strcmp(status, "NOK") == 0) {
        printf("You have not created any active events.\n");
    } else if(strcmp(status, "NLG") == 0) {
        printf("User not logged in.\n");
    } else if(strcmp(status, "WRP") == 0) {
        printf("Incorrect password.\n");
    } else {
        fprintf(stderr, "Invalid response from server! Ending session.\n");
        return 1;
    }

    return 0;
}

/* cmd_list
   Uses TCP. Request: "LST\n" or "LST UID\n" depending on parser.
   Response: "RLS <STATUS>\n" with list data appended, or status EMPTY/ERR
*/
int cmd_list(char *request, struct addrinfo *res) {
    char *response = (char *) malloc(sizeof(char) * 2048);
    if(response == NULL) {
        fprintf(stderr, "Error while communicating with server! Ending session.\n");
        return 1;
    }

    char res_cmd[4];
    char status[8];

    if(send_tcp_request(request, res, response, 2048) == 1) {
        fprintf(stderr, "Error while communicating with server! Ending session.\n");
        free(response);
        return 1;
    }

    if(sscanf(response, "%3s %7s", res_cmd, status) < 2) {
        fprintf(stderr, "Invalid response from server! Ending session.\n");
        free(response);
        return 1;
    }

    if(strcmp(res_cmd, "RLS") != 0) {
        fprintf(stderr, "Invalid response from server! Ending session.\n");
        free(response);
        return 1;
    }

    if(strcmp(status, "OK") == 0) {
        /* print list payload after the two tokens */
        char *payload = strchr(response, ' ');
        if (payload != NULL) {
            payload = strchr(payload + 1, ' ');
            if (payload != NULL) {
                printf("%s\n", payload + 1);
            } else {
                printf("(no events)\n");
            }
        } else {
            printf("(no events)\n");
        }
    } else if(strcmp(status, "NOK") == 0) {
        printf("No events have been created.\n");
    } else if(strcmp(status, "ERR") == 0) {
        fprintf(stderr, "Invalid arguments!\n");
        free(response);
        return 1;
    } else {
        fprintf(stderr, "Invalid response from server! Ending session.\n");
        free(response);
        return 1;
    }

    free(response);
    return 0;
}

/* cmd_show
   Uses TCP. Request: "SHW EID\n"
   Response: "RSH <STATUS> name date seats reserved fname fsize <file-data>\n"
   Or error statuses: ERR, UNKNOWN, NOK
   We'll parse name/date/seats/reserved and then extract the file data similarly to earlier examples.
*/
int cmd_show(char *request, struct addrinfo *res) {
    char *response = (char *) malloc(sizeof(char) * 8192); /* larger to receive file */
    if(response == NULL) {
        fprintf(stderr, "Error while communicating with server! Ending session.\n");
        return 1;
    }

    char res_cmd[4];
    char status[8];
    char name[64];
    char date[16];
    unsigned int seats = 0;
    unsigned int reserved = 0;
    char fname[256];
    unsigned int fsize = 0;

    if(send_tcp_request(request, res, response, 8192) == 1) {
        fprintf(stderr, "Error while communicating with server! Ending session.\n");
        free(response);
        return 1;
    }

    if(sscanf(response, "%3s %7s", res_cmd, status) < 2) {
        fprintf(stderr, "Invalid response from server! Ending session.\n");
        free(response);
        return 1;
    }

    if(strcmp(res_cmd, "RSE") != 0) {
        fprintf(stderr, "Invalid response from server! Ending session.\n");
        free(response);
        return 1;
    }

    if(strcmp(status, "OK") == 0) {
        /* Parse header fields (UID, name, date, attendance, reserved, fname, fsize) */
        unsigned int uid;
        if(sscanf(response, "%*s %*s %u %63s %15s %u %u %255s %u",
                &uid, name, date, &seats, &reserved, fname, &fsize) < 7) {
            fprintf(stderr, "Invalid response from server! Ending session.\n");
            free(response);
            return 1;
        }

        /* locate file data: Fdata starts after the header */
        char *f_data = strstr(response, fname);
        if(f_data == NULL) {
            fprintf(stderr, "Cannot locate file data in response! Ending session.\n");
            free(response);
            return 1;
        }
        /* Move pointer past filename and space to start of file content */
        f_data += strlen(fname) + 1; 
        /* now f_data points to file content; write only fsize bytes */
        if(create_file(fname, f_data) == 1) {
            fprintf(stderr, "Error creating file. Ending session.\n");
            free(response);
            return 1;
        }

        printf("Event: %s\nDate: %s\nSeats: %u\nReserved: %u\nFile: %s (size %u bytes)\nSaved to: %s\n",
            name, date, seats, reserved, fname, fsize, fname);

    } else if(strcmp(status, "NOK") == 0) {
        printf("Event does not exist or cannot send file.\n");
    } else if(strcmp(status, "ERR") == 0) {
        fprintf(stderr, "Invalid arguments!\n");
        free(response);
        return 1;
    } else {
        fprintf(stderr, "Invalid response from server! Ending session.\n");
        free(response);
        return 1;
    }


    free(response);
    return 0;
}

/* cmd_reserve
   Uses TCP. Request: "RES UID EID value\n"
   Response: "RSR <STATUS> [remaining]\n" STATUS: OK, REFUSED, CLOSED, UNKNOWN, ERR
   If REFUSED server may include number of seats still available.
*/
int cmd_reserve(char *request, struct addrinfo *res) {
    char *response = (char *) malloc(sizeof(char) * 512);
    if(response == NULL) {
        fprintf(stderr, "Error while communicating with server! Ending session.\n");
        return 1;
    }

    char res_cmd[4];
    char status[10];
    unsigned int remaining = 0;
    char extra[2];

    if(send_tcp_request(request, res, response, 512) == 1) {
        fprintf(stderr, "Error while communicating with server! Ending session.\n");
        free(response);
        return 1;
    }

    if(sscanf(response, "%3s %9s %u %1s", res_cmd, status, &remaining, extra) < 2) {
        fprintf(stderr, "Invalid response from server! Ending session.\n");
        free(response);
        return 1;
    }

    if(strcmp(res_cmd, "RRI") != 0) {
        fprintf(stderr, "Invalid response from server! Ending session.\n");
        free(response);
        return 1;
    }

    if(strcmp(status, "ACC") == 0) {
        printf("Reservation accepted.\n");
    } else if(strcmp(status, "REJ") == 0) {
        /* extract remaining seats from payload */
        unsigned int remaining = 0;
        if(sscanf(response, "%*s %u", &remaining) == 1) {
            printf("Reservation rejected. Seats remaining: %u\n", remaining);
        } else {
            printf("Reservation rejected. Remaining seats unknown.\n");
        }
    } else if(strcmp(status, "CLS") == 0) {
        printf("Event is not accepting reservations (closed).\n");
    } else if(strcmp(status, "SLD") == 0) {
        printf("Event is sold out.\n");
    } else if(strcmp(status, "NOK") == 0) {
        printf("Event not active.\n");
    } else if(strcmp(status, "NLG") == 0) {
        printf("User not logged in.\n");
    } else if(strcmp(status, "PST") == 0) {
        printf("Event date has passed.\n");
    } else if(strcmp(status, "WRP") == 0) {
        printf("Wrong password.\n");
    } else if(strcmp(status, "ERR") == 0) {
        fprintf(stderr, "Invalid arguments!\n");
    } else {
        fprintf(stderr, "Invalid response from server! Ending session.\n");
        free(response);
        return 1;
    }

    free(response);
    return 0;
}

/* cmd_myreservations (myr)
   Uses UDP. Request: "MYR UID\n"
   Response: "RMR <STATUS> [list]\n" STATUS: OK, EMPTY, UNKNOWN, ERR
*/
int cmd_myreservations(char *request, int fd_udp, struct addrinfo *res) {
    char response[512];
    char res_cmd[4];
    char status[8];

    if(send_udp_request(request, fd_udp, res, response) == 1) {
        fprintf(stderr, "Error while communicating with server! Ending session.\n");
        return 1;
    }

    if(sscanf(response, "%3s %7s", res_cmd, status) < 2 ||
       validate_string_spaces(response, 0) == 1) {
        fprintf(stderr, "Invalid response from server! Ending session.\n");
        return 1;
    }

    if(strcmp(res_cmd, "RMR") != 0) {
        fprintf(stderr, "Invalid response from server! Ending session.\n");
        return 1;
    }

    if(strcmp(status, "OK") == 0) {
        char *payload = strchr(response, ' ');
        if (payload != NULL) {
            payload = strchr(payload + 1, ' ');
            if (payload != NULL) {
                printf("%s\n", payload + 1);
            } else {
                printf("(no reservations)\n");
            }
        } else {
            printf("(no reservations)\n");
        }
    } else if(strcmp(status, "NOK") == 0) {
        printf("You have not made any reservations.\n");
    } else if(strcmp(status, "NLG") == 0) {
        printf("User not logged in.\n");
    } else if(strcmp(status, "WRP") == 0) {
        printf("Incorrect password.\n");
    } else {
        fprintf(stderr, "Invalid response from server! Ending session.\n");
        return 1;
    }

    return 0;
}

/* cmd_exit is a local command and usually handled by user.c. We include a small helper
   that returns 0 for normal exit or 2 to indicate user must logout first (if logged_in). */

int cmd_exit(int logged_in) {
    if(logged_in) {
        printf("You are still logged in — please logout first.\n");
        return 2; /* indicate cannot exit yet */
    }
    printf("Exiting.\n");
    return 0;
}