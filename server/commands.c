/* commands.c - simple in-memory implementations of the ES command handlers
 *
 * Matches the server code's expected signatures:
 *  - UDP: int cmd_xxx(char *response, ...); fills response and returns 0 on success, 1 on fatal error.
 *  - TCP: int cmd_xxx(char **response_buf, size_t *response_len, ...); allocates response_buf (malloc),
 *         sets response_len and returns 0 on success, nonzero on error.
 *
 * NOTE: includes basic memory limits tuned for testing. Not production-grade.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <dirent.h>
#include <unistd.h>
#include <errno.h>

#include "commands.h"

/* Base directory for persistent storage */
#define BASE_DIR "ESDIR"
#define USERS_DIR BASE_DIR "/USERS"
#define EVENTS_DIR BASE_DIR "/EVENTS"

/* Dynamic data structures */
typedef struct UserNode {
    User data;
    struct UserNode *next;
} UserNode;

typedef struct EventNode {
    Event data;
    struct EventNode *next;
} EventNode;

typedef struct ReservationNode {
    Reservation data;
    struct ReservationNode *next;
} ReservationNode;

static UserNode *users_head = NULL;
static EventNode *events_head = NULL;
static ReservationNode *reservations_head = NULL;
static int next_eid = 1;

/* ========== HELPER FUNCTIONS ========== */

/* Get current timestamp in dd-mm-yyyy hh:mm:ss format */
static void get_current_timestamp(char *out) {
    time_t now = time(NULL);
    struct tm *t = localtime(&now);
    snprintf(out, 20, "%02d-%02d-%04d %02d:%02d:%02d",
             t->tm_mday, t->tm_mon + 1, t->tm_year + 1900,
             t->tm_hour, t->tm_min, t->tm_sec);
}

/* Get current date+time in dd-mm-yyyy hh:mm format (for event date comparisons) */
static void get_current_datetime(char *out) {
    time_t now = time(NULL);
    struct tm *t = localtime(&now);
    snprintf(out, 17, "%02d-%02d-%04d %02d:%02d",
             t->tm_mday, t->tm_mon + 1, t->tm_year + 1900,
             t->tm_hour, t->tm_min);
}

/* Find user by UID */
static User *find_user(const char *uid) {
    UserNode *curr = users_head;
    while (curr) {
        if (curr->data.used && strcmp(curr->data.uid, uid) == 0) 
            return &curr->data;
        curr = curr->next;
    }
    return NULL;
}

/* Find event by EID string */
static Event *find_event_by_eid_str(const char *eidstr) {
    if (!eidstr) return NULL;
    if (strlen(eidstr) != 3) return NULL;
    int eid = atoi(eidstr);
    if (eid <= 0) return NULL;
    
    EventNode *curr = events_head;
    while (curr) {
        if (curr->data.used && curr->data.eid == eid) 
            return &curr->data;
        curr = curr->next;
    }
    return NULL;
}

/* Create new event slot */
static Event *create_event_slot(void) {
    if (next_eid > MAX_EID) return NULL;  /* Reached EID limit (999) */
    
    EventNode *new_node = malloc(sizeof(EventNode));
    if (!new_node) return NULL;
    
    memset(&new_node->data, 0, sizeof(Event));
    new_node->data.used = 1;
    new_node->data.fdata = NULL;
    new_node->data.seats_reserved = 0;
    new_node->data.closed = 0;
    new_node->data.eid = next_eid++;
    if (next_eid > MAX_EID) next_eid = 1;  /* Wrap around */
    
    new_node->next = events_head;
    events_head = new_node;
    
    return &new_node->data;
}

/* Free event data */
static void free_event(Event *e) {
    if (!e) return;
    if (e->fdata) {
        free(e->fdata);
        e->fdata = NULL;
    }
    memset(e, 0, sizeof(Event));
}

/* Create new reservation slot */
static Reservation *create_reservation_slot(void) {
    ReservationNode *new_node = malloc(sizeof(ReservationNode));
    if (!new_node) return NULL;
    
    memset(&new_node->data, 0, sizeof(Reservation));
    new_node->data.used = 1;
    
    new_node->next = reservations_head;
    reservations_head = new_node;
    
    return &new_node->data;
}

/* Compare date+time. Format: dd-mm-yyyy hh:mm 
   Returns: -1 if d1 < d2, 0 if equal, 1 if d1 > d2
   Direct comparison of components: year, month, day, hour, minute
*/
static int cmp_date(const char *d1, const char *d2) {
    if (!d1 || !d2) return 0;
    if (strlen(d1) != 16 || strlen(d2) != 16) return 0;
    
    /* Compare year (positions 6-9) */
    int cmp = strncmp(d1 + 6, d2 + 6, 4);
    if (cmp != 0) return (cmp < 0) ? -1 : 1;
    
    /* Compare month (positions 3-4) */
    cmp = strncmp(d1 + 3, d2 + 3, 2);
    if (cmp != 0) return (cmp < 0) ? -1 : 1;
    
    /* Compare day (positions 0-1) */
    cmp = strncmp(d1, d2, 2);
    if (cmp != 0) return (cmp < 0) ? -1 : 1;
    
    /* Compare hour (positions 11-12) */
    cmp = strncmp(d1 + 11, d2 + 11, 2);
    if (cmp != 0) return (cmp < 0) ? -1 : 1;
    
    /* Compare minute (positions 14-15) */
    cmp = strncmp(d1 + 14, d2 + 14, 2);
    return (cmp < 0) ? -1 : (cmp > 0) ? 1 : 0;
}

