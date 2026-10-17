/*
 * ojjyOS v3 Kernel - User Accounts & Credentials Implementation
 *
 * Implements FIPS 180-4 SHA-256 password hashing with per-user random salt,
 * persistent storage in /Library/Accounts/users.db, per-user home directory
 * provisioning (/Users/<username>/{Desktop,Documents}), and session state.
 *
 * Created by Jonas Lee.
 */

#include "users.h"
#include "string.h"
#include "timer.h"
#include "serial.h"
#include "system.h"
#include "fs/vfs.h"
#include "drivers/rtc.h"

#define USERS_DB_DIR    "/Library/Accounts"
#define USERS_DB_PATH   "/Library/Accounts/users.db"

static UserAccount accounts[USERS_MAX];
static int current_user_idx = -1;
static bool session_authenticated = false;
static char last_username[USER_NAME_MAX] = "";

/* =========================================================
 * Helper string utilities
 * ========================================================= */

static size_t u_str_append(char *dest, size_t size, const char *src)
{
    size_t len = strlen(dest);
    size_t i = 0;
    if (len >= size) return len;
    while (src[i] && (len + i + 1) < size) {
        dest[len + i] = src[i];
        i++;
    }
    dest[len + i] = '\0';
    return len + i;
}

static char u_ascii_lower(char c)
{
    if (c >= 'A' && c <= 'Z') return (char)(c + 32);
    return c;
}

static bool u_streq_ci(const char *a, const char *b)
{
    if (!a || !b) return false;
    while (*a && *b) {
        if (u_ascii_lower(*a) != u_ascii_lower(*b)) return false;
        a++;
        b++;
    }
    return *a == '\0' && *b == '\0';
}

/* =========================================================
 * FIPS 180-4 SHA-256 Implementation (for credential hashing)
 * ========================================================= */

typedef struct {
    uint32_t state[8];
    uint64_t bitcount;
    uint8_t  buffer[64];
} Sha256Ctx;

static const uint32_t sha256_k[64] = {
    0x428a2f98U, 0x71374491U, 0xb5c0fbcfU, 0xe9b5dba5U,
    0x3956c25bU, 0x59f111f1U, 0x923f82a4U, 0xab1c5ed5U,
    0xd807aa98U, 0x12835b01U, 0x243185beU, 0x550c7dc3U,
    0x72be5d74U, 0x80deb1feU, 0x9bdc06a7U, 0xc19bf174U,
    0xe49b69c1U, 0xefbe4786U, 0x0fc19dc6U, 0x240ca1ccU,
    0x2de92c6fU, 0x4a7484aaU, 0x5cb0a9dcU, 0x76f988daU,
    0x983e5152U, 0xa831c66dU, 0xb00327c8U, 0xbf597fc7U,
    0xc6e00bf3U, 0xd5a79147U, 0x06ca6351U, 0x14292967U,
    0x27b70a85U, 0x2e1b2138U, 0x4d2c6dfcU, 0x53380d13U,
    0x650a7354U, 0x766a0abbU, 0x81c2c92eU, 0x92722c85U,
    0xa2bfe8a1U, 0xa81a664bU, 0xc24b8b70U, 0xc76c51a3U,
    0xd192e819U, 0xd6990624U, 0xf40e3585U, 0x106aa070U,
    0x19a4c116U, 0x1e376c08U, 0x2748774cU, 0x34b0bcb5U,
    0x391c0cb3U, 0x4ed8aa4aU, 0x5b9cca4fU, 0x682e6ff3U,
    0x748f82eeU, 0x78a5636fU, 0x84c87814U, 0x8cc70208U,
    0x90befffaU, 0xa4506cebU, 0xbef9a3f7U, 0xc67178f2U
};

static inline uint32_t rotr32(uint32_t x, uint32_t n)
{
    return (x >> n) | (x << (32U - n));
}

