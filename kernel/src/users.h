/*
 * ojjyOS v3 Kernel - User Accounts & Credentials Subsystem
 *
 * Provides user account creation, salted SHA-256 credential hashing,
 * authentication, session lock/unlock, per-user home directory
 * provisioning (/Users/<username>), and persistence in
 * /Library/Accounts/users.db.
 *
 * Created by Jonas Lee.
 */

#ifndef _OJJY_USERS_H
#define _OJJY_USERS_H

#include "types.h"

#define USERS_MAX           8
#define USER_NAME_MAX       32
#define USER_FULLNAME_MAX   48
#define USER_PASS_MAX       48
#define USER_HOME_MAX       64
#define USER_SALT_HEX_LEN   17   /* 8 CSPRNG bytes as 16 hex chars + NUL */
#define USER_HASH_HEX_LEN   80   /* "pbkdf2$<iters>$" + 64 hex + NUL */

typedef struct {
    char username[USER_NAME_MAX];
    char full_name[USER_FULLNAME_MAX];
    char salt_hex[USER_SALT_HEX_LEN];
    char pass_hash[USER_HASH_HEX_LEN];
    char home_dir[USER_HOME_MAX];
    bool is_admin;
    bool active;
} UserAccount;

/* Initialize user subsystem and load /Library/Accounts/users.db */
void users_init(void);

/* Save accounts database to /Library/Accounts/users.db and request disk sync */
int users_save(void);

/* Number of active user accounts */
int users_count(void);

/* Get account by slot index (0 .. USERS_MAX-1) or by username */
const UserAccount *users_get_by_index(int index);
const UserAccount *users_find(const char *username);

/* Currently logged-in user (NULL if locked / not yet logged in) */
const UserAccount *users_current(void);
int users_current_index(void);
bool users_is_authenticated(void);

/* True only when an authenticated administrator session is active. Gates
 * account creation/deletion and the VFS access checks. */
bool users_current_is_admin(void);

/* Convenience getters (always return non-NULL strings; fallback to "guest") */
const char *users_current_username(void);
const char *users_current_fullname(void);
const char *users_current_full_name(void);
const char *users_current_home(void);

/* Build a path inside the active user's home directory, e.g. "Desktop" -> "/Users/jonas/Desktop" */
int users_make_home_path(char *out, size_t size, const char *subpath);

/* Create a new user account with salted SHA-256 password hash & provision /Users/<username> */
int users_create(const char *username, const char *full_name,
                 const char *password, bool is_admin);

/* Verify username + password against stored salted SHA-256 hash */
bool users_verify_credentials(const char *username, const char *password);

/* Authenticate and switch active session to username */
int users_login(const char *username, const char *password);

/* Change password for an existing account (verifies old_password if non-NULL) */
int users_change_password(const char *username, const char *old_password,
                          const char *new_password);

/* Delete a user account (cannot delete the last remaining account) */
int users_delete(const char *username);

/* Lock or log out of the current session */
void users_lock_session(void);
void users_logout(void);

/* Provision standard directories and starter files for a user home directory */
void users_provision_home(const char *username, const char *full_name);

#endif /* _OJJY_USERS_H */
