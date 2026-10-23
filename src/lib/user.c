#include "user.h"
#include <pwd.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

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
    snprintf(u->role, sizeof u->role, "%s", getuid() == 0 ? "Administrateur système" : "Standard");
    snprintf(u->avatar, sizeof u->avatar, "/share/icons/user_avatar.png");
    snprintf(u->created, sizeof u->created, "session actuelle");
    u->is_admin = getuid() == 0;
    snprintf(g_users.current_username, sizeof g_users.current_username, "%s", u->username);
}
void user_save(void) {}
UserAccount *user_get_current(void) { return g_users.count ? &g_users.users[0] : NULL; }
UserAccount *user_get_by_name(const char *username) {
    return username && g_users.count && !strcmp(g_users.users[0].username, username) ? &g_users.users[0] : NULL;
}
int user_switch(const char *username) { (void)username; return 0; }
int user_add(const char *username, const char *fullname, const char *role, const char *avatar) {
    (void)username; (void)fullname; (void)role; (void)avatar; return 0;
}
int user_update(const char *username, const char *fullname, const char *role) {
    (void)username; (void)fullname; (void)role; return 0;
}
int user_remove(const char *username) { (void)username; return 0; }