static void sha256_transform(Sha256Ctx *ctx, const uint8_t data[64])
{
    uint32_t w[64];
    for (int i = 0; i < 16; i++) {
        w[i] = ((uint32_t)data[i * 4 + 0] << 24) |
               ((uint32_t)data[i * 4 + 1] << 16) |
               ((uint32_t)data[i * 4 + 2] << 8)  |
               ((uint32_t)data[i * 4 + 3]);
    }
    for (int i = 16; i < 64; i++) {
        uint32_t s0 = rotr32(w[i - 15], 7) ^ rotr32(w[i - 15], 18) ^ (w[i - 15] >> 3);
        uint32_t s1 = rotr32(w[i - 2], 17) ^ rotr32(w[i - 2], 19) ^ (w[i - 2] >> 10);
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }

    uint32_t a = ctx->state[0];
    uint32_t b = ctx->state[1];
    uint32_t c = ctx->state[2];
    uint32_t d = ctx->state[3];
    uint32_t e = ctx->state[4];
    uint32_t f = ctx->state[5];
    uint32_t g = ctx->state[6];
    uint32_t h = ctx->state[7];

    for (int i = 0; i < 64; i++) {
        uint32_t S1 = rotr32(e, 6) ^ rotr32(e, 11) ^ rotr32(e, 25);
        uint32_t ch = (e & f) ^ ((~e) & g);
        uint32_t temp1 = h + S1 + ch + sha256_k[i] + w[i];
        uint32_t S0 = rotr32(a, 2) ^ rotr32(a, 13) ^ rotr32(a, 22);
        uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
        uint32_t temp2 = S0 + maj;

        h = g;
        g = f;
        f = e;
        e = d + temp1;
        d = c;
        c = b;
        b = a;
        a = temp1 + temp2;
    }

    ctx->state[0] += a;
    ctx->state[1] += b;
    ctx->state[2] += c;
    ctx->state[3] += d;
    ctx->state[4] += e;
    ctx->state[5] += f;
    ctx->state[6] += g;
    ctx->state[7] += h;
}

static void sha256_init(Sha256Ctx *ctx)
{
    ctx->state[0] = 0x6a09e667U;
    ctx->state[1] = 0xbb67ae85U;
    ctx->state[2] = 0x3c6ef372U;
    ctx->state[3] = 0xa54ff53aU;
    ctx->state[4] = 0x510e527fU;
    ctx->state[5] = 0x9b05688cU;
    ctx->state[6] = 0x1f83d9abU;
    ctx->state[7] = 0x5be0cd19U;
    ctx->bitcount = 0;
    memset(ctx->buffer, 0, sizeof(ctx->buffer));
}

static void sha256_update(Sha256Ctx *ctx, const uint8_t *data, size_t len)
{
    size_t idx = (size_t)((ctx->bitcount >> 3) & 0x3FU);
    ctx->bitcount += (uint64_t)len << 3;

    for (size_t i = 0; i < len; i++) {
        ctx->buffer[idx++] = data[i];
        if (idx == 64) {
            sha256_transform(ctx, ctx->buffer);
            idx = 0;
        }
    }
}

static void sha256_final_hex(Sha256Ctx *ctx, char out_hex[65])
{
    uint8_t pad[64];
    size_t idx = (size_t)((ctx->bitcount >> 3) & 0x3FU);
    size_t pad_len = (idx < 56) ? (56 - idx) : (120 - idx);

    memset(pad, 0, sizeof(pad));
    pad[0] = 0x80;
    uint64_t bits = ctx->bitcount;
    sha256_update(ctx, pad, pad_len);

    uint8_t len_be[8];
    for (int i = 0; i < 8; i++) {
        len_be[7 - i] = (uint8_t)(bits >> (i * 8));
    }
    sha256_update(ctx, len_be, 8);

    static const char hex[] = "0123456789abcdef";
    for (int i = 0; i < 8; i++) {
        uint32_t v = ctx->state[i];
        out_hex[i * 8 + 0] = hex[(v >> 28) & 0xF];
        out_hex[i * 8 + 1] = hex[(v >> 24) & 0xF];
        out_hex[i * 8 + 2] = hex[(v >> 20) & 0xF];
        out_hex[i * 8 + 3] = hex[(v >> 16) & 0xF];
        out_hex[i * 8 + 4] = hex[(v >> 12) & 0xF];
        out_hex[i * 8 + 5] = hex[(v >> 8)  & 0xF];
        out_hex[i * 8 + 6] = hex[(v >> 4)  & 0xF];
        out_hex[i * 8 + 7] = hex[(v >> 0)  & 0xF];
    }
    out_hex[64] = '\0';
}

