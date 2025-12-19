/* server_event.c  -- adapted to the Event Reservation protocol (UDP + TCP)
   Uses parser_server.h and commands.h for parsing and business logic.
   Make sure to implement/adjust the parse_* and cmd_* functions described
   in the comments above to match your project's existing headers.
*/

#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <errno.h>
extern int errno;

#include "commands.h"
#include "parser_server.h"

#define DEFAULT_PORT "58068"
#define PORT_STRLEN 6

/* Helper: read exactly 'size' bytes from fd into buf.
   Returns 1 on success, 0 on error.
   Based on robust reading pattern from reference implementation. */
static int read_exact(int fd, char *buf, size_t size) {
    ssize_t nread;
    char *ptr = buf;
    while (size > 0) {
        nread = read(fd, ptr, size);
        if (nread == -1) {
            if (errno == EINTR) continue; /* interrupted, retry */
            return 0; /* error */
        }
        if (nread == 0) {
            return 0; /* premature EOF */
        }
        size -= (size_t)nread;
        ptr += nread;
    }
    return 1;
}

/* Helper: write exactly 'size' bytes from buf to fd.
   Returns 1 on success, 0 on error.
   Based on robust writing pattern from reference implementation. */
static int write_exact(int fd, const char *buf, size_t size) {
    ssize_t nwritten;
    const char *ptr = buf;
    while (size > 0) {
        nwritten = write(fd, ptr, size);
        if (nwritten == -1) {
            if (errno == EINTR) continue; /* interrupted, retry */
            return 0; /* error */
        }
        size -= (size_t)nwritten;
        ptr += nwritten;
    }
    return 1;
}


void get_addr_info(char* ESport, struct addrinfo **res_udp, struct addrinfo **res_tcp) {
    struct addrinfo hints;
    int errcode;

    memset(&hints,0,sizeof hints);
    hints.ai_family=AF_INET; //IPv4
    hints.ai_socktype=SOCK_DGRAM; // UDP
    hints.ai_flags=AI_PASSIVE;

    if((errcode = getaddrinfo(NULL, ESport, &hints, res_udp)) != 0) {
        fprintf(stderr, "Error: getaddrinfo: %s\n", gai_strerror(errcode));
        exit(1);
    }


    memset(&hints,0,sizeof hints);
    hints.ai_family=AF_INET; //IPv4
    hints.ai_socktype=SOCK_STREAM; // TCP
    hints.ai_flags=AI_PASSIVE;

    if((errcode = getaddrinfo(NULL, ESport, &hints, res_tcp)) != 0) {
        fprintf(stderr, "Error: getaddrinfo: %s\n", gai_strerror(errcode));
        freeaddrinfo(*res_udp);
        exit(1);
    }

    return;
}