/* Helper to determine whether date checks should be bypassed.
   Set environment variable ES_ALLOW_PAST (any value) to bypass checks. */
/* (previously had an ES_ALLOW_PAST toggle; date checks are now permanent)
   No helper required. */

/* ========== FILE PERSISTENCE FUNCTIONS ========== */

/* Create directory if it doesn't exist */
static int ensure_dir(const char *path) {
    struct stat st = {0};
    if (stat(path, &st) == -1) {
        if (mkdir(path, 0700) == -1) {
            return -1;
        }
    }
    return 0;
}

/* Initialize directory structure */
static int init_storage(void) {
    if (ensure_dir(BASE_DIR) == -1) return -1;
    if (ensure_dir(USERS_DIR) == -1) return -1;
    if (ensure_dir(EVENTS_DIR) == -1) return -1;
    return 0;
}

/* Save user password */
static int save_user_password(const char *uid, const char *password) {
    char user_dir[512];
    char pass_file[512];
    char created_dir[512];
    char reserved_dir[512];
    
    snprintf(user_dir, sizeof user_dir, "%s/%s", USERS_DIR, uid);
    if (ensure_dir(user_dir) == -1) return -1;
    
    /* Create CREATED and RESERVED subdirectories */
    snprintf(created_dir, sizeof created_dir, "%s/CREATED", user_dir);
    snprintf(reserved_dir, sizeof reserved_dir, "%s/RESERVED", user_dir);
    ensure_dir(created_dir);
    ensure_dir(reserved_dir);
    
    snprintf(pass_file, sizeof pass_file, "%s/%s_pass.txt", user_dir, uid);
    FILE *f = fopen(pass_file, "w");
    if (!f) return -1;
    fprintf(f, "%s\n", password);
    fclose(f);
    return 0;
}

/* Load user password */
static int load_user_password(const char *uid, char *password, size_t max_len) {
    char pass_file[512];
    snprintf(pass_file, sizeof pass_file, "%s/%s/%s_pass.txt", USERS_DIR, uid, uid);
    
    FILE *f = fopen(pass_file, "r");
    if (!f) return -1;
    
    if (fgets(password, max_len, f) == NULL) {
        fclose(f);
        return -1;
    }
    
    /* Remove newline */
    size_t len = strlen(password);
    if (len > 0 && password[len-1] == '\n') password[len-1] = '\0';
    
    fclose(f);
    return 0;
}

/* Create login marker */
static int mark_user_logged_in(const char *uid) {
    char login_file[512];
    snprintf(login_file, sizeof login_file, "%s/%s/%s_login.txt", USERS_DIR, uid, uid);
    
    FILE *f = fopen(login_file, "w");
    if (!f) return -1;
    fprintf(f, "logged_in\n");
    fclose(f);
    return 0;
}

/* Remove login marker */
static int mark_user_logged_out(const char *uid) {
    char login_file[512];
    snprintf(login_file, sizeof login_file, "%s/%s/%s_login.txt", USERS_DIR, uid, uid);
    unlink(login_file);  /* Remove file */
    return 0;
}

/* Delete user password (for unregister) */
static int delete_user_password(const char *uid) {
    char pass_file[512];
    snprintf(pass_file, sizeof pass_file, "%s/%s/%s_pass.txt", USERS_DIR, uid, uid);
    unlink(pass_file);
    
    char login_file[512];
    snprintf(login_file, sizeof login_file, "%s/%s/%s_login.txt", USERS_DIR, uid, uid);
    unlink(login_file);
    
    return 0;
}

