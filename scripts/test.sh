#!/bin/bash
# Host regression suite. Created by Jonas Lee.
set -euo pipefail
ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT_DIR"
OUT="${OJJY_TEST_DIR:-$ROOT_DIR/build/tests}"
HOST_CC="${HOST_CC:-clang}"
mkdir -p "$OUT"
FLAGS=(-O1 -fno-builtin -Wno-incompatible-library-redeclaration -Wall -Wextra -Werror=return-type -Ikernel/src)
if [ "${OJJY_SANITIZE:-0}" = "1" ]; then
    FLAGS+=(-g -fsanitize=address,undefined -fno-omit-frame-pointer)
    # The freestanding libc must not interpose on ASan's own startup calls.
    # Still instrument and exercise the real implementations under prefixes.
    for symbol in memset memcpy memmove memcmp strlen strcpy strncpy strcmp strncmp; do
        FLAGS+=("-D$symbol=ojjy_$symbol")
    done
fi
"$HOST_CC" "${FLAGS[@]}" tests/test_fs.c kernel/src/fs/{ojfs,vfs}.c kernel/src/string.c -o "$OUT/test_fs"
"$HOST_CC" "${FLAGS[@]}" tests/test_ramfs.c kernel/src/fs/{ramfs,vfs}.c kernel/src/{users,memory,proc,string}.c -o "$OUT/test_ramfs"
"$HOST_CC" "${FLAGS[@]}" -DOJJY_HOST_TEST tests/test_input.c kernel/src/drivers/{input,ps2_keyboard}.c \
    kernel/src/arch/aarch64/virtio_input.c kernel/src/string.c -o "$OUT/test_input"
"$HOST_CC" "${FLAGS[@]}" tests/test_persist.c kernel/src/fs/{persist,ramfs,vfs}.c kernel/src/string.c -o "$OUT/test_persist"
"$HOST_CC" "${FLAGS[@]}" -DOJJY_HOST_TEST tests/test_virtqueue.c kernel/src/arch/aarch64/virtio.c kernel/src/string.c -o "$OUT/test_virtqueue"
"$HOST_CC" "${FLAGS[@]}" tests/test_acpi_aml.c kernel/src/drivers/acpi_aml.c kernel/src/string.c -o "$OUT/test_acpi_aml"
"$HOST_CC" "${FLAGS[@]}" tests/test_user.c kernel/src/{user,user_image,string}.c -o "$OUT/test_user"
"$HOST_CC" "${FLAGS[@]}" tests/test_framebuffer.c kernel/src/{framebuffer,string}.c -o "$OUT/test_framebuffer"
"$HOST_CC" "${FLAGS[@]}" -DOJJY_HOST_TEST tests/test_net.c kernel/src/net/{net,tcp}.c kernel/src/crypto/{sha256,chacha_poly}.c kernel/src/{random,string}.c -o "$OUT/test_net"
"$HOST_CC" "${FLAGS[@]}" -DOJJY_HOST_TEST tests/test_web.c kernel/src/web/{url,http,html}.c kernel/src/crypto/{sha256,chacha_poly}.c kernel/src/{random,string}.c -o "$OUT/test_web"
"$HOST_CC" "${FLAGS[@]}" -Itests tests/test_crypto.c kernel/src/crypto/{sha256,x25519,aes_gcm,chacha_poly}.c kernel/src/string.c -o "$OUT/test_crypto"
for test in test_fs test_ramfs test_input test_persist test_virtqueue test_acpi_aml test_user test_framebuffer test_net test_web test_crypto; do
    echo "Running $test"
    "$OUT/$test"
done
python3 -B tests/test_image.py