/* UDP handler implementing the User-ES protocol in UDP:
   LIN, LOU, UNR, LME, LMR
   For each request we rely on parse_* and cmd_* to perform parsing and to
   fill response (response must end with '\n').
*/
int server_udp(int fd_udp, int verbose) {
    char buffer[2048];
    char command[8];
    char response[2048];
    char status[16];
    char host[NI_MAXHOST], port[NI_MAXSERV];
    int invalid_req = 0;

    struct sockaddr_in addr;
    socklen_t addrlen = sizeof(addr);

    ssize_t bytes_received = recvfrom(fd_udp, buffer, sizeof(buffer)-1, 0, (struct sockaddr*)&addr, &addrlen);
    if(bytes_received == -1) {
        fprintf(stderr, "Error communicating with client (recvfrom): %s\n", strerror(errno));
        return 1;
    }
    buffer[bytes_received] = '\0';

    if(verbose) {
        if(getnameinfo((struct sockaddr *)&addr, addrlen, host, sizeof host, port, sizeof port, 0) == 0) {
            printf("UDP request sent by [%s:%s]\n", host, port);
        } else
            fprintf(stderr, "Error while getting hostname\n");
    }

    if(sscanf(buffer, "%7s", command) != 1) {
        invalid_req = 2;
        strcpy(response, "ERR\n");
    }

    if(invalid_req != 2) {
        /* Handle commands */
        if(strcmp(command, "LIN") == 0) {
            char uid[8] = {0}; /* 6 digits + '\0' */
            char password[128] = {0};
            if(parse_lin(buffer, uid, password) == 0) {
                if(cmd_lin(response, uid, password) == 1) {
                    /* fatal internal error */
                    return 1;
                }
            } else {
                invalid_req = 1;
                strcpy(response, "RLI ERR\n");
            }
        }
        else if(strcmp(command, "LOU") == 0) {
            char uid[8] = {0};
            char password[128] = {0};
            if(parse_lou(buffer, uid, password) == 0) {
                if(cmd_lou(response, uid, password) == 1)
                    return 1;
            } else {
                invalid_req = 1;
                strcpy(response, "RLO ERR\n");
            }
        }
        else if(strcmp(command, "UNR") == 0) {
            char uid[8] = {0};
            char password[128] = {0};
            if(parse_unr(buffer, uid, password) == 0) {
                if(cmd_unr(response, uid, password) == 1)
                    return 1;
            } else {
                invalid_req = 1;
                strcpy(response, "RUR ERR\n");
            }
        }
        else if(strcmp(command, "LME") == 0) {
            char uid[8] = {0};
            char password[128] = {0};
            if(parse_lme(buffer, uid, password) == 0) {
                if(cmd_lme(response, uid, password) == 1)
                    return 1;
            } else {
                invalid_req = 1;
                strcpy(response, "RME ERR\n");
            }
        }
        else if(strcmp(command, "LMR") == 0) {
            char uid[8] = {0};
            char password[128] = {0};
            if(parse_lmr(buffer, uid, password) == 0) {
                if(cmd_lmr(response, uid, password) == 1)
                    return 1;
            } else {
                invalid_req = 1;
                strcpy(response, "RMR ERR\n");
            }
        }
        else {
            invalid_req = 1;
            strcpy(response, "ERR\n");
        }
    }

    if(verbose) {
        if(invalid_req == 1)
            printf("The client sent an invalid request.\nSending 'ERR'\n\n");
        else if(invalid_req == 2)
            printf("The client didn't type a command.\nSending 'ERR'\n\n");
        else {
            /* extract status for logging if possible */
            if(sscanf(response, "%*s %15s", status) == 1)
                printf("UDP: Sending response with status '%s'\n\n", status);
            else
                printf("UDP: Sending response: %s\n\n", response);
        }
    }

    if(sendto(fd_udp, response, strlen(response), 0, (struct sockaddr*)&addr, addrlen) < 0) {
        fprintf(stderr, "Error communicating with client (sendto): %s\n", strerror(errno));
        return 1;
    }

    return 0;
}