/* Save event to START file */
static int save_event_start(const Event *ev) {
    char event_dir[512];
    char start_file[512];
    char desc_dir[512];
    char desc_file[512];
    char res_file[512];
    
    snprintf(event_dir, sizeof event_dir, "%s/%03d", EVENTS_DIR, ev->eid);
    if (ensure_dir(event_dir) == -1) return -1;
    
    /* Save START file */
    snprintf(start_file, sizeof start_file, "%s/START_%03d.txt", event_dir, ev->eid);
    FILE *f = fopen(start_file, "w");
    if (!f) return -1;
    fprintf(f, "%s %s %s %d %s\n", ev->owner, ev->name, ev->fname, ev->attendance_size, ev->event_date);
    fclose(f);
    
    /* Save RES file (initially 0) */
    snprintf(res_file, sizeof res_file, "%s/RES_%03d.txt", event_dir, ev->eid);
    f = fopen(res_file, "w");
    if (!f) return -1;
    fprintf(f, "%d\n", ev->seats_reserved);
    fclose(f);
    
    /* Create DESCRIPTION dir and save file */
    snprintf(desc_dir, sizeof desc_dir, "%s/DESCRIPTION", event_dir);
    if (ensure_dir(desc_dir) == -1) return -1;
    
    snprintf(desc_file, sizeof desc_file, "%s/%s", desc_dir, ev->fname);
    f = fopen(desc_file, "wb");
    if (!f) return -1;
    if (ev->fsize > 0 && ev->fdata) {
        fwrite(ev->fdata, 1, ev->fsize, f);
    }
    fclose(f);
    
    /* Create RESERVATIONS dir */
    char res_dir[512];
    snprintf(res_dir, sizeof res_dir, "%s/RESERVATIONS", event_dir);
    ensure_dir(res_dir);
    
    /* Add to user's CREATED dir */
    char user_created[512];
    char created_file[512];
    snprintf(user_created, sizeof user_created, "%s/%s/CREATED", USERS_DIR, ev->owner);
    ensure_dir(user_created);
    snprintf(created_file, sizeof created_file, "%s/%03d.txt", user_created, ev->eid);
    f = fopen(created_file, "w");
    if (f) {
        fprintf(f, "%03d\n", ev->eid);
        fclose(f);
    }
    
    return 0;
}

/* Update RES file */
static int update_event_reservations(int eid, int total_reserved) {
    char res_file[512];
    snprintf(res_file, sizeof res_file, "%s/%03d/RES_%03d.txt", EVENTS_DIR, eid, eid);
    
    FILE *f = fopen(res_file, "w");
    if (!f) return -1;
    fprintf(f, "%d\n", total_reserved);
    fclose(f);
    return 0;
}

/* Save reservation */
static int save_reservation(const Reservation *r) {
    /* Format: R-UID-YYYY-MM-DD-HHMMSS.txt */
    char timestamp_file[256];
    
    /* Convert timestamp dd-mm-yyyy hh:mm:ss to YYYY-MM-DD HHMMSS */
    /* timestamp format: dd-mm-yyyy hh:mm:ss (19 chars) */
    if (strlen(r->timestamp) < 19) return -1;
    
    char yyyy[5], mm[3], dd[3], hh[3], min[3], ss[3];
    strncpy(dd, r->timestamp + 0, 2); dd[2] = '\0';
    strncpy(mm, r->timestamp + 3, 2); mm[2] = '\0';
    strncpy(yyyy, r->timestamp + 6, 4); yyyy[4] = '\0';
    strncpy(hh, r->timestamp + 11, 2); hh[2] = '\0';
    strncpy(min, r->timestamp + 14, 2); min[2] = '\0';
    strncpy(ss, r->timestamp + 17, 2); ss[2] = '\0';
    
    snprintf(timestamp_file, sizeof timestamp_file, "R-%s-%s%s%s_%s%s%s.txt",
             r->uid, yyyy, mm, dd, hh, min, ss);
    
    /* Save to event RESERVATIONS dir */
    char event_res_file[512];
    snprintf(event_res_file, sizeof event_res_file, "%s/%03d/RESERVATIONS/%s",
             EVENTS_DIR, r->eid, timestamp_file);
    
    FILE *f = fopen(event_res_file, "w");
    if (!f) return -1;
    fprintf(f, "%s %d %s\n", r->uid, r->people, r->timestamp);
    fclose(f);
    
    /* Save to user RESERVED dir */
    char user_reserved_dir[512];
    char user_res_file[512];
    snprintf(user_reserved_dir, sizeof user_reserved_dir, "%s/%s/RESERVED", USERS_DIR, r->uid);
    ensure_dir(user_reserved_dir);
    
    snprintf(user_res_file, sizeof user_res_file, "%s/%s", user_reserved_dir, timestamp_file);
    f = fopen(user_res_file, "w");
    if (!f) return 0;  /* Not critical if user copy fails */
    fprintf(f, "%s %d %s\n", r->uid, r->people, r->timestamp);
    fclose(f);
    
    return 0;
}

/* Close event (create END file) */
static int close_event_file(int eid) {
    char end_file[512];
    char timestamp[20];
    
    get_current_timestamp(timestamp);
    
    snprintf(end_file, sizeof end_file, "%s/%03d/END_%03d.txt", EVENTS_DIR, eid, eid);
    FILE *f = fopen(end_file, "w");
    if (!f) return -1;
    fprintf(f, "%s\n", timestamp);
    fclose(f);
    return 0;
}

