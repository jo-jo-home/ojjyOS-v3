/* Crypto primitive tests. Created by Jonas Lee. Vectors: published RFC/NIST
 * test cases below plus tests/crypto_vectors.h generated from Python
 * hashlib/hmac and the OpenSSL CLI (tests/gen_crypto_vectors.py). */
#include "crypto/crypto.h"
#include "test_support.h"
#include "crypto_vectors.h"

static int hexval(char c) { return c <= '9' ? c - '0' : (c | 32) - 'a' + 10; }
static size_t unhex(const char *h, uint8_t *out)
{
    size_t n = 0;
    while (h[0] && h[1]) { out[n++] = (uint8_t)(hexval(h[0]) * 16 + hexval(h[1])); h += 2; }
    return n;
}
static bool eq_hex(const uint8_t *got, const char *hex)
{
    uint8_t want[256];
    size_t n = unhex(hex, want);
    return memcmp(got, want, n) == 0;
}

int main(void)
{
    uint8_t out[512], tag[16];

#define SHA_CHECK(n) sha256(sha_in_##n, sha_in_##n##_len, out); CHECK(memcmp(out, sha_out_##n, 32) == 0, "sha256 len " #n);
    SHA_CASES(SHA_CHECK)
    {
        Sha256 s;
        sha256_init(&s);
        for (size_t i = 0; i < sha_in_512_len; i += 7) sha256_update(&s, sha_in_512 + i, MIN(7, sha_in_512_len - i));
        sha256_final(&s, out);
        CHECK(memcmp(out, sha_out_512, 32) == 0, "sha256 incremental updates");
    }
    sha256("abc", 3, out);
    CHECK(eq_hex(out, "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"), "sha256 abc (FIPS)");

    /* HMAC: RFC 4231 test cases 1 and 6, plus a generated case */
    uint8_t k[131];
    memset(k, 0x0b, 20);
    hmac_sha256(k, 20, "Hi There", 8, out);
    CHECK(eq_hex(out, "b0344c61d8db38535ca8afceaf0bf12b881dc200c9833da726e9376c2e32cff7"), "hmac rfc4231 tc1");
    memset(k, 0xaa, 131);
    hmac_sha256(k, 131, "Test Using Larger Than Block-Size Key - Hash Key First", 54, out);
    CHECK(eq_hex(out, "60e431591ee0b67f0d8a26aacbf5b77f8e0bc6213728c5140546040f0ee37f54"), "hmac rfc4231 tc6 long key");
    hmac_sha256(hmac_key, hmac_key_len, hmac_data, hmac_data_len, out);
    CHECK(memcmp(out, hmac_out, 32) == 0, "hmac generated");

    /* HKDF: RFC 5869 test case 1 */
    uint8_t ikm[22], salt[13], info[10], prk[32];
    memset(ikm, 0x0b, 22);
    for (int i = 0; i < 13; i++) salt[i] = (uint8_t)i;
    for (int i = 0; i < 10; i++) info[i] = (uint8_t)(0xf0 + i);
    hkdf_extract(salt, 13, ikm, 22, prk);
    CHECK(eq_hex(prk, "077709362c2e32df0ddc3f0dc47bba6390b6c73bb50f9c3122ec844ad7c2b3e5"), "hkdf extract rfc5869");
    hkdf_expand(prk, info, 10, out, 42);
    CHECK(eq_hex(out, "3cb25f25faacd57a90434f64d0362f2a2d2d0a90cf1a5a4c5db02d56ecc4c5bf34007208d5b887185865"),
          "hkdf expand rfc5869 (42 bytes)");
    hkdf_expand_label(label_secret, "key", NULL, 0, out, 16);
    CHECK(memcmp(out, label_key16, 16) == 0, "tls13 expand-label key");
    hkdf_expand_label(label_secret, "iv", NULL, 0, out, 12);
    CHECK(memcmp(out, label_iv12, 12) == 0, "tls13 expand-label iv");
    uint8_t empty_hash[32];
    sha256("", 0, empty_hash);
    hkdf_expand_label(label_secret, "derived", empty_hash, 32, out, 32);
    CHECK(memcmp(out, label_derived, 32) == 0, "tls13 expand-label derived");
    hkdf_expand_label(label_secret, "c hs traffic", label_ctx, 32, out, 32);
    CHECK(memcmp(out, label_cfin, 32) == 0, "tls13 expand-label with context");

    /* X25519: RFC 7748 6.1 and generated OpenSSL keys */
    uint8_t a_priv[32], b_priv[32], pub[32], shared[32];
    unhex("77076d0a7318a57d3c16c17251b26645df4c2f87ebc0992ab177fba51db92c2a", a_priv);
    unhex("5dab087e624a8a4b79e17f8b83800ee66f3bb1292618b6fd1c2f8b27ff88e0eb", b_priv);
    x25519_base(pub, a_priv);
    CHECK(eq_hex(pub, "8520f0098930a754748b7ddcb43ef75a0dbf3a0d26381af4eba4a98eaa9b4e6a"), "x25519 alice public");
    x25519_base(pub, b_priv);
    CHECK(eq_hex(pub, "de9edb7d7b7dc1b4d35b61c2ece435373f8343c85b78674dadfc7e146f882b4f"), "x25519 bob public");
    x25519(shared, a_priv, pub);
    CHECK(eq_hex(shared, "4a5d9d5ba4ce2de1728e3bf480350f25e07e21c947d19e3376f09b3c1e161742"), "x25519 shared secret");
    x25519_base(pub, x_priv_a);
    CHECK(memcmp(pub, x_pub_a, 32) == 0, "x25519 public matches openssl");
    x25519(shared, x_priv_a, x_pub_b);
    CHECK(memcmp(shared, x_shared, 32) == 0, "x25519 derive matches openssl");

    /* AES-128 (FIPS 197 C.1) and GCM (McGrew-Viega test cases 1-4) */
    uint8_t key[16], pt[64], iv[12], aad[20], ct[64];
    unhex("000102030405060708090a0b0c0d0e0f", key);
    unhex("00112233445566778899aabbccddeeff", pt);
    Aes128Gcm g;
    aes128_gcm_init(&g, key);
    aes128_encrypt_block(g.rk, pt, out);
    CHECK(eq_hex(out, "69c4e0d86a7b0430d8cdb78070b4c55a"), "aes128 fips-197 block");
    memset(key, 0, 16); memset(iv, 0, 12);
    aes128_gcm_init(&g, key);
    aes128_gcm_seal(&g, iv, NULL, 0, NULL, 0, NULL, tag);
    CHECK(eq_hex(tag, "58e2fccefa7e3061367f1d57a4e7455a"), "gcm tc1 empty");
    memset(pt, 0, 16);
    aes128_gcm_seal(&g, iv, NULL, 0, pt, 16, ct, tag);
    CHECK(eq_hex(ct, "0388dace60b6a392f328c2b971b2fe78") && eq_hex(tag, "ab6e47d42cec13bdf53a67b21257bddf"), "gcm tc2");
    unhex("feffe9928665731c6d6a8f9467308308", key);
    unhex("cafebabefacedbaddecaf888", iv);
    unhex("d9313225f88406e5a55909c5aff5269a86a7a9531534f7da2e4c303d8a318a721c3c0c95956809532fcf0e2449a6b525"
          "b16aedf5aa0de657ba637b391aafd255", pt);
    aes128_gcm_init(&g, key);
    aes128_gcm_seal(&g, iv, NULL, 0, pt, 64, ct, tag);
    CHECK(eq_hex(ct, "42831ec2217774244b7221b784d0d49ce3aa212f2c02a4e035c17e2329aca12e21d514b25466931c7d8f6a5aac84aa05"
                     "1ba30b396a0aac973d58e091473f5985") && eq_hex(tag, "4d5c2af327cd64a62cf35abd2ba6fab4"), "gcm tc3");
    unhex("feedfacedeadbeeffeedfacedeadbeefabaddad2", aad);
    aes128_gcm_seal(&g, iv, aad, 20, pt, 60, ct, tag);
    CHECK(eq_hex(ct, "42831ec2217774244b7221b784d0d49ce3aa212f2c02a4e035c17e2329aca12e21d514b25466931c7d8f6a5aac84aa05"
                     "1ba30b396a0aac973d58e091") && eq_hex(tag, "5bc94fbc3221a5db94fae95ae7121a47"), "gcm tc4 with aad");
    uint8_t back[64];
    CHECK(aes128_gcm_open(&g, iv, aad, 20, ct, 60, tag, back) && memcmp(back, pt, 60) == 0, "gcm open round trip");
    tag[3] ^= 1;
    CHECK(!aes128_gcm_open(&g, iv, aad, 20, ct, 60, tag, back), "gcm rejects a bad tag");

    /* ChaCha20 / Poly1305 against OpenSSL, AEAD against RFC 8439 2.8.2 */
    chacha20_xor(chacha_key, 7, chacha_nonce, chacha_plain, out, chacha_plain_len);
    CHECK(memcmp(out, chacha_cipher, chacha_cipher_len) == 0, "chacha20 stream matches openssl (counter 7)");
#define POLY_CHECK(i) poly1305(poly_key_##i, poly_msg_##i, poly_msg_##i##_len, tag); \
    CHECK(memcmp(tag, poly_tag_##i, 16) == 0, "poly1305 matches openssl case " #i);
    POLY_CASES(POLY_CHECK)
    {
        uint8_t ckey[32], nonce[12], caad[12];
        for (int i = 0; i < 32; i++) ckey[i] = (uint8_t)(0x80 + i);
        unhex("070000004041424344454647", nonce);
        unhex("50515253c0c1c2c3c4c5c6c7", caad);
        const char *text = "Ladies and Gentlemen of the class of '99: If I could offer you only one tip for the "
                           "future, sunscreen would be it.";
        size_t n = strlen(text);
        chacha20_poly1305_seal(ckey, nonce, caad, 12, (const uint8_t *)text, n, out, tag);
        CHECK(eq_hex(out, "d31a8d34648e60db7b86afbc53ef7ec2a4aded51296e08fea9e2b5a736ee62d6") &&
              eq_hex(tag, "1ae10b594f09e26a7e902ecbd0600691"), "chacha20-poly1305 rfc8439 aead");
        uint8_t plain[128];
        CHECK(chacha20_poly1305_open(ckey, nonce, caad, 12, out, n, tag, plain) && memcmp(plain, text, n) == 0,
              "aead open round trip");
        out[5] ^= 0x40;
        CHECK(!chacha20_poly1305_open(ckey, nonce, caad, 12, out, n, tag, plain), "aead rejects tampering");
    }
    CHECK(crypto_equal("abc", "abc", 3) && !crypto_equal("abc", "abd", 3), "constant-time compare");
    return finish();
}
