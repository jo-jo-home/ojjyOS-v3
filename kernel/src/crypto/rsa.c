/*
 * ojjyOS v3 - RSA signature verification (RFC 8017)
 *
 * Public-key verification only: PKCS#1 v1.5 (fixed DigestInfo prefixes)
 * and RSASSA-PSS (MGF1 over the digest). Moduli 2048..4096 bits.
 *
 * ojjyOS - Created by Jonas Lee
 */
#include "crypto.h"

static int hash_digest_len(RsaHashId id)
{
    return id == RSA_HASH_SHA256 ? 32 : id == RSA_HASH_SHA384 ? 48 : 64;
}

/* EMSA-PKCS1-v1_5 DigestInfo DER prefixes (no need for an ASN.1 writer) */
static const uint8_t pkcs1_prefix[][19] = {
    /* sha256WithRSAEncryption */
    { 0x30, 0x31, 0x30, 0x0d, 0x06, 0x09, 0x60, 0x86, 0x48, 0x01, 0x65,
      0x03, 0x04, 0x02, 0x01, 0x05, 0x00, 0x04, 0x20 },
    /* sha384WithRSAEncryption */
    { 0x30, 0x41, 0x30, 0x0d, 0x06, 0x09, 0x60, 0x86, 0x48, 0x01, 0x65,
      0x03, 0x04, 0x02, 0x02, 0x05, 0x00, 0x04, 0x30 },
    /* sha512WithRSAEncryption */
    { 0x30, 0x51, 0x30, 0x0d, 0x06, 0x09, 0x60, 0x86, 0x48, 0x01, 0x65,
      0x03, 0x04, 0x02, 0x03, 0x05, 0x00, 0x04, 0x40 },
};

/* Compute sig^e mod n, returned as big-endian bytes padded to mod_bytes. */
static bool rsa_public_op(const uint8_t *n_be, size_t n_len, uint32_t e,
                          const uint8_t *sig, size_t sig_len,
                          uint8_t *em, size_t em_len)
{
    static BigNum bn_n, bn_e, bn_s, bn_m;
    bn_set_bytes(&bn_n, n_be, n_len);
    bn_set_u32(&bn_e, e);
    bn_set_bytes(&bn_s, sig, sig_len);
    if (bn_cmp(&bn_s, &bn_n) >= 0) return false;   /* sig must be < n */
    if (!bn_mod_exp(&bn_m, &bn_s, &bn_e, &bn_n)) return false;
    bn_get_bytes(&bn_m, em, em_len);
    return true;
}

bool rsa_verify_pkcs1(const uint8_t *n_be, size_t n_len, uint32_t e,
                      const uint8_t *sig, size_t sig_len,
                      RsaHashId hash_id, const uint8_t *digest)
{
    if (n_len < 256 / 8 || n_len > (size_t)BN_LIMBS * 4) return false;
    if (sig_len != n_len) return false;

    static uint8_t em[BN_LIMBS * 4];
    if (!rsa_public_op(n_be, n_len, e, sig, sig_len, em, n_len)) return false;

    /* EM = 0x00 || 0x01 || PS(0xFF..) || 0x00 || DigestInfo || digest.
     * Compare exactly; the whole buffer is attacker-controlled, so a
     * plain (non-constant-time) compare is fine here. */
    size_t dlen = (size_t)hash_digest_len(hash_id);
    size_t t_len = 19 + dlen;                      /* DigestInfo || H */
    if (n_len < t_len + 11) return false;
    const uint8_t *em_digest = em + n_len - t_len;

    if (em[0] != 0x00 || em[1] != 0x01) return false;
    for (size_t i = 2; i < n_len - t_len - 1; i++) {
        if (em[i] != 0xFF) return false;
    }
    if (em[n_len - t_len - 1] != 0x00) return false;
    for (size_t i = 0; i < 19; i++) {
        if (em_digest[i] != pkcs1_prefix[hash_id][i]) return false;
    }
    for (size_t i = 0; i < dlen; i++) {
        if (em_digest[19 + i] != digest[i]) return false;
    }
    return true;
}