/* Load all events from disk on startup */
static int load_events_from_disk(void) {
    DIR *events_dir = opendir(EVENTS_DIR);
    if (!events_dir) return 0;  /* No events yet */
    
    struct dirent *entry;
    while ((entry = readdir(events_dir)) != NULL) {
        if (entry->d_name[0] == '.') continue;
        
        int eid = atoi(entry->d_name);
        if (eid <= 0 || eid > 999) continue;
        
        /* Read START file */
        char start_file[512];
        snprintf(start_file, sizeof start_file, "%s/%03d/START_%03d.txt", EVENTS_DIR, eid, eid);
        
        FILE *f = fopen(start_file, "r");
        if (!f) continue;
        
        Event *ev = create_event_slot();
        if (!ev) { fclose(f); continue; }
        
        ev->eid = eid;
        if (eid >= next_eid) next_eid = eid + 1;
        
        char owner[8], name[16], fname[32], date[32];
        int attendance;
        if (fscanf(f, "%7s %15s %31s %d %31[^\n]", owner, name, fname, &attendance, date) == 5) {
            strncpy(ev->owner, owner, 6); ev->owner[6] = '\0';
            strncpy(ev->name, name, 10); ev->name[10] = '\0';
            strncpy(ev->fname, fname, MAX_FILENAME_LEN); ev->fname[MAX_FILENAME_LEN] = '\0';
            strncpy(ev->event_date, date, 16); ev->event_date[16] = '\0';
            ev->attendance_size = attendance;
        }
        fclose(f);
        
        /* Read RES file */
        char res_file[512];
        snprintf(res_file, sizeof res_file, "%s/%03d/RES_%03d.txt", EVENTS_DIR, eid, eid);
        f = fopen(res_file, "r");
        if (f) {
            fscanf(f, "%d", &ev->seats_reserved);
            fclose(f);
        }
        
        /* Check if closed */
        char end_file[512];
        snprintf(end_file, sizeof end_file, "%s/%03d/END_%03d.txt", EVENTS_DIR, eid, eid);
        ev->closed = (access(end_file, F_OK) == 0) ? 1 : 0;
        
        /* Load file data */
        char desc_file[512];
        snprintf(desc_file, sizeof desc_file, "%s/%03d/DESCRIPTION/%s", EVENTS_DIR, eid, ev->fname);
        f = fopen(desc_file, "rb");
        if (f) {
            fseek(f, 0, SEEK_END);
            ev->fsize = ftell(f);
            rewind(f);
            if (ev->fsize > 0 && ev->fsize <= MAX_FDATA_SIZE) {
                ev->fdata = malloc(ev->fsize);
                if (ev->fdata) {
                    fread(ev->fdata, 1, ev->fsize, f);
                }
            }
            fclose(f);
        }
    }
    
    closedir(events_dir);
    return 0;
}

/* Load all users from disk on startup */
static int load_users_from_disk(void) {
    DIR *users_dir = opendir(USERS_DIR);
    if (!users_dir) return 0;
    
    struct dirent *entry;
    while ((entry = readdir(users_dir)) != NULL) {
        if (entry->d_name[0] == '.') continue;
        if (strlen(entry->d_name) != 6) continue;
        
        const char *uid = entry->d_name;
        
        /* Check if password file exists */
        char pass_file[512];
        snprintf(pass_file, sizeof pass_file, "%s/%s/%s_pass.txt", USERS_DIR, uid, uid);
        
        FILE *f = fopen(pass_file, "r");
        if (!f) continue;  /* User unregistered */
        
        /* Find or create user slot */
        User *u = find_user(uid);
        if (!u) {
            /* Create new user node */
            UserNode *new_node = malloc(sizeof(UserNode));
            if (!new_node) {
                fclose(f);
                continue;
            }
            memset(&new_node->data, 0, sizeof(User));
            new_node->data.used = 1;
            strncpy(new_node->data.uid, uid, 6);
            new_node->data.uid[6] = '\0';
            
            new_node->next = users_head;
            users_head = new_node;
            u = &new_node->data;
        }
        
        if (u) {
            char password[128];
            if (fgets(password, sizeof password, f)) {
                size_t len = strlen(password);
                if (len > 0 && password[len-1] == '\n') password[len-1] = '\0';
                strncpy(u->password, password, 127);
                u->password[127] = '\0';
            }
            
            /* Check if logged in (login file exists) */
            char login_file[512];
            snprintf(login_file, sizeof login_file, "%s/%s/%s_login.txt", USERS_DIR, uid, uid);
            u->logged_in = (access(login_file, F_OK) == 0) ? 1 : 0;
        }
        
        fclose(f);
    }
    
    closedir(users_dir);
    return 0;
}

/* Initialize persistence system and load data */
int init_persistence(void) {
    if (init_storage() == -1) {
        fprintf(stderr, "Failed to initialize storage directories\n");
        return -1;
    }
    
    load_users_from_disk();
    load_events_from_disk();
    
    printf("Persistence system initialized. Loaded users and events from disk.\n");
    return 0;
}

/* ========== END FILE PERSISTENCE FUNCTIONS ========== */

/* ---------- UDP command handlers ---------- */