/* TCP handler supporting the messaging protocol (file transfer over TCP):
   CRE, CLS, LST, SED, RID, CPS
   The TCP handlers rely on parse_* and cmd_* variants that can return a
   dynamically allocated response buffer (with file contents if necessary).
   The server sends that entire buffer back to the client and closes the TCP
   connection (as the protocol requires).
*/
int server_tcp(int fd_tcp, int verbose) {
    char command[8];
    int invalid_req = 0;
    char delim = '\0';
    
    /* Read initial part to identify command - read up to first space or newline */
    char cmd_buf[8];
    size_t cmd_len = 0;
    while (cmd_len < sizeof(cmd_buf) - 1) {
        char c;
        ssize_t r = read(fd_tcp, &c, 1);
        if (r == -1) {
            if (errno == EINTR) continue;
            fprintf(stderr, "Error reading from TCP client\n");
            return 1;
        }
        if (r == 0) {
            delim = '\0';
            break;
        }
        if (c == ' ' || c == '\n') {
            delim = c;
            break;
        }
        cmd_buf[cmd_len++] = c;
    }
    cmd_buf[cmd_len] = '\0';
    
    if (cmd_len == 0) {
        invalid_req = 2;
        char *err_response = "ERR\n";
        write_exact(fd_tcp, err_response, strlen(err_response));
        return 0;
    }
    
    strncpy(command, cmd_buf, sizeof(command) - 1);
    command[sizeof(command) - 1] = '\0';
    
    /* Now read the rest based on command type */
    char *request = NULL;
    size_t req_len = 0;
    
    /* For most commands, read until newline. For CRE, need special handling.
       If the delimiter was a newline, the full request is just "CMD\n". */
    if (strcmp(command, "CRE") == 0) {
        if (delim != ' ') {
            invalid_req = 1;
            char *err_response = "RCE ERR\n";
            write_exact(fd_tcp, err_response, strlen(err_response));
            return 0;
        }
        /* CRE format: CRE UID password name event_date attendance_size Fname Fsize <Fdata>
           Read rest of line first, then read Fdata based on Fsize */
        const size_t MAX_LINE = 8192;
        char *line_buf = malloc(MAX_LINE);
        if (!line_buf) {
            fprintf(stderr, "Memory allocation error\n");
            return 1;
        }
        
        /* Copy "CRE " to start of buffer */
        strcpy(line_buf, command);
        strcat(line_buf, " ");
        size_t line_len = strlen(line_buf);
        
        /* Read until we have all metadata (up to and including Fsize) */
        /* This is tricky - we need to parse as we go to find Fsize, then read Fdata */
        /* Simpler approach: read until space after a number that should be Fsize */
        int space_count = 0;
        while (line_len < MAX_LINE - 1) {
            char c;
            ssize_t r = read(fd_tcp, &c, 1);
            if (r == -1) {
                if (errno == EINTR) continue;
                free(line_buf);
                fprintf(stderr, "Error reading from TCP client\n");
                return 1;
            }
            if (r == 0) break;
            
            line_buf[line_len++] = c;
            
            /* Count spaces to know when we've read enough metadata
               Format: CRE UID password name event_date attendance_size Fname Fsize */
            if (c == ' ') space_count++;
            
            /* After 7th space we have Fsize, read it */
            if (space_count == 7) {
                /* Next chars should be Fsize number, followed by space */
                int fsize = 0;
                char fsize_buf[16];
                int fsize_idx = 0;
                
                while (fsize_idx < 15) {
                    r = read(fd_tcp, &c, 1);
                    if (r <= 0) break;
                    line_buf[line_len++] = c;
                    
                    if (c == ' ') {
                        fsize_buf[fsize_idx] = '\0';
                        fsize = atoi(fsize_buf);
                        break;
                    }
                    fsize_buf[fsize_idx++] = c;
                }
                
                /* Now read fsize bytes of file data */
                if (fsize > 0) {
                    size_t total_needed = line_len + fsize;
                    char *new_buf = realloc(line_buf, total_needed + 1);
                    if (!new_buf) {
                        free(line_buf);
                        fprintf(stderr, "Memory allocation error\n");
                        return 1;
                    }
                    line_buf = new_buf;
                    
                    if (!read_exact(fd_tcp, line_buf + line_len, fsize)) {
                        free(line_buf);
                        fprintf(stderr, "Error reading file data from TCP client\n");
                        return 1;
                    }
                    line_len += fsize;
                }
                break;
            }
        }
        
        line_buf[line_len] = '\0';
        request = line_buf;
        req_len = line_len;
        
    } else if (delim == '\n') {
        /* request is only the command + newline */
        size_t clen = strlen(command);
        request = malloc(clen + 2);
        if (!request) {
            fprintf(stderr, "Memory allocation error\n");
            return 1;
        }
        memcpy(request, command, clen);
        request[clen] = '\n';
        request[clen + 1] = '\0';
        req_len = clen + 1;
    } else {
        /* For other commands, read until newline */
        const size_t MAX_LINE = 8192;
        char *line_buf = malloc(MAX_LINE);
        if (!line_buf) {
            fprintf(stderr, "Memory allocation error\n");
            return 1;
        }
        
        strcpy(line_buf, command);
        /* only append a space if the delimiter was a space */
        if (delim == ' ') strcat(line_buf, " ");
        size_t line_len = strlen(line_buf);
        
        while (line_len < MAX_LINE - 1) {
            char c;
            ssize_t r = read(fd_tcp, &c, 1);
            if (r == -1) {
                if (errno == EINTR) continue;
                free(line_buf);
                fprintf(stderr, "Error reading from TCP client\n");
                return 1;
            }
            if (r == 0) break;
            
            line_buf[line_len++] = c;
            if (c == '\n') break;
        }
        
        line_buf[line_len] = '\0';
        request = line_buf;
        req_len = line_len;
    }

    char *response_buf = NULL;
    size_t response_len = 0;

    if(invalid_req != 2) {
        if(strcmp(command, "CRE") == 0) {
            /* CRE format: CRE UID password name event_date attendance_size Fname Fsize <Fdata>
               No newline in the middle - Fsize comes right after Fname
            */
            char uid[8] = {0}, password[128] = {0}, name[16] = {0}, event_date[17] = {0}, fname[32] = {0};
            int attendance_size = 0;
            
            /* Manual parsing because event_date has space (16 chars: dd-mm-yyyy hh:mm) */
            char *p = request;
            
            /* Skip "CRE " */
            while (*p && *p != ' ') p++;
            if (*p == ' ') p++;
            
            /* UID (6 chars) */
            char *uid_start = p;
            while (*p && *p != ' ') p++;
            size_t uid_len = p - uid_start;
            if (uid_len != 6) {
                invalid_req = 1;
                response_buf = strdup("RCE ERR\n");
                response_len = strlen(response_buf);
                goto cre_done;
            }
            memcpy(uid, uid_start, uid_len);
            uid[uid_len] = '\0';
            if (*p == ' ') p++;
            
            /* password */
            char *pass_start = p;
            while (*p && *p != ' ') p++;
            size_t pass_len = p - pass_start;
            if (pass_len >= 128) {
                invalid_req = 1;
                response_buf = strdup("RCE ERR\n");
                response_len = strlen(response_buf);
                goto cre_done;
            }
            memcpy(password, pass_start, pass_len);
            password[pass_len] = '\0';
            if (*p == ' ') p++;
            
            /* name */
            char *name_start = p;
            while (*p && *p != ' ') p++;
            size_t name_len = p - name_start;
            if (name_len > 10) {
                invalid_req = 1;
                response_buf = strdup("RCE ERR\n");
                response_len = strlen(response_buf);
                goto cre_done;
            }
            memcpy(name, name_start, name_len);
            name[name_len] = '\0';
            if (*p == ' ') p++;
            
            /* event_date (16 chars: dd-mm-yyyy hh:mm) */
            if ((size_t)(p - request) + 16 > req_len) {
                invalid_req = 1;
                response_buf = strdup("RCE ERR\n");
                response_len = strlen(response_buf);
                goto cre_done;
            }
            memcpy(event_date, p, 16);
            event_date[16] = '\0';
            p += 16;
            if (*p == ' ') p++;
            
            /* attendance_size */
            int n;
            if (sscanf(p, "%d%n", &attendance_size, &n) != 1) {
                invalid_req = 1;
                response_buf = strdup("RCE ERR\n");
                response_len = strlen(response_buf);
                goto cre_done;
            }
            p += n;
            if (*p == ' ') p++;
            
            /* fname */
            char *fname_start = p;
            while (*p && *p != ' ') p++;
            size_t fname_len = p - fname_start;
            if (fname_len > 24) {
                invalid_req = 1;
                response_buf = strdup("RCE ERR\n");
                response_len = strlen(response_buf);
                goto cre_done;
            }
            memcpy(fname, fname_start, fname_len);
            fname[fname_len] = '\0';
            if (*p == ' ') p++;
            
            /* Fsize */
            int fsize = 0;
            if (sscanf(p, "%d%n", &fsize, &n) != 1) {
                invalid_req = 1;
                response_buf = strdup("RCE ERR\n");
                response_len = strlen(response_buf);
                goto cre_done;
            }
            p += n;
            if (*p == ' ') p++;
            
            /* Fdata (remaining bytes) */
            char *fdata = p;
            size_t available = request + req_len - fdata;
            if ((int)available < fsize) {
                invalid_req = 1;
                response_buf = strdup("RCE ERR\n");
                response_len = strlen(response_buf);
                goto cre_done;
            }
            
            /* Call command handler */
            if(cmd_cre(&response_buf, &response_len, uid, password, name, event_date, attendance_size, fname, fsize, fdata) != 0) {
                free(response_buf);
                response_buf = strdup("RCE NOK\n");
                response_len = strlen(response_buf);
            }
            
            cre_done:;
        }
        else if(strcmp(command, "CLS") == 0) {
            char uid[8] = {0}, password[128] = {0}, eid[8] = {0};
            if(parse_cls(request, uid, password, eid) == 0) {
                if(cmd_cls(&response_buf, &response_len, uid, password, eid) != 0) {
                    free(response_buf);
                    response_buf = strdup("RCL NOK\n");
                    response_len = strlen(response_buf);
                }
            } else {
                invalid_req = 1;
                response_buf = strdup("RCL ERR\n");
                response_len = strlen(response_buf);
            }
        }
        else if(strcmp(command, "LST") == 0) {
            /* no arguments expected */
            if (parse_lst(request) != 0) {
                invalid_req = 1;
                response_buf = strdup("RLS ERR\n");
                response_len = strlen(response_buf);
            }
            else
            if(cmd_lst(&response_buf, &response_len) != 0) {
                free(response_buf);
                response_buf = strdup("RLS NOK\n");
                response_len = strlen(response_buf);
            }
        }
        else if(strcmp(command, "SED") == 0) {
            char eid[8] = {0};
            if(parse_sed(request, eid) == 0) {
                if(cmd_sed(&response_buf, &response_len, eid) != 0) {
                    free(response_buf);
                    response_buf = strdup("RSE NOK\n");
                    response_len = strlen(response_buf);
                }
            } else {
                invalid_req = 1;
                response_buf = strdup("RSE ERR\n");
                response_len = strlen(response_buf);
            }
        }
        else if(strcmp(command, "RID") == 0) {
            char uid[8] = {0}, password[128] = {0}, eid[8] = {0};
            int people = 0;
            if(parse_rid(request, uid, password, eid, &people) == 0) {
                if(cmd_rid(&response_buf, &response_len, uid, password, eid, people) != 0) {
                    free(response_buf);
                    response_buf = strdup("RRI NOK\n");
                    response_len = strlen(response_buf);
                }
            } else {
                invalid_req = 1;
                response_buf = strdup("RRI ERR\n");
                response_len = strlen(response_buf);
            }
        }
        else if(strcmp(command, "CPS") == 0) {
            char uid[8] = {0}, oldpass[128] = {0}, newpass[128] = {0};
            if(parse_cps(request, uid, oldpass, newpass) == 0) {
                if(cmd_cps(&response_buf, &response_len, uid, oldpass, newpass) != 0) {
                    free(response_buf);
                    response_buf = strdup("RCP NOK\n");
                    response_len = strlen(response_buf);
                }
            } else {
                invalid_req = 1;
                response_buf = strdup("RCP ERR\n");
                response_len = strlen(response_buf);
            }
        }
        else {
            invalid_req = 1;
            response_buf = strdup("ERR\n");
            response_len = strlen(response_buf);
        }
    }
    else {
        /* invalid (no command) */
        response_buf = strdup("ERR\n");
        response_len = strlen(response_buf);
    }

    if(verbose) {
        if(invalid_req == 1) {
            printf("The TCP client sent an invalid request. Sending 'ERR' or protocol error response.\n\n");
        } else if(invalid_req == 2) {
            printf("The TCP client didn't type a command. Sending 'ERR'\n\n");
        } else {
            /* attempt to print status word from response */
            char status[32] = {0};
            if(response_buf && sscanf(response_buf, "%*s %31s", status) == 1)
                printf("TCP: Sending response with status '%s'\n\n", status);
            else if(response_buf)
                printf("TCP: Sending response (raw) ...\n\n");
        }
    }

    /* write full response_buf to TCP client using robust write */
    if (response_buf && response_len > 0) {
        if (!write_exact(fd_tcp, response_buf, response_len)) {
            fprintf(stderr, "Error writing to TCP client: %s\n", strerror(errno));
            free(request);
            free(response_buf);
            return 1;
        }
    }

    free(request);
    free(response_buf);

    return 0;
}


