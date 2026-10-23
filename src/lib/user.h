#pragma once
#include <stdint.h>
#include <stddef.h>

#define MAX_USERS 8

typedef struct {
    char username[32];
    char fullname[64];
    char role[32];       // "Administrateur" or "Standard"
    char avatar[128];    // path to avatar image, e.g. "/share/icons/user_avatar.png"
    char created[32];
    int is_admin;
} UserAccount;

typedef struct {
    char current_username[32];
    int count;
    UserAccount users[MAX_USERS];
} UserManager;

extern UserManager g_users;

void user_init(void);
void user_save(void);
UserAccount *user_get_current(void);
UserAccount *user_get_by_name(const char *username);
int user_switch(const char *username);
int user_add(const char *username, const char *fullname, const char *role, const char *avatar);
int user_update(const char *username, const char *fullname, const char *role);
int user_remove(const char *username);