static void hash_password_salted(const char *salt_hex, const char *password, char out_hex[65])
{
    Sha256Ctx ctx;
    sha256_init(&ctx);
    const char *domain = "ojjyOS-v3-auth:";
    sha256_update(&ctx, (const uint8_t *)domain, strlen(domain));
    if (salt_hex) {
        sha256_update(&ctx, (const uint8_t *)salt_hex, strlen(salt_hex));
    }
    sha256_update(&ctx, (const uint8_t *)":", 1);
    if (password) {
        sha256_update(&ctx, (const uint8_t *)password, strlen(password));
    }
    sha256_final_hex(&ctx, out_hex);
}

static void generate_salt_hex(const char *username, char out_salt[9])
{
    uint32_t seed = (uint32_t)timer_get_ticks() ^ 0xA5A5F00DU;
    RtcTime now;
    rtc_read_time(&now);
    seed ^= ((uint32_t)now.second << 16) ^ ((uint32_t)now.minute << 8) ^ now.hour;
    if (username) {
        for (size_t i = 0; username[i]; i++) {
            seed = (seed * 16777619U) ^ (uint8_t)username[i];
        }
    }
    static uint32_t counter = 1;
    seed ^= (counter++ * 0x9E3779B9U);

    static const char hex[] = "0123456789abcdef";
    for (int i = 0; i < 8; i++) {
        out_salt[i] = hex[(seed >> ((7 - i) * 4)) & 0xF];
    }
    out_salt[8] = '\0';
}

/* Constant-time string compare for password hashes */
static bool hash_equal_ct(const char *a, const char *b)
{
    if (!a || !b) return false;
    uint8_t diff = 0;
    for (int i = 0; i < 64; i++) {
        if (a[i] == '\0' || b[i] == '\0') return false;
        diff |= (uint8_t)(a[i] ^ b[i]);
    }
    return diff == 0 && a[64] == '\0' && b[64] == '\0';
}

/* =========================================================
 * Home directory provisioning
 * ========================================================= */

void users_provision_home(const char *username, const char *full_name)
{
    if (!username || username[0] == '\0') return;

    char home[USER_HOME_MAX];
    home[0] = '\0';
    u_str_append(home, sizeof(home), "/Users/");
    u_str_append(home, sizeof(home), username);

    char desktop[128];
    char documents[128];
    vfs_join_path(desktop, sizeof(desktop), home, "Desktop");
    vfs_join_path(documents, sizeof(documents), home, "Documents");

    vfs_mkdir(home);
    vfs_mkdir(desktop);
    vfs_mkdir(documents);

    /* Seed Welcome.txt on Desktop if not already present */
    char welcome_path[160];
    vfs_join_path(welcome_path, sizeof(welcome_path), desktop, "Welcome.txt");
    if (!vfs_exists(welcome_path)) {
        VfsFile *wf = vfs_open(welcome_path, VFS_O_CREATE | VFS_O_WRITE | VFS_O_TRUNC);
        if (wf) {
            char msg[256];
            msg[0] = '\0';
            u_str_append(msg, sizeof(msg), "Welcome to ojjyOS, ");
            u_str_append(msg, sizeof(msg), (full_name && full_name[0]) ? full_name : username);
            u_str_append(msg, sizeof(msg), "!\nCreated by Jonas Lee.\nYour files in ");
            u_str_append(msg, sizeof(msg), home);
            u_str_append(msg, sizeof(msg), " persist across reboots.\n");
            vfs_write(wf, msg, strlen(msg));
            vfs_close(wf);
        }
    }

    /* Seed Notes.txt in Documents if not already present */
    char notes_path[160];
    vfs_join_path(notes_path, sizeof(notes_path), documents, "Notes.txt");
    if (!vfs_exists(notes_path)) {
        VfsFile *nf = vfs_open(notes_path, VFS_O_CREATE | VFS_O_WRITE | VFS_O_TRUNC);
        if (nf) {
            char msg[256];
            msg[0] = '\0';
            u_str_append(msg, sizeof(msg), "- ");
            u_str_append(msg, sizeof(msg), (full_name && full_name[0]) ? full_name : username);
            u_str_append(msg, sizeof(msg), "'s Notes\n- Press Cmd+S to save changes\n");
            vfs_write(nf, msg, strlen(msg));
            vfs_close(nf);
        }
    }

    /* Seed Calendar.txt in Documents if not already present */
    char cal_path[160];
    vfs_join_path(cal_path, sizeof(cal_path), documents, "Calendar.txt");
    if (!vfs_exists(cal_path)) {
        VfsFile *cf = vfs_open(cal_path, VFS_O_CREATE | VFS_O_WRITE | VFS_O_TRUNC);
        if (cf) {
            RtcTime now;
            rtc_read_time(&now);
            char line[128];
            char num[16];
            line[0] = '\0';
            utoa(now.year, num, 10);
            u_str_append(line, sizeof(line), num);
            u_str_append(line, sizeof(line), "-");
            if (now.month < 10) u_str_append(line, sizeof(line), "0");
            utoa(now.month, num, 10);
            u_str_append(line, sizeof(line), num);
            u_str_append(line, sizeof(line), "-");
            if (now.day < 10) u_str_append(line, sizeof(line), "0");
            utoa(now.day, num, 10);
            u_str_append(line, sizeof(line), num);
            u_str_append(line, sizeof(line), "|09:00|Welcome to ojjyOS|Tahoe Desktop|Created by Jonas Lee\n");
            vfs_write(cf, line, strlen(line));
            vfs_close(cf);
        }
    }
}

