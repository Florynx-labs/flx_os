#include "user.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

UserManager g_users;

static const char *CONFIG_PATH = "/home/user/.config/users.json";

void user_init(void) {
    memset(&g_users, 0, sizeof(g_users));

    // Try loading from config file
    FILE *f = fopen(CONFIG_PATH, "r");
    if (f) {
        char line[256];
        UserAccount *cur = NULL;
        while (fgets(line, sizeof(line), f)) {
            char *p = line;
            while (*p == ' ' || *p == '\t') p++;
            if (strncmp(p, "\"current\":", 10) == 0) {
                char *start = strchr(p, '"');
                if (start) start = strchr(start + 1, '"');
                if (start) {
                    char *val = start + 1;
                    char *end = strchr(val, '"');
                    if (end) {
                        *end = '\0';
                        strncpy(g_users.current_username, val, sizeof(g_users.current_username) - 1);
                    }
                }
            } else if (strncmp(p, "\"username\":", 11) == 0) {
                if (g_users.count < MAX_USERS) {
                    cur = &g_users.users[g_users.count++];
                    char *start = strchr(p, ':');
                    if (start) start = strchr(start, '"');
                    if (start) {
                        char *val = start + 1;
                        char *end = strchr(val, '"');
                        if (end) {
                            *end = '\0';
                            strncpy(cur->username, val, sizeof(cur->username) - 1);
                        }
                    }
                }
            } else if (cur && strncmp(p, "\"fullname\":", 11) == 0) {
                char *start = strchr(p, ':');
                if (start) start = strchr(start, '"');
                if (start) {
                    char *val = start + 1;
                    char *end = strchr(val, '"');
                    if (end) {
                        *end = '\0';
                        strncpy(cur->fullname, val, sizeof(cur->fullname) - 1);
                    }
                }
            } else if (cur && strncmp(p, "\"role\":", 7) == 0) {
                char *start = strchr(p, ':');
                if (start) start = strchr(start, '"');
                if (start) {
                    char *val = start + 1;
                    char *end = strchr(val, '"');
                    if (end) {
                        *end = '\0';
                        strncpy(cur->role, val, sizeof(cur->role) - 1);
                        cur->is_admin = (strstr(val, "Admin") != NULL);
                    }
                }
            } else if (cur && strncmp(p, "\"avatar\":", 9) == 0) {
                char *start = strchr(p, ':');
                if (start) start = strchr(start, '"');
                if (start) {
                    char *val = start + 1;
                    char *end = strchr(val, '"');
                    if (end) {
                        *end = '\0';
                        strncpy(cur->avatar, val, sizeof(cur->avatar) - 1);
                    }
                }
            }
        }
        fclose(f);
    }

    // Default accounts if none loaded
    if (g_users.count == 0) {
        g_users.count = 2;
        strncpy(g_users.current_username, "asuno", sizeof(g_users.current_username) - 1);

        // Administrator account
        strncpy(g_users.users[0].username, "asuno", sizeof(g_users.users[0].username) - 1);
        strncpy(g_users.users[0].fullname, "FLX Admin", sizeof(g_users.users[0].fullname) - 1);
        strncpy(g_users.users[0].role, "Administrateur", sizeof(g_users.users[0].role) - 1);
        strncpy(g_users.users[0].avatar, "/share/icons/user_avatar.png", sizeof(g_users.users[0].avatar) - 1);
        strncpy(g_users.users[0].created, "23 Octobre 2026", sizeof(g_users.users[0].created) - 1);
        g_users.users[0].is_admin = 1;

        // Standard user account
        strncpy(g_users.users[1].username, "invite", sizeof(g_users.users[1].username) - 1);
        strncpy(g_users.users[1].fullname, "Utilisateur Invité", sizeof(g_users.users[1].fullname) - 1);
        strncpy(g_users.users[1].role, "Standard", sizeof(g_users.users[1].role) - 1);
        strncpy(g_users.users[1].avatar, "/share/icons/user_avatar.png", sizeof(g_users.users[1].avatar) - 1);
        strncpy(g_users.users[1].created, "23 Octobre 2026", sizeof(g_users.users[1].created) - 1);
        g_users.users[1].is_admin = 0;

        user_save();
    }
}

