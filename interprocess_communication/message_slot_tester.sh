#!/bin/bash


set -euo pipefail

# ── Config ────────────────────────────────────────────────────────
MODULE="message_slot.ko"
MAJOR=235
DEV0="/dev/msg_slot0"   # minor 0
DEV1="/dev/msg_slot1"   # minor 1
SENDER=./message_sender
READER=./message_reader
PASS=0
FAIL=0
CFLAGS="-O3 -Wall -std=c11"

GREEN=$'\e[32m'; RED=$'\e[31m'; RESET=$'\e[0m'

pass() {  printf "%bPASS%b\n"  "$GREEN" "$RESET";  PASS=$((PASS+1)); }
fail() {  printf "%bFAIL%b – %s\n" "$RED" "$RESET" "$1";  FAIL=$((FAIL+1)); }

cleanup() {
  rmmod message_slot 2>/dev/null || true
  rm -f "$DEV0" "$DEV1" sender_tmp reader_tmp
}
trap cleanup EXIT

# ── Root requirement ──────────────────────────────────────────────
[[ $EUID -eq 0 ]] || { echo "Please run as root"; exit 1; }

echo "Building kernel module + user programs …"
make -s || { echo "make failed"; exit 1; }

echo "Loading module …"
insmod "$MODULE" 2>/dev/null || modprobe message_slot 2>/dev/null || {
  echo "Cannot load module"; exit 1; }

# create two device files (minor 0 and 1)
for n in 0 1; do
  dev="/dev/msg_slot$n"
  [[ -e $dev ]] || mknod "$dev" c $MAJOR $n
  chmod 666 "$dev"
done

echo "Running tests …"

# ── helpers ───────────────────────────────────────────────────────
send()  { "$SENDER"  "$1" "$2" "$3" "$4"  >/dev/null 2>&1; }
read_msg() { "$READER" "$1" "$2" 2>/dev/null; }
expect_fail() { "$@" >/dev/null 2>&1 && fail "$3" || pass; }

# 1. basic write/read  ----------------------------------------------------------
MSG="HelloWorld"
send "$DEV0" 11 0 "$MSG"
[[ $(read_msg "$DEV0" 11) == "$MSG" ]] && pass || fail "basic write/read"

# 2. censor write/read  ---------------------------------------------------------
RAW="abcdefghi"
CEN="ab#de#gh#"
send "$DEV0" 22 1 "$RAW"
[[ $(read_msg "$DEV0" 22) == "$CEN" ]] && pass || fail "censor write/read"

# 3. read-empty-channel  --------------------------------------------------------
expect_fail read_msg "$DEV0" 33 "read empty channel"

# 4. oversize write (>128)  -----------------------------------------------------
BIG=$(head -c 200 < /dev/zero | tr '\0' 'a')
expect_fail send "$DEV0" 44 0 "$BIG" "oversize write"

# 5. zero-size write  -----------------------------------------------------------
expect_fail send "$DEV0" 55 0 "" "zero-size write"

# 6. write / read without channel set  -----------------------------------------
fd_noc=$(mktemp -u); exec {fd_noc}<> "$DEV0"
echo -n "data" >&$fd_noc 2>/dev/null && { fail "write w/o channel"; } || pass
dd bs=1 count=1 <&$fd_noc 2>/dev/null && { fail "read w/o channel"; } || pass
exec {fd_noc}>&-

# 7. insufficient buffer on read  ----------------------------------------------
#  Message of length 10, then attempt to read only 5 bytes via dd
send "$DEV0" 66 0 "1234567890"
( dd if="$DEV0" bs=5 count=1 2>/dev/null ) && fail "small buffer read" || pass

# 8. overwrite test  -----------------------------------------------------------
send "$DEV0" 77 0 "first"
send "$DEV0" 77 0 "second"
[[ $(read_msg "$DEV0" 77) == "second" ]] && pass || fail "overwrite"

# 9. read same message twice  ---------------------------------------------------
send "$DEV0" 88 0 "persist"
r1=$(read_msg "$DEV0" 88); r2=$(read_msg "$DEV0" 88)
[[ $r1 == "persist" && $r2 == "persist" ]] && pass || fail "double read"

# 10. UTF-8 round-trip  ---------------------------------------------------------
UTF=$'טקסט✓'
send "$DEV0" 99 0 "$UTF"
[[ $(read_msg "$DEV0" 99) == "$UTF" ]] && pass || fail "UTF-8 round-trip"

# 11. sharing channels test  ----------------------------------------------------
send "$DEV0" 1 0 "slot0_msg"
send "$DEV1" 1 0 "slot1_msg"
[[ $(read_msg "$DEV0" 1) == "slot0_msg" && $(read_msg "$DEV1" 1) == "slot1_msg" ]] \
  && pass || fail "different device files separation"

# 12. two processes on one device  ---------------------------------------------
#   (background subshells using different channels)
( send "$DEV0" 7 0 "procA" ) &
( send "$DEV0" 8 0 "procB" ) &
wait
[[ $(read_msg "$DEV0" 7) == "procA" && $(read_msg "$DEV0" 8) == "procB" ]] \
  && pass || fail "two procs on same dev"

# 13. ioctl affects only this fd  ----------------------------------------------
fd1=$(mktemp -u); fd2=$(mktemp -u)
exec {fd1}<> "$DEV0"
exec {fd2}<> "$DEV0"
ioctl -n $fd1 0x80014d01 123 2>/dev/null   # set channel=123 on fd1
ioctl -n $fd2 0x80014d01 456 2>/dev/null   # set channel=456 on fd2
send "/proc/self/fd/$fd1" 123 0 "A"   # uses fd1
send "/proc/self/fd/$fd2" 456 0 "B"   # uses fd2
[[ $(read_msg "/proc/self/fd/$fd1" 123) == "A" && \
   $(read_msg "/proc/self/fd/$fd2" 456) == "B" ]] && pass || fail "ioctl per-fd"
exec {fd1}>&- {fd2}>&-

# 14. ioctl does NOT affect future opens  --------------------------------------
send "$DEV0" 111 0 "OLD"
# open new fd without ioctl
if read_msg "$DEV0" 111 >/dev/null 2>&1; then
  fail "ioctl affects new fds"
else
  pass
fi

# 15. large channel id ( > 2^20 )  ---------------------------------------------
BIGID=5000000
send "$DEV0" $BIGID 0 "big"
[[ $(read_msg "$DEV0" $BIGID) == "big" ]] && pass || fail "large channel id"

# ── Summary ────────────────────────────────────────────────────────
TOTAL=$((PASS+FAIL))
echo "────────────────────────────────────────"
echo "Total: $TOTAL  |  Passed: $PASS  |  Failed: $FAIL"

exit $FAIL