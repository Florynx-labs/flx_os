#include "user.h"
#include <pwd.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include "w12.h"

UserManager g_users;

// Account labels are read from the kernel identity. User switching and
// account administration must go through the privileged auth/session service;
// never emulate them by editing a user-writable JSON file.
void user_init(void) {
    memset(&g_users, 0, sizeof g_users);
    struct passwd *pw = getpwuid(getuid());
    UserAccount *u = &g_users.users[0];
    g_users.count = 1;
    snprintf(u->username, sizeof u->username, "%s", pw && pw->pw_name ? pw->pw_name : "unknown");
    const char *gecos = pw && pw->pw_gecos ? pw->pw_gecos : u->username;
    size_t n = strcspn(gecos, ",");
    if (n >= sizeof u->fullname) n = sizeof u->fullname - 1;
    memcpy(u->fullname, gecos, n); u->fullname[n] = 0;
    snprintf(u->role, sizeof u->role, "%s", getuid() == 1000 ? "Administrateur" : "Standard");
    snprintf(u->avatar, sizeof u->avatar, "/share/icons/user_avatar.png");
    snprintf(u->created, sizeof u->created, "session actuelle");
    u->is_admin = getuid() == 1000;
    snprintf(g_users.current_username, sizeof g_users.current_username, "%s", u->username);
    char response[3500];
    if (app_auth_request("LIST", response, sizeof response) || strncmp(response, "USERS", 5)) return;
    memset(&g_users, 0, sizeof g_users);
    char *save = NULL;
    for (char *entry = strtok_r(response + 5, " ", &save); entry && g_users.count < MAX_USERS; entry = strtok_r(NULL, " ", &save)) {
        char *part[5] = {0}; char *p = entry;
        for (int i = 0; i < 4; i++) { part[i] = p; p = strchr(p, ':'); if (!p) break; *p++ = 0; }
        if (!part[0] || !part[1] || !part[2] || !part[3] || !p) continue;
        unsigned uid = (unsigned)strtoul(part[1], NULL, 10);
        UserAccount *item = &g_users.users[g_users.count++];
        snprintf(item->username, sizeof item->username, "%s", part[0]);
        item->is_admin = !strcmp(part[3], "admin");
        snprintf(item->role, sizeof item->role, "%s", item->is_admin ? "Administrateur" : "Standard");
        size_t n = strlen(p);
        if (!(n & 1)) for (size_t i = 0; i + 1 < n && i / 2 < sizeof item->fullname - 1; i += 2) {
            char pair[3] = {p[i], p[i + 1], 0}; item->fullname[i / 2] = (char)strtoul(pair, NULL, 16); item->fullname[i / 2 + 1] = 0;
        }
        if (!item->fullname[0]) snprintf(item->fullname, sizeof item->fullname, "%s", item->username);
        snprintf(item->created, sizeof item->created, "UID %u", uid);
        snprintf(item->avatar, sizeof item->avatar, "/share/icons/user_avatar.png");
        if (uid == getuid()) snprintf(g_users.current_username, sizeof g_users.current_username, "%s", item->username);
    }
}
void user_save(void) {}
UserAccount *user_get_current(void) { return user_get_by_name(g_users.current_username); }
UserAccount *user_get_by_name(const char *username) {
    if (!username) return NULL;
    for (int i = 0; i < g_users.count; i++) if (!strcmp(g_users.users[i].username, username)) return &g_users.users[i];
    return NULL;
}
int user_switch(const char *username) { (void)username; return 0; }
int user_add(const char *username, const char *fullname, const char *role, const char *avatar) {
    (void)username; (void)fullname; (void)role; (void)avatar; return 0;
}
int user_update(const char *username, const char *fullname, const char *role) {
    (void)username; (void)fullname; (void)role; return 0;
}
int user_remove(const char *username) { (void)username; return 0; }