/* MGF1 (RFC 8017 B.2.1) with the same hash as the signature. */
static void mgf1(RsaHashId id, const uint8_t *seed, size_t seed_len,
                 uint8_t *mask, size_t mask_len)
{
    size_t dlen = (size_t)hash_digest_len(id);
    uint8_t counter[4], block[64];
    size_t done = 0;
    for (uint32_t c = 0; done < mask_len; c++) {
        counter[0] = (uint8_t)(c >> 24);
        counter[1] = (uint8_t)(c >> 16);
        counter[2] = (uint8_t)(c >> 8);
        counter[3] = (uint8_t)c;
        /* hash(seed || counter) without building a concatenated buffer:
         * use the incremental API. */
        Sha512 s;
        if (id == RSA_HASH_SHA256) {
            Sha256 s256;
            sha256_init(&s256);
            sha256_update(&s256, seed, seed_len);
            sha256_update(&s256, counter, 4);
            sha256_final(&s256, block);
        } else if (id == RSA_HASH_SHA384) {
            sha384_init(&s);
            sha384_update(&s, seed, seed_len);
            sha384_update(&s, counter, 4);
            sha384_final(&s, block);
        } else {
            sha512_init(&s);
            sha512_update(&s, seed, seed_len);
            sha512_update(&s, counter, 4);
            sha512_final(&s, block);
        }
        size_t take = mask_len - done < dlen ? mask_len - done : dlen;
        for (size_t i = 0; i < take; i++) mask[done + i] = block[i];
        done += take;
    }
}

bool rsa_verify_pss(const uint8_t *n_be, size_t n_len, uint32_t e,
                    const uint8_t *sig, size_t sig_len,
                    RsaHashId hash_id, const uint8_t *m_hash,
                    int salt_len)
{
    if (n_len < 256 / 8 || n_len > (size_t)BN_LIMBS * 4) return false;
    if (sig_len != n_len) return false;
    size_t h_len = (size_t)hash_digest_len(hash_id);
    if (salt_len < 0) salt_len = (int)h_len;        /* -1 = hash length */
    size_t s_len = (size_t)salt_len;
    if (n_len < h_len + s_len + 2) return false;

    /* emBits = modBits - 1 from the actual modulus. The leftmost
     * 8*emLen - emBits bits of DB must be zero after unmasking. */
    static BigNum bn_n;
    bn_set_bytes(&bn_n, n_be, n_len);
    int mod_bits = bn_bits(&bn_n);
    if (mod_bits < 512 || mod_bits > (int)(n_len * 8) ||
        mod_bits < (int)(n_len * 8) - 7) return false;
    size_t em_bits = (size_t)mod_bits - 1;
    size_t top_zero_bits = n_len * 8 - em_bits;    /* 1..8 */

    static uint8_t em[BN_LIMBS * 4];
    if (!rsa_public_op(n_be, n_len, e, sig, sig_len, em, n_len)) return false;

    if (em[n_len - 1] != 0xBC) return false;

    size_t db_len = n_len - h_len - 1;
    uint8_t *db = em;                               /* em[0..db_len) */
    uint8_t *h = em + db_len;

    static uint8_t db_mask[BN_LIMBS * 4];
    mgf1(hash_id, h, h_len, db_mask, db_len);
    for (size_t i = 0; i < db_len; i++) db[i] ^= db_mask[i];

    /* RFC 8017 8.1.2 step 10: zero the leftmost 8*emLen - emBits bits of
     * DB. The encoder cleared them in the MASKED form, so after unmasking
     * they are legitimately garbage and must be ignored, not rejected. */
    db[0] &= (uint8_t)(0xFFu >> top_zero_bits);

    for (size_t i = 0; i < db_len - s_len - 1; i++) {
        if (db[i] != 0x00) return false;
    }
    if (db[db_len - s_len - 1] != 0x01) return false;

    /* M' = 0x00*8 || mHash || salt; H' = Hash(M') must equal h. */
    uint8_t zero8[8] = { 0 };
    uint8_t h_check[64];
    Sha512 s;
    if (hash_id == RSA_HASH_SHA256) {
        Sha256 s256;
        sha256_init(&s256);
        sha256_update(&s256, zero8, 8);
        sha256_update(&s256, m_hash, h_len);
        sha256_update(&s256, db + db_len - s_len, s_len);
        sha256_final(&s256, h_check);
    } else if (hash_id == RSA_HASH_SHA384) {
        sha384_init(&s);
        sha384_update(&s, zero8, 8);
        sha384_update(&s, m_hash, h_len);
        sha384_update(&s, db + db_len - s_len, s_len);
        sha384_final(&s, h_check);
    } else {
        sha512_init(&s);
        sha512_update(&s, zero8, 8);
        sha512_update(&s, m_hash, h_len);
        sha512_update(&s, db + db_len - s_len, s_len);
        sha512_final(&s, h_check);
    }
    return crypto_equal(h, h_check, h_len) != 0;
}