/* LIN UID password -> replies: RLI OK | RLI NOK | RLI REG */
int cmd_lin(char *response, const char *uid, const char *password) {
    if (!response || !uid || !password) return 1;
    
    /* First check if user exists in memory */
    User *u = find_user(uid);
    
    if (!u) {
        /* Check if user exists on disk (previously registered) */
        char saved_password[128];
        if (load_user_password(uid, saved_password, sizeof saved_password) == 0) {
            /* User exists on disk but not in memory - re-login */
            if (strcmp(saved_password, password) != 0) {
                sprintf(response, "RLI NOK\n");
                return 0;
            }
            
            /* Create user in memory */
            UserNode *new_node = malloc(sizeof(UserNode));
            if (!new_node) {
                sprintf(response, "RLI NOK\n");
                return 0;
            }
            memset(&new_node->data, 0, sizeof(User));
            new_node->data.used = 1;
            strncpy(new_node->data.uid, uid, 6); new_node->data.uid[6] = '\0';
            strncpy(new_node->data.password, password, sizeof(new_node->data.password)-1);
            new_node->data.logged_in = 1;
            
            new_node->next = users_head;
            users_head = new_node;
            
            mark_user_logged_in(uid);
            sprintf(response, "RLI OK\n");
            return 0;
        }
        
        /* New user - register and login */
        UserNode *new_node = malloc(sizeof(UserNode));
        if (!new_node) {
            sprintf(response, "RLI NOK\n");
            return 0;
        }
        memset(&new_node->data, 0, sizeof(User));
        new_node->data.used = 1;
        strncpy(new_node->data.uid, uid, 6); new_node->data.uid[6] = '\0';
        strncpy(new_node->data.password, password, sizeof(new_node->data.password)-1);
        new_node->data.logged_in = 1;
        
        new_node->next = users_head;
        users_head = new_node;
        
        /* Save to disk */
        save_user_password(uid, password);
        mark_user_logged_in(uid);
        
        sprintf(response, "RLI REG\n");
        return 0;
    }
    
    /* user exists in memory */
    if (strcmp(u->password, password) != 0) {
        sprintf(response, "RLI NOK\n");
        return 0;
    }
    u->logged_in = 1;
    mark_user_logged_in(uid);
    sprintf(response, "RLI OK\n");
    return 0;
}

/* LOU UID password -> RLO OK | RLO NOK | RLO UNR | RLO WRP */
int cmd_lou(char *response, const char *uid, const char *password) {
    if (!response || !uid || !password) return 1;
    User *u = find_user(uid);
    if (!u) {
        sprintf(response, "RLO UNR\n");
        return 0;
    }
    if (strcmp(u->password, password) != 0) {
        sprintf(response, "RLO WRP\n");
        return 0;
    }
    if (u->logged_in) {
        u->logged_in = 0;
        mark_user_logged_out(uid);
        sprintf(response, "RLO OK\n");
    } else {
        sprintf(response, "RLO NOK\n");
    }
    return 0;
}

/* UNR UID password -> RUR OK | RUR NOK | RUR UNR | RUR WRP */
int cmd_unr(char *response, const char *uid, const char *password) {
    if (!response || !uid || !password) return 1;
    User *u = find_user(uid);
    if (!u) {
        sprintf(response, "RUR UNR\n");
        return 0;
    }
    if (strcmp(u->password, password) != 0) {
        sprintf(response, "RUR WRP\n");
        return 0;
    }
    if (!u->logged_in) {
        sprintf(response, "RUR NOK\n");
        return 0;
    }
    
    /* Delete password and login files, but preserve CREATED/RESERVED dirs */
    delete_user_password(uid);
    
    /* Remove from memory */
    u->used = 0;
    u->logged_in = 0;
    sprintf(response, "RUR OK\n");
    return 0;
}

/* LME UID password -> RME status [EID state]* */
int cmd_lme(char *response, const char *uid, const char *password) {
    if (!response || !uid || !password) return 1;
    User *u = find_user(uid);
    if (!u) {
        sprintf(response, "RME NOK\n");
        return 0;
    }
    if (strcmp(u->password, password) != 0) {
        sprintf(response, "RME WRP\n");
        return 0;
    }
    if (!u->logged_in) {
        sprintf(response, "RME NLG\n");
        return 0;
    }
    /* collect events owned by this user */
    char tmp[2048];
    tmp[0] = '\0';
    int count = 0;
    
    EventNode *curr = events_head;
    while (curr) {
        if (curr->data.used && strcmp(curr->data.owner, uid) == 0) {
            int state = 1; /* default future open */
            if (curr->data.closed) state = 3;
            else if (curr->data.seats_reserved >= curr->data.attendance_size) state = 2;
            else {
                /* compare date to current */
                char current_dt[17];
                get_current_datetime(current_dt);
                if (cmp_date(curr->data.event_date, current_dt) < 0) state = 0;
                else state = 1;
            }
            char part[64];
            snprintf(part, sizeof part, " %03d %d", curr->data.eid, state);
            strncat(tmp, part, sizeof tmp - strlen(tmp) - 1);
            count++;
        }
        curr = curr->next;
    }
    
    if (count == 0) {
        sprintf(response, "RME NOK\n");
        return 0;
    }
    snprintf(response, 2048, "RME OK%s\n", tmp);
    return 0;
}