void user_save(void) {
    FILE *f = fopen(CONFIG_PATH, "w");
    if (!f) return;

    fprintf(f, "{\n");
    fprintf(f, "  \"current\": \"%s\",\n", g_users.current_username);
    fprintf(f, "  \"users\": [\n");
    for (int i = 0; i < g_users.count; i++) {
        UserAccount *u = &g_users.users[i];
        fprintf(f, "    {\n");
        fprintf(f, "      \"username\": \"%s\",\n", u->username);
        fprintf(f, "      \"fullname\": \"%s\",\n", u->fullname);
        fprintf(f, "      \"role\": \"%s\",\n", u->role);
        fprintf(f, "      \"avatar\": \"%s\",\n", u->avatar);
        fprintf(f, "      \"created\": \"%s\"\n", u->created);
        fprintf(f, "    }%s\n", (i == g_users.count - 1) ? "" : ",");
    }
    fprintf(f, "  ]\n");
    fprintf(f, "}\n");
    fclose(f);
    sync();
}

UserAccount *user_get_current(void) {
    for (int i = 0; i < g_users.count; i++) {
        if (strcmp(g_users.users[i].username, g_users.current_username) == 0) {
            return &g_users.users[i];
        }
    }
    return (g_users.count > 0) ? &g_users.users[0] : NULL;
}

UserAccount *user_get_by_name(const char *username) {
    if (!username) return NULL;
    for (int i = 0; i < g_users.count; i++) {
        if (strcmp(g_users.users[i].username, username) == 0) {
            return &g_users.users[i];
        }
    }
    return NULL;
}

int user_switch(const char *username) {
    UserAccount *u = user_get_by_name(username);
    if (!u) return 0;
    strncpy(g_users.current_username, username, sizeof(g_users.current_username) - 1);
    user_save();
    return 1;
}

int user_add(const char *username, const char *fullname, const char *role, const char *avatar) {
    if (g_users.count >= MAX_USERS || user_get_by_name(username) != NULL) return 0;
    UserAccount *u = &g_users.users[g_users.count++];
    strncpy(u->username, username, sizeof(u->username) - 1);
    strncpy(u->fullname, fullname ? fullname : username, sizeof(u->fullname) - 1);
    strncpy(u->role, role ? role : "Standard", sizeof(u->role) - 1);
    strncpy(u->avatar, avatar ? avatar : "/share/icons/user_avatar.png", sizeof(u->avatar) - 1);
    strncpy(u->created, "23 Octobre 2026", sizeof(u->created) - 1);
    u->is_admin = (strstr(u->role, "Admin") != NULL);
    user_save();
    return 1;
}

int user_update(const char *username, const char *fullname, const char *role) {
    UserAccount *u = user_get_by_name(username);
    if (!u) return 0;
    if (fullname) strncpy(u->fullname, fullname, sizeof(u->fullname) - 1);
    if (role) {
        strncpy(u->role, role, sizeof(u->role) - 1);
        u->is_admin = (strstr(role, "Admin") != NULL);
    }
    user_save();
    return 1;
}

int user_remove(const char *username) {
    int idx = -1;
    for (int i = 0; i < g_users.count; i++) {
        if (strcmp(g_users.users[i].username, username) == 0) {
            idx = i;
            break;
        }
    }
    if (idx < 0 || g_users.count <= 1) return 0; // cannot remove last user

    for (int i = idx; i < g_users.count - 1; i++) {
        g_users.users[i] = g_users.users[i + 1];
    }
    g_users.count--;
    if (strcmp(g_users.current_username, username) == 0) {
        strncpy(g_users.current_username, g_users.users[0].username, sizeof(g_users.current_username) - 1);
    }
    user_save();
    return 1;
}