int main(int argc, char *argv[]) {
    char ESport[PORT_STRLEN] = DEFAULT_PORT;
    unsigned int verbose = 0;
    struct addrinfo *res_udp, *res_tcp;
    fd_set inputs, test_fds;

    int out_fds;

    switch (argc) {
        case 1:
            break;
        case 2:
            if(strcmp(argv[1], "-v") == 0)
                verbose = 1;
            else {
                fprintf(stderr, "Invalid format. Please use: ./ES [-p ESport] [-v]\n");
                return 1;
            }
            break;
        case 3:
            if(strcmp(argv[1], "-p") == 0)
                strncpy(ESport, argv[2], PORT_STRLEN-1);
            else {
                fprintf(stderr, "Invalid format. Please use: ./ES [-p ESport] [-v]\n");
                return 1;
            }
            break;
        case 4:
            if(strcmp(argv[1], "-v") == 0 && strcmp(argv[2], "-p") == 0){
                verbose = 1;
                strncpy(ESport, argv[3], PORT_STRLEN-1);
            }
            else if(strcmp(argv[1], "-p") == 0 && strcmp(argv[3], "-v") == 0){
                verbose = 1;
                strncpy(ESport, argv[2], PORT_STRLEN-1);
            }
            else{
                fprintf(stderr, "Invalid format. Please use: ./ES [-p ESport] [-v]\n");
                return 1;
            }
            break;
        default:
            fprintf(stderr, "Invalid number of parameters. Please use: ./ES [-p ESport] [-v]\n");
            return 1;
    }

    get_addr_info(ESport, &res_udp, &res_tcp);


    /* UDP socket */
    int fd_udp = socket(AF_INET, SOCK_DGRAM, 0);
    if(fd_udp == -1) {
        freeaddrinfo(res_udp);
        freeaddrinfo(res_tcp);
        fprintf(stderr, "Error in UDP socket creation: %s\n", strerror(errno));
        return 1;
    }

    if(bind(fd_udp, res_udp->ai_addr, res_udp->ai_addrlen) == -1) {
        freeaddrinfo(res_udp);
        freeaddrinfo(res_tcp);
        close(fd_udp);
        fprintf(stderr, "Error binding UDP socket: %s\n", strerror(errno));
        return 1;
    }


    /* TCP socket */
    int fd_tcp = socket(AF_INET, SOCK_STREAM, 0);
    if(fd_tcp == -1) {
        freeaddrinfo(res_udp);
        freeaddrinfo(res_tcp);
        close(fd_udp);
        fprintf(stderr, "Error in TCP socket creation: %s\n", strerror(errno));
        return 1;
    }


    int optval = 1;

    if (setsockopt(fd_tcp, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval)) < 0) {
        freeaddrinfo(res_udp);
        freeaddrinfo(res_tcp);
        close(fd_udp);
        close(fd_tcp);
        fprintf(stderr, "Setsockopt failed\n");
        return 1;
    }


    if(bind(fd_tcp, res_tcp->ai_addr, res_tcp->ai_addrlen) == -1) {
        freeaddrinfo(res_udp);
        freeaddrinfo(res_tcp);
        close(fd_udp);
        close(fd_tcp);
        fprintf(stderr, "Error binding TCP socket: %s\n", strerror(errno));
        return 1;
    }

    if(listen(fd_tcp, 1024) == -1) {
        freeaddrinfo(res_udp);
        freeaddrinfo(res_tcp);
        close(fd_udp);
        close(fd_tcp);
        fprintf(stderr, "Error in listen(): %s\n", strerror(errno));
        return 1;
    }

    /* Initialize persistence system and load data from disk */
    if (init_persistence() == -1) {
        fprintf(stderr, "Warning: Failed to initialize persistence system\n");
    }

    FD_ZERO(&inputs);
    FD_SET(fd_udp, &inputs);
    FD_SET(fd_tcp, &inputs);


    int maxfd = (fd_udp > fd_tcp ? fd_udp : fd_tcp) + 1;

    while (1) {
        test_fds = inputs;

        out_fds = select(maxfd, &test_fds, NULL, NULL, NULL);
        if (out_fds == -1) {
            if (errno == EINTR) {
                continue; /* interrupted by signal, try again */
            }
            fprintf(stderr, "Error in select: %s\n", strerror(errno));
            freeaddrinfo(res_udp);
            freeaddrinfo(res_tcp);
            close(fd_udp);
            close(fd_tcp);
            return 1;
        }

        /* out_fds == 0 won't happen because timeout is NULL (blocking). */
        if (FD_ISSET(fd_udp, &test_fds)) {
            if (server_udp(fd_udp, verbose) == 1) {
                freeaddrinfo(res_udp);
                freeaddrinfo(res_tcp);
                close(fd_udp);
                close(fd_tcp);
                return 1;
            }
        }

        if (FD_ISSET(fd_tcp, &test_fds)) {
            struct sockaddr_in addr;
            socklen_t addrlen = sizeof(addr);

            int new_fd = accept(fd_tcp, (struct sockaddr*)&addr, &addrlen);
            if (new_fd == -1) {
                if (errno == EINTR) {
                    continue; /* accept interrupted, loop back */
                }
                freeaddrinfo(res_udp);
                freeaddrinfo(res_tcp);
                close(fd_udp);
                close(fd_tcp);
                fprintf(stderr, "Error in accept: %s\n", strerror(errno));
                return 1;
            }

            if (verbose) {
                char host[NI_MAXHOST], port[NI_MAXSERV];
                if (getnameinfo((struct sockaddr *)&addr, addrlen, host, sizeof host, port, sizeof port, 0) == 0) {
                    printf("TCP request sent by [%s:%s]\n", host, port);
                } else {
                    fprintf(stderr, "Error while getting hostname\n");
                }
            }

            if (server_tcp(new_fd, verbose) == 1) {
                freeaddrinfo(res_udp);
                freeaddrinfo(res_tcp);
                close(fd_udp);
                close(fd_tcp);
                close(new_fd);
                return 1;
            }

            close(new_fd);
        }
    }


    close(fd_udp);
    close(fd_tcp);
    freeaddrinfo(res_udp);
    freeaddrinfo(res_tcp);

    return 0;
}