/* LMR UID password -> RMR status [EID date value]*
   Returns up to 50 most recent reservations with timestamp in dd-mm-yyyy hh:mm:ss format
*/
int cmd_lmr(char *response, const char *uid, const char *password) {
    if (!response || !uid || !password) return 1;
    User *u = find_user(uid);
    if (!u) {
        sprintf(response, "RMR NOK\n");
        return 0;
    }
    if (strcmp(u->password, password) != 0) {
        sprintf(response, "RMR WRP\n");
        return 0;
    }
    if (!u->logged_in) {
        sprintf(response, "RMR NLG\n");
        return 0;
    }
    
    /* Collect all reservations for this user */
    typedef struct {
        int eid;
        char timestamp[20];
        int people;
    } ResInfo;
    
    /* Allocate space for reservations - we'll store all and then take last 50 */
    ResInfo *res_list = NULL;
    int res_count = 0;
    int res_capacity = 0;
    
    ReservationNode *curr = reservations_head;
    while (curr) {
        if (curr->data.used && strcmp(curr->data.uid, uid) == 0) {
            /* Find the corresponding event */
            Event *ev = NULL;
            EventNode *ev_curr = events_head;
            while (ev_curr) {
                if (ev_curr->data.used && ev_curr->data.eid == curr->data.eid) {
                    ev = &ev_curr->data;
                    break;
                }
                ev_curr = ev_curr->next;
            }
            
            if (ev) {
                /* Expand array if needed */
                if (res_count >= res_capacity) {
                    res_capacity = (res_capacity == 0) ? 10 : res_capacity * 2;
                    res_list = realloc(res_list, res_capacity * sizeof(ResInfo));
                    if (!res_list) {
                        sprintf(response, "RMR NOK\n");
                        return 0;
                    }
                }
                
                res_list[res_count].eid = ev->eid;
                strncpy(res_list[res_count].timestamp, curr->data.timestamp, 19);
                res_list[res_count].timestamp[19] = '\0';
                res_list[res_count].people = curr->data.people;
                res_count++;
            }
        }
        curr = curr->next;
    }
    
    if (res_count == 0) {
        sprintf(response, "RMR NOK\n");
        return 0;
    }
    
    /* Take last 50 reservations maximum (most recent) */
    int start_idx = (res_count > MAX_MYRESERVATIONS_DISPLAY) ? (res_count - MAX_MYRESERVATIONS_DISPLAY) : 0;
    
    char tmp[4096];
    tmp[0] = '\0';
    for (int i = start_idx; i < res_count; ++i) {
        char part[128];
        snprintf(part, sizeof part, " %03d %s %d", 
                 res_list[i].eid, res_list[i].timestamp, res_list[i].people);
        strncat(tmp, part, sizeof tmp - strlen(tmp) - 1);
    }
    
    snprintf(response, 4096, "RMR OK%s\n", tmp);
    free(res_list);  /* Don't forget to free */
    return 0;
}

/* ---------- TCP command handlers ---------- */

/* cmd_cre: creates event, stores file on server, returns "RCE OK EID\n" or error codes.
   signature: int cmd_cre(char **response_buf, size_t *response_len, const char *uid, const char *password,
                         const char *name, const char *event_date, int attendance_size,
                         const char *fname, int fsize, const char *fdata);
*/
int cmd_cre(char **response_buf, size_t *response_len, const char *uid, const char *password,
            const char *name, const char *event_date, int attendance_size, const char *fname,
            int fsize, const char *fdata)
{
    if (!response_buf || !response_len || !uid || !password || !name || !event_date || !fname) return 1;

    User *u = find_user(uid);
    if (!u) {
        /* not registered or bad user -> NLG? Spec: if user not logged in reply NLG. For not registered, WRP? We'll reply NLG for simplicity */
        *response_buf = strdup("RCE NLG\n");
        *response_len = strlen(*response_buf);
        return 0;
    }
    if (strcmp(u->password, password) != 0) {
        *response_buf = strdup("RCE WRP\n");
        *response_len = strlen(*response_buf);
        return 0;
    }
    if (!u->logged_in) {
        *response_buf = strdup("RCE NLG\n");
        *response_len = strlen(*response_buf);
        return 0;
    }

    /* Creation: allow events with past dates. They will be shown as past
       by listing/state logic (no creation-time rejection). */

    /* create event slot */
    Event *ev = create_event_slot();
    if (!ev) {
        *response_buf = strdup("RCE NOK\n");
        *response_len = strlen(*response_buf);
        return 0;
    }
    ev->eid = ev->eid; /* assigned in create_event_slot */
    strncpy(ev->owner, uid, 6); ev->owner[6] = '\0';
    strncpy(ev->name, name, sizeof ev->name - 1);
    strncpy(ev->event_date, event_date, sizeof ev->event_date - 1);
    ev->attendance_size = attendance_size;
    ev->seats_reserved = 0;
    strncpy(ev->fname, fname, sizeof ev->fname - 1);
    ev->fsize = fsize;
    if (fsize > 0) {
        if (fsize > MAX_FDATA_SIZE) {
            free_event(ev);
            *response_buf = strdup("RCE NOK\n");
            *response_len = strlen(*response_buf);
            return 0;
        }
        ev->fdata = malloc(fsize);
        if (!ev->fdata) {
            free_event(ev);
            *response_buf = strdup("RCE NOK\n");
            *response_len = strlen(*response_buf);
            return 0;
        }
        memcpy(ev->fdata, fdata, fsize);
    } else {
        ev->fdata = NULL;
    }

    /* success: return OK + EID as 3 digits */
    char tmp[64];
    snprintf(tmp, sizeof tmp, "RCE OK %03d\n", ev->eid);
    *response_buf = strdup(tmp);
    *response_len = strlen(tmp);
    
    /* Save event to disk */
    save_event_start(ev);
    
    return 0;
}

