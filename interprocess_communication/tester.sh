#!/bin/bash

# Minimal automated test suite for the message_slot kernel module

MODULE="message_slot.ko"
DEV="/dev/msg_slot_test"
MAJOR=235
SENDER=./message_sender
READER=./message_reader
PASS=0
FAIL=0

GREEN='[0;32m'
RED='[0;31m'
RESET='[0m'

pass() { echo -e "${GREEN}PASS${RESET}"; PASS=$((PASS+1)); }
fail()  { echo -e "${RED}FAIL${RESET} - $1"; FAIL=$((FAIL+1)); }

echo "Running tests for message_slot ..."

# Root required for insmod / mknod
if [[ $EUID -ne 0 ]]; then
  echo "Please run as root"; exit 1;
fi

make -s >/dev/null 2>&1 || { echo "make failed"; exit 1; }

insmod $MODULE 2>/dev/null || modprobe message_slot 2>/dev/null || true

# Create device node if absent
if [[ ! -e $DEV ]]; then
  mknod $DEV c $MAJOR 0 || { echo "mknod failed"; exit 1; }
fi

# --- Test 1 : plain send / receive ---
MSG1="HelloWorld"
$SENDER $DEV 100 0 "$MSG1" 2>/dev/null
OUT=$($READER $DEV 100 2>/dev/null)
[[ "$OUT" == "$MSG1" ]] && pass || fail "basic send/recv"

# --- Test 2 : censor active ---
MSG2="abcdefghi"
EXP="ab#de#gh#"
$SENDER $DEV 200 1 "$MSG2" 2>/dev/null
OUT=$($READER $DEV 200 2>/dev/null)
[[ "$OUT" == "$EXP" ]] && pass || fail "censor send/recv"

# --- Test 3 : read from empty channel should fail ---
$READER $DEV 300 >/dev/null 2>&1
[[ $? -ne 0 ]] && pass || fail "read empty channel"

# --- Test 4 : oversize write rejected ---
BIG=$(head -c 200 < /dev/zero | tr '\0' 'a')
$SENDER $DEV 400 0 "$BIG" >/dev/null 2>&1
[[ $? -ne 0 ]] && pass || fail "oversize write"

# --- Summary ---
TOTAL=$((PASS+FAIL))
echo "---"
echo "Total: $TOTAL  |  Passed: $PASS  |  Failed: $FAIL"

rmmod message_slot 2>/dev/null
exit $FAIL