/* =========================================================
 * Database Load & Save (/Library/Accounts/users.db)
 * ========================================================= */

static void parse_db_field(const char **cursor, char *out, size_t size)
{
    size_t i = 0;
    const char *p = *cursor;
    while (*p && *p != '|' && *p != '\n' && *p != '\r' && i + 1 < size) {
        out[i++] = *p++;
    }
    out[i] = '\0';
    if (*p == '|') p++;
    *cursor = p;
}

int users_save(void)
{
    vfs_mkdir(USERS_DB_DIR);

    VfsFile *file = vfs_open(USERS_DB_PATH, VFS_O_CREATE | VFS_O_WRITE | VFS_O_TRUNC);
    if (!file) {
        serial_printf("[USERS] Failed to open %s for writing\n", USERS_DB_PATH);
        return -1;
    }

    char line[256];
    if (last_username[0]) {
        line[0] = '\0';
        u_str_append(line, sizeof(line), "#last=");
        u_str_append(line, sizeof(line), last_username);
        u_str_append(line, sizeof(line), "\n");
        vfs_write(file, line, strlen(line));
    }

    for (int i = 0; i < USERS_MAX; i++) {
        if (!accounts[i].active) continue;
        UserAccount *u = &accounts[i];
        line[0] = '\0';
        u_str_append(line, sizeof(line), u->username);
        u_str_append(line, sizeof(line), "|");
        u_str_append(line, sizeof(line), u->full_name);
        u_str_append(line, sizeof(line), "|");
        u_str_append(line, sizeof(line), u->salt_hex);
        u_str_append(line, sizeof(line), "|");
        u_str_append(line, sizeof(line), u->pass_hash);
        u_str_append(line, sizeof(line), "|");
        u_str_append(line, sizeof(line), u->is_admin ? "1" : "0");
        u_str_append(line, sizeof(line), "|");
        u_str_append(line, sizeof(line), u->home_dir);
        u_str_append(line, sizeof(line), "\n");
        vfs_write(file, line, strlen(line));
    }

    vfs_close(file);
    system_request_sync();
    return 0;
}

void users_init(void)
{
    memset(accounts, 0, sizeof(accounts));
    current_user_idx = -1;
    session_authenticated = false;
    last_username[0] = '\0';

    vfs_mkdir(USERS_DB_DIR);

    VfsFile *file = vfs_open(USERS_DB_PATH, VFS_O_READ);
    if (!file) {
        serial_printf("[USERS] No existing account database (%s); first-boot setup active\n",
                      USERS_DB_PATH);
        return;
    }

    static char buf[2048];
    ssize_t n = vfs_read(file, buf, sizeof(buf) - 1);
    vfs_close(file);
    if (n <= 0) return;
    buf[n] = '\0';

    int slot = 0;
    const char *p = buf;
    while (*p && slot < USERS_MAX) {
        while (*p == '\r' || *p == '\n') p++;
        if (!*p) break;

        if (strncmp(p, "#last=", 6) == 0) {
            p += 6;
            size_t i = 0;
            while (*p && *p != '\r' && *p != '\n' && i + 1 < sizeof(last_username)) {
                last_username[i++] = *p++;
            }
            last_username[i] = '\0';
            continue;
        }

        UserAccount u;
        memset(&u, 0, sizeof(u));
        char admin_str[4];

        parse_db_field(&p, u.username, sizeof(u.username));
        parse_db_field(&p, u.full_name, sizeof(u.full_name));
        parse_db_field(&p, u.salt_hex, sizeof(u.salt_hex));
        parse_db_field(&p, u.pass_hash, sizeof(u.pass_hash));
        parse_db_field(&p, admin_str, sizeof(admin_str));
        parse_db_field(&p, u.home_dir, sizeof(u.home_dir));

        while (*p && *p != '\n') p++;

        if (u.username[0] && strlen(u.pass_hash) == 64) {
            u.is_admin = (admin_str[0] == '1');
            u.active = true;
            if (u.home_dir[0] == '\0') {
                u_str_append(u.home_dir, sizeof(u.home_dir), "/Users/");
                u_str_append(u.home_dir, sizeof(u.home_dir), u.username);
            }
            accounts[slot++] = u;
            users_provision_home(u.username, u.full_name);
        }
    }

    /* Select default user index for the login screen */
    if (slot > 0) {
        current_user_idx = 0;
        if (last_username[0]) {
            for (int i = 0; i < slot; i++) {
                if (u_streq_ci(accounts[i].username, last_username)) {
                    current_user_idx = i;
                    break;
                }
            }
        }
    }

    serial_printf("[USERS] Loaded %d user account(s)\n", slot);
}

int users_count(void)
{
    int count = 0;
    for (int i = 0; i < USERS_MAX; i++) {
        if (accounts[i].active) count++;
    }
    return count;
}

const UserAccount *users_get_by_index(int index)
{
    int seen = 0;
    for (int i = 0; i < USERS_MAX; i++) {
        if (!accounts[i].active) continue;
        if (seen == index) return &accounts[i];
        seen++;
    }
    return NULL;
}

const UserAccount *users_find(const char *username)
{
    if (!username || username[0] == '\0') return NULL;
    for (int i = 0; i < USERS_MAX; i++) {
        if (accounts[i].active && u_streq_ci(accounts[i].username, username)) {
            return &accounts[i];
        }
    }
    return NULL;
}

const UserAccount *users_current(void)
{
    if (current_user_idx >= 0 && current_user_idx < USERS_MAX &&
        accounts[current_user_idx].active) {
        return &accounts[current_user_idx];
    }
    return NULL;
}

int users_current_index(void)
{
    return current_user_idx;
}

bool users_is_authenticated(void)
{
    return session_authenticated && (users_current() != NULL);
}

const char *users_current_username(void)
{
    const UserAccount *u = users_current();
    return (u && u->username[0]) ? u->username : "guest";
}

const char *users_current_fullname(void)
{
    const UserAccount *u = users_current();
    if (u) {
        return u->full_name[0] ? u->full_name : u->username;
    }
    return "Guest User";
}

const char *users_current_full_name(void)
{
    return users_current_fullname();
}

const char *users_current_home(void)
{
    const UserAccount *u = users_current();
    return (u && u->home_dir[0]) ? u->home_dir : "/Users/guest";
}

int users_make_home_path(char *out, size_t size, const char *subpath)
{
    if (!out || size == 0) return -1;
    const char *home = users_current_home();
    if (!subpath || subpath[0] == '\0') {
        strncpy(out, home, size - 1);
        out[size - 1] = '\0';
        return 0;
    }
    return vfs_join_path(out, size, home, subpath);
}

int users_create(const char *username, const char *full_name,
                 const char *password, bool is_admin)
{
    if (!username || username[0] == '\0' || !password || password[0] == '\0') {
        return -1;
    }

    /* Validate username characters (alphanumeric, '_', '-', '.') */
    char clean_user[USER_NAME_MAX];
    size_t j = 0;
    for (size_t i = 0; username[i] && j + 1 < sizeof(clean_user); i++) {
        char c = u_ascii_lower(username[i]);
        if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
            c == '_' || c == '-' || c == '.') {
            clean_user[j++] = c;
        } else {
            return -1;
        }
    }
    clean_user[j] = '\0';
    if (j == 0) return -1;

    if (users_find(clean_user) != NULL) {
        return -2; /* Already exists */
    }

    int slot = -1;
    for (int i = 0; i < USERS_MAX; i++) {
        if (!accounts[i].active) {
            slot = i;
            break;
        }
    }
    if (slot < 0) return -3; /* Full */

    UserAccount *u = &accounts[slot];
    memset(u, 0, sizeof(*u));
    strncpy(u->username, clean_user, sizeof(u->username) - 1);
    if (full_name && full_name[0]) {
        /* Sanitize '|' out of full_name */
        size_t k = 0;
        for (size_t i = 0; full_name[i] && k + 1 < sizeof(u->full_name); i++) {
            u->full_name[k++] = (full_name[i] == '|') ? ' ' : full_name[i];
        }
        u->full_name[k] = '\0';
    } else {
        strncpy(u->full_name, clean_user, sizeof(u->full_name) - 1);
    }

    generate_salt_hex(clean_user, u->salt_hex);
    hash_password_salted(u->salt_hex, password, u->pass_hash);

    u->home_dir[0] = '\0';
    u_str_append(u->home_dir, sizeof(u->home_dir), "/Users/");
    u_str_append(u->home_dir, sizeof(u->home_dir), clean_user);
    u->is_admin = is_admin;
    u->active = true;

    users_provision_home(u->username, u->full_name);

    /* If no user was selected yet, select this new account */
    if (current_user_idx < 0) {
        current_user_idx = slot;
        strncpy(last_username, u->username, sizeof(last_username) - 1);
    }

    users_save();
    serial_printf("[USERS] Created account '%s' (%s), home=%s\n",
                  u->username, u->full_name, u->home_dir);
    return 0;
}

bool users_verify_credentials(const char *username, const char *password)
{
    if (!username || !password) return false;
    const UserAccount *u = users_find(username);
    if (!u) return false;

    char computed[65];
    hash_password_salted(u->salt_hex, password, computed);
    return hash_equal_ct(computed, u->pass_hash);
}

int users_login(const char *username, const char *password)
{
    if (!username || !password) return -1;
    for (int i = 0; i < USERS_MAX; i++) {
        if (accounts[i].active && u_streq_ci(accounts[i].username, username)) {
            char computed[65];
            hash_password_salted(accounts[i].salt_hex, password, computed);
            if (!hash_equal_ct(computed, accounts[i].pass_hash)) {
                return -1;
            }
            current_user_idx = i;
            session_authenticated = true;
            strncpy(last_username, accounts[i].username, sizeof(last_username) - 1);
            last_username[sizeof(last_username) - 1] = '\0';
            users_provision_home(accounts[i].username, accounts[i].full_name);
            users_save();
            serial_printf("[USERS] User '%s' logged in\n", accounts[i].username);
            return 0;
        }
    }
    return -1;
}

int users_change_password(const char *username, const char *old_password,
                          const char *new_password)
{
    if (!username || !new_password || new_password[0] == '\0') return -1;
    for (int i = 0; i < USERS_MAX; i++) {
        if (accounts[i].active && u_streq_ci(accounts[i].username, username)) {
            if (old_password != NULL) {
                char computed[65];
                hash_password_salted(accounts[i].salt_hex, old_password, computed);
                if (!hash_equal_ct(computed, accounts[i].pass_hash)) {
                    return -1;
                }
            }
            generate_salt_hex(accounts[i].username, accounts[i].salt_hex);
            hash_password_salted(accounts[i].salt_hex, new_password, accounts[i].pass_hash);
            users_save();
            serial_printf("[USERS] Password updated for '%s'\n", accounts[i].username);
            return 0;
        }
    }
    return -1;
}

int users_delete(const char *username)
{
    if (!username || users_count() <= 1) return -1;
    for (int i = 0; i < USERS_MAX; i++) {
        if (accounts[i].active && u_streq_ci(accounts[i].username, username)) {
            if (i == current_user_idx && session_authenticated) {
                return -1; /* Cannot delete currently logged-in user */
            }
            memset(&accounts[i], 0, sizeof(accounts[i]));
            users_save();
            return 0;
        }
    }
    return -1;
}

void users_lock_session(void)
{
    session_authenticated = false;
}

void users_logout(void)
{
    session_authenticated = false;
}