/* cmd_cls: close event
   signature: int cmd_cls(char **response_buf, size_t *response_len, const char *uid, const char *password, const char *eid);
*/
int cmd_cls(char **response_buf, size_t *response_len, const char *uid, const char *password, const char *eidstr) {
    if (!response_buf || !response_len || !uid || !password || !eidstr) return 1;
    User *u = find_user(uid);
    if (!u) {
        *response_buf = strdup("RCL NOK\n");
        *response_len = strlen(*response_buf);
        return 0;
    }
    if (strcmp(u->password, password) != 0) {
        *response_buf = strdup("RCL NOK\n");
        *response_len = strlen(*response_buf);
        return 0;
    }
    if (!u->logged_in) {
        *response_buf = strdup("RCL NLG\n");
        *response_len = strlen(*response_buf);
        return 0;
    }
    Event *ev = find_event_by_eid_str(eidstr);
    if (!ev) {
        *response_buf = strdup("RCL NOE\n");
        *response_len = strlen(*response_buf);
        return 0;
    }
    if (strcmp(ev->owner, uid) != 0) {
        *response_buf = strdup("RCL EOW\n");
        *response_len = strlen(*response_buf);
        return 0;
    }
    /* check if sold out */
    if (ev->seats_reserved >= ev->attendance_size) {
        *response_buf = strdup("RCL SLD\n");
        *response_len = strlen(*response_buf);
        return 0;
    }
        /* (Previously would reject if event date is in the past.)
           Creation of past events is allowed; do not reject here. */
    if (ev->closed) {
        *response_buf = strdup("RCL CLO\n");
        *response_len = strlen(*response_buf);
        return 0;
    }
    /* close it */
    ev->closed = 1;
    close_event_file(ev->eid);
    *response_buf = strdup("RCL OK\n");
    *response_len = strlen(*response_buf);
    return 0;
}

/* cmd_lst: list events
   signature: int cmd_lst(char **response_buf, size_t *response_len);
   reply: RLS status[ EID name state event_date ]*
*/
int cmd_lst(char **response_buf, size_t *response_len) {
    if (!response_buf || !response_len) return 1;
    char tmp[8192];
    tmp[0] = '\0';
    int count = 0;
    
    EventNode *curr = events_head;
    while (curr) {
        if (!curr->data.used) {
            curr = curr->next;
            continue;
        }
        
        int state;
        char current_dt[17];
        get_current_datetime(current_dt);
        if (curr->data.closed) state = 3;
        else if (curr->data.seats_reserved >= curr->data.attendance_size) state = 2;
        else if (cmp_date(curr->data.event_date, current_dt) < 0) state = 0;
        else state = 1;
        
        char part[128];
        snprintf(part, sizeof part, " %03d %s %d %s", curr->data.eid, curr->data.name, state, curr->data.event_date);
        strncat(tmp, part, sizeof tmp - strlen(tmp) - 1);
        count++;
        curr = curr->next;
    }
    
    if (count == 0) {
        *response_buf = strdup("RLS NOK\n");
        *response_len = strlen(*response_buf);
        return 0;
    }
    char header[32];
    snprintf(header, sizeof header, "RLS OK");
    char *out = malloc(strlen(header) + strlen(tmp) + 2);
    if (!out) return 1;
    strcpy(out, header);
    strcat(out, tmp);
    strcat(out, "\n");
    *response_buf = out;
    *response_len = strlen(out);
    return 0;
}

/* cmd_sed: show event + file
   signature: int cmd_sed(char **response_buf, size_t *response_len, const char *eid);
   reply: RSE status [UID name event_date attendance_size Seats_reserved Fname Fsize Fdata]
*/
int cmd_sed(char **response_buf, size_t *response_len, const char *eidstr) {
    if (!response_buf || !response_len || !eidstr) return 1;
    Event *ev = find_event_by_eid_str(eidstr);
    if (!ev) {
        *response_buf = strdup("RSE NOK\n");
        *response_len = strlen(*response_buf);
        return 0;
    }
    /* build header with metadata then append file contents */
    char header[512];
    snprintf(header, sizeof header, "RSE OK %s %s %s %d %d %s %d ",
             ev->owner, ev->name, ev->event_date, ev->attendance_size, ev->seats_reserved, ev->fname, ev->fsize);
    size_t hlen = strlen(header);
    size_t total = hlen + ev->fsize + 2;
    char *out = malloc(total);
    if (!out) return 1;
    memcpy(out, header, hlen);
    if (ev->fsize > 0 && ev->fdata) {
        memcpy(out + hlen, ev->fdata, ev->fsize);
        hlen += ev->fsize;
    }
    out[hlen] = '\n';
    out[hlen+1] = '\0';
    *response_buf = out;
    *response_len = hlen + 1;
    return 0;
}

/* cmd_rid: reserve seats
   signature: int cmd_rid(char **response_buf, size_t *response_len, const char *uid, const char *password, const char *eid, int people)
   replies: RRI ACC | RRI REJ n_seats | RRI CLS | RRI SLD | RRI PST | RRI WRP | RRI NOK
*/
int cmd_rid(char **response_buf, size_t *response_len, const char *uid, const char *password, const char *eidstr, int people) {
    if (!response_buf || !response_len || !uid || !password || !eidstr) return 1;
    User *u = find_user(uid);
    if (!u) {
        *response_buf = strdup("RRI NOK\n");
        *response_len = strlen(*response_buf);
        return 0;
    }
    if (strcmp(u->password, password) != 0) {
        *response_buf = strdup("RRI WRP\n");
        *response_len = strlen(*response_buf);
        return 0;
    }
    if (!u->logged_in) {
        *response_buf = strdup("RRI NLG\n");
        *response_len = strlen(*response_buf);
        return 0;
    }
    Event *ev = find_event_by_eid_str(eidstr);
    if (!ev) {
        *response_buf = strdup("RRI NOK\n");
        *response_len = strlen(*response_buf);
        return 0;
    }
    /* (Previously would reject if event date is in the past.)
       Reservation creation now allowed; do not reject here. */
    /* closed */
    if (ev->closed) {
        *response_buf = strdup("RRI CLS\n");
        *response_len = strlen(*response_buf);
        return 0;
    }
    int remaining = ev->attendance_size - ev->seats_reserved;
    if (remaining <= 0) {
        *response_buf = strdup("RRI SLD\n");
        *response_len = strlen(*response_buf);
        return 0;
    }
    if (people > remaining) {
        char tmp[64];
        snprintf(tmp, sizeof tmp, "RRI REJ %d\n", remaining);
        *response_buf = strdup(tmp);
        *response_len = strlen(*response_buf);
        return 0;
    }
    /* accept reservation */
    ev->seats_reserved += people;
    Reservation *r = create_reservation_slot();
    if (!r) {
        *response_buf = strdup("RRI NOK\n");
        *response_len = strlen(*response_buf);
        return 0;
    }
    strncpy(r->uid, uid, 6); r->uid[6] = '\0';
    r->eid = ev->eid;
    r->people = people;
    get_current_timestamp(r->timestamp);  /* Add timestamp */
    
    /* Save reservation to disk */
    save_reservation(r);
    update_event_reservations(ev->eid, ev->seats_reserved);
    
    char tmp[64];
    snprintf(tmp, sizeof tmp, "RRI ACC\n");
    *response_buf = strdup(tmp);
    *response_len = strlen(*response_buf);
    return 0;
}

/* cmd_cps: change password
   signature: int cmd_cps(char **response_buf, size_t *response_len, const char *uid, const char *oldpass, const char *newpass)
   replies: RCP OK | RCP NLG | RCP NOK | RCP NID
*/
int cmd_cps(char **response_buf, size_t *response_len, const char *uid, const char *oldpass, const char *newpass) {
    if (!response_buf || !response_len || !uid || !oldpass || !newpass) return 1;
    User *u = find_user(uid);
    if (!u) {
        *response_buf = strdup("RCP NID\n");
        *response_len = strlen(*response_buf);
        return 0;
    }
    if (!u->logged_in) {
        *response_buf = strdup("RCP NLG\n");
        *response_len = strlen(*response_buf);
        return 0;
    }
    if (strcmp(u->password, oldpass) != 0) {
        *response_buf = strdup("RCP NOK\n");
        *response_len = strlen(*response_buf);
        return 0;
    }
    strncpy(u->password, newpass, sizeof(u->password)-1);
    
    /* Save new password to disk */
    if (save_user_password(uid, newpass) == -1) {
        *response_buf = strdup("RCP NOK\n");
        *response_len = strlen(*response_buf);
        return 0;
    }
    
    *response_buf = strdup("RCP OK\n");
    *response_len = strlen(*response_buf);
    return 0;
}

