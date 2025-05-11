#!/bin/bash

# === Configuration ===
SHELL_EXEC="./a.out" # Path to the compiled shell executable
TEMP_DIR="shell_test_temp_$$" # Unique temp dir per run
VERBOSE=0 # Set to 1 for more verbose output during tests

# === State ===
PASS_COUNT=0
FAIL_COUNT=0
TEST_NO=0
CORE_DUMPS_FOUND=0

# === Colors ===
GREEN='\033[0;32m'
RED='\033[0;31m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
MAGENTA='\033[0;35m'
RESET='\033[0m'

# === Helper Functions ===

# Function to clean up temporary files and directories
cleanup() {
    if [ "$VERBOSE" -eq 1 ]; then echo "🧹 Cleaning up $TEMP_DIR..."; fi
    rm -rf "$TEMP_DIR"
    # Optionally remove core dumps if desired after checking
    # find . -maxdepth 1 -name 'core*' -delete
}

# Trap cleanup function on exit, interrupt, terminate
trap cleanup EXIT SIGINT SIGTERM

# Increment test number and print test description
start_test() {
    TEST_NO=$((TEST_NO + 1))
    echo -n -e "${BLUE}Test $TEST_NO: $1 ... ${RESET}"
}

# Print PASSED status
print_pass() {
    echo -e "${GREEN}PASSED${RESET}"
    PASS_COUNT=$((PASS_COUNT + 1))
}

# Print FAILED status and details
print_fail() {
    local reason="$1"
    local cmd="$2"
    local expected="$3"
    local got_stdout="$4"
    local got_stderr="$5"

    echo -e "${RED}FAILED${RESET}"
    echo "  Reason: $reason"
    [ -n "$cmd" ] && echo "  Command: $cmd"
    [ -n "$expected" ] && echo "  Expected: $expected"
    [ -n "$got_stdout" ] && echo -e "  Got stdout:\n<<<\n$got_stdout\n>>>"
    [ -n "$got_stderr" ] && echo -e "  Got stderr:\n<<<\n$got_stderr\n>>>"
    FAIL_COUNT=$((FAIL_COUNT + 1))
}

# Run command and check stdout/stderr for a substring
# Usage: run_test_output "Description" "command" "expected_substring" ["stdout"|"stderr"|"any"] [exact_match?]
run_test_output() {
    local desc="$1"
    local cmd="$2"
    local expected="$3"
    local check_where="${4:-any}" # Where to check: stdout, stderr, or any
    local exact_match="${5:-no}" # Use "yes" for exact line match
    start_test "$desc"

    local out_file="$TEMP_DIR/out.txt"
    local err_file="$TEMP_DIR/err.txt"
    echo "$cmd" | $SHELL_EXEC > "$out_file" 2> "$err_file"
    local exit_code=$?

    local stdout_content=$(cat "$out_file")
    local stderr_content=$(cat "$err_file")
    local combined_content="$stdout_content\n$stderr_content"

    local found=0
    local grep_opts="-q"
    [ "$exact_match" == "yes" ] && grep_opts="-Fxq"

    if [[ "$check_where" == "stdout" && $(grep $grep_opts "$expected" "$out_file"; echo $?) -eq 0 ]]; then
        found=1
    elif [[ "$check_where" == "stderr" && $(grep $grep_opts "$expected" "$err_file"; echo $?) -eq 0 ]]; then
        found=1
    elif [[ "$check_where" == "any" && $(echo -e "$combined_content" | grep $grep_opts "$expected"; echo $?) -eq 0 ]]; then
        found=1
    fi

    if [ $found -eq 1 ]; then
        print_pass
    else
        print_fail "Substring not found" "$cmd" "Substring '$expected' in '$check_where' (Exact: $exact_match)" "$stdout_content" "$stderr_content"
    fi
}

# Run command and check file content
# Usage: run_test_file_content "Description" "command" "filename" "expected_content"
run_test_file_content() {
    local desc="$1"
    local cmd="$2"
    local filename="$TEMP_DIR/$3"
    local expected="$4"
    start_test "$desc"

    local err_file="$TEMP_DIR/err.txt"
    echo "$cmd" | $SHELL_EXEC > /dev/null 2> "$err_file"
    sleep 0.1 # Give FS time

    local stderr_content=$(cat "$err_file")

    if [ ! -f "$filename" ]; then
        print_fail "Output file not created" "$cmd" "File '$filename' to contain '$expected'" "" "$stderr_content"
    elif ! grep -Fxq "$expected" "$filename"; then
        local file_content=$(cat "$filename")
        print_fail "Incorrect file content" "$cmd" "File '$filename' to contain exactly '$expected'" "$file_content" "$stderr_content"
    elif [ -s "$err_file" ]; then
         local file_content=$(cat "$filename")
         print_fail "Stderr not empty" "$cmd" "File '$filename' contains '$expected', empty stderr" "$file_content" "$stderr_content"
    else
        print_pass
    fi
}

# Run command and check file permissions (requires stat)
# Usage: run_test_file_perms "Description" "command" "filename" "expected_perms (e.g., 600)"
run_test_file_perms() {
    local desc="$1"
    local cmd="$2"
    local filename="$TEMP_DIR/$3"
    local expected_perms="$4"
    start_test "$desc"

    local err_file="$TEMP_DIR/err.txt"
    echo "$cmd" | $SHELL_EXEC > /dev/null 2> "$err_file"
    sleep 0.1 # Give FS time

    local stderr_content=$(cat "$err_file")

    if [ ! -f "$filename" ]; then
        print_fail "Output file not created" "$cmd" "File '$filename' with permissions '$expected_perms'" "" "$stderr_content"
    elif ! stat -c "%a" "$filename" | grep -q "$expected_perms"; then
        local actual_perms=$(stat -c "%a" "$filename")
        print_fail "Incorrect file permissions" "$cmd" "File '$filename' permissions '$expected_perms'" "Actual: $actual_perms" "$stderr_content"
    elif [ -s "$err_file" ]; then
        print_fail "Stderr not empty" "$cmd" "File '$filename' permissions '$expected_perms', empty stderr" "" "$stderr_content"
    else
        print_pass
    fi
}


# Run command and check file does NOT exist
# Usage: run_test_no_file "Description" "command" "filename" "expected_stderr_substring"
run_test_no_file() {
    local desc="$1"
    local cmd="$2"
    local filename="$TEMP_DIR/$3"
    local expected_err="$4"
    start_test "$desc"

    local err_file="$TEMP_DIR/err.txt"
    echo "$cmd" | $SHELL_EXEC > /dev/null 2> "$err_file"
    sleep 0.1

    local stderr_content=$(cat "$err_file")

    if [ -f "$filename" ]; then
        print_fail "File was unexpectedly created" "$cmd" "File '$filename' should not exist" "" "$stderr_content"
    # Check if expected error substring is present in stderr
    elif [[ -n "$expected_err" ]] && ! grep -q "$expected_err" "$err_file"; then
         print_fail "Expected error message not found in stderr" "$cmd" "Stderr contains '$expected_err'" "" "$stderr_content"
    # If no specific error expected, just check stderr is non-empty (implies child error)
    elif [[ -z "$expected_err" ]] && [ ! -s "$err_file" ]; then
        print_fail "Stderr expected to be non-empty (indicating child error)" "$cmd" "Non-empty stderr" "" ""
    # Pass if file doesn't exist AND (expected error found OR no specific error expected but stderr has something)
    else
        print_pass
    fi
}


# Run background command and check timing
# Usage: run_test_background_timing "Description" "command" max_time_secs
run_test_background_timing() {
    local desc="$1"
    local cmd="$2"
    local max_time="${3:-1}" # Default max 1 second
    start_test "$desc"

    local err_file="$TEMP_DIR/err.txt"
    local start=$(date +%s.%N)
    echo "$cmd" | $SHELL_EXEC > /dev/null 2> "$err_file"
    local end=$(date +%s.%N)
    local elapsed=$(echo "$end - $start" | bc)
    local stderr_content=$(cat "$err_file")

    if (( $(echo "$elapsed < $max_time" | bc -l) )) && [ ! -s "$err_file" ]; then
        print_pass
    else
        print_fail "Background command took too long or produced stderr" "$cmd" "Return < ${max_time}s, empty stderr" "Elapsed: ${elapsed}s" "$stderr_content"
    fi
}

# === Test Execution ===

# --- Setup ---
echo "🚀 Starting Mini Shell Test Suite..."
if [ ! -f "$SHELL_EXEC" ]; then
    echo -e "${RED}Error: Shell executable '$SHELL_EXEC' not found. Compile myshell.c first.${RESET}"
    exit 1
fi
# Clean previous temp dir if exists, then create fresh
cleanup
mkdir -p "$TEMP_DIR" || { echo -e "${RED}Failed to create temp directory $TEMP_DIR${RESET}"; exit 1; }
echo "Test file for input redirection." > "$TEMP_DIR/input.txt"
echo "Initial content for overwrite test." > "$TEMP_DIR/overwrite.txt"
touch "$TEMP_DIR/dummy1" "$TEMP_DIR/dummy2"

# --- 1. Normal Command Execution (Foreground) ---
echo -e "\n--- Testing Normal Execution ---"
run_test_output "Foreground: echo" "echo Hello Shell" "Hello Shell" "stdout" "yes"
run_test_output "Foreground: ls" "ls $TEMP_DIR" "dummy1" "stdout"
run_test_output "Foreground: sleep (check wait)" "sleep 0.1" "" "any" # Just check it runs without error, implies waiting
run_test_output "Foreground: No arguments" "pwd" "$PWD" "stdout" # Checks commands without arguments
run_test_output "Foreground: Many arguments" "echo a b c d e f g h i j k l m n o p" "a b c d e f g h i j k l m n o p" "stdout" "yes"
run_test_output "Foreground: Full path execution" "/bin/pwd" "$PWD" "stdout"
run_test_output "Foreground: Command fails" "false" "" "any" # Command runs but exits non-zero, no output expected

# --- 2. Background Execution (&) ---
echo -e "\n--- Testing Background Execution (&) ---"
run_test_background_timing "Background: sleep 1 &" "sleep 1 &" 1
run_test_background_timing "Background: Instant command (&)" "true &" 0.1 # Should return very fast
# Check stderr for error from background child
run_test_output "Background: Erroring command (&)" "ls $TEMP_DIR/no_such_file_for_bg &" "No such file or directory" "stderr"

# --- 3. Pipe Execution (|) ---
echo -e "\n--- Testing Pipe Execution (|) ---"
run_test_output "Pipe: Simple" "echo Simple Pipe | cat" "Simple Pipe" "stdout" "yes"
run_test_output "Pipe: Multi-stage (3)" "echo Stage1 | cat | wc -c" "7" "stdout" # "Stage1\n" is 7 chars
run_test_output "Pipe: Command fails in middle" "echo PipeFail | false | cat" "" "any" # Expect no output as middle fails
run_test_output "Pipe: Command fails at end" "echo PipeFailEnd | false" "" "any"
run_test_output "Pipe: Command fails at start" "false | cat" "" "any"
# Build max pipe command (9 pipes = 10 stages)
max_pipe_cmd="echo MaxPipeTest"
for i in {1..9}; do max_pipe_cmd="$max_pipe_cmd | cat"; done
run_test_output "Pipe: Max pipes (9)" "$max_pipe_cmd" "MaxPipeTest" "stdout" "yes"
# Test large output through pipe (stress buffering/fds) - use head to limit actual output
run_test_output "Pipe: Large output" "yes | head -n 5" $'y\ny\ny\ny\ny' "stdout" "yes"

# --- 4. Input Redirection (<) ---
echo -e "\n--- Testing Input Redirection (<) ---"
run_test_output "Input Redir: cat < input.txt" "cat < $TEMP_DIR/input.txt" "Test file for input redirection." "stdout" "yes"
start_test "Input Redir: from /dev/null (check empty stdout)"
local out_file_20="$TEMP_DIR/out_20.txt"
local err_file_20="$TEMP_DIR/err_20.txt"
echo "cat < /dev/null" | $SHELL_EXEC > "$out_file_20" 2> "$err_file_20"
if [ ! -s "$out_file_20" ] && [ ! -s "$err_file_20" ]; then # Check if stdout AND stderr are empty
    print_pass
else
    print_fail "Stdout/Stderr not empty" "cat < /dev/null" "Empty stdout and stderr" "$(cat $out_file_20)" "$(cat $err_file_20)"
fi

run_test_output "Input Redir: Error non-existent file" "cat < $TEMP_DIR/no_such_file.txt" "No such file or directory" "stderr"

# --- 5. Output Redirection (>) ---
echo -e "\n--- Testing Output Redirection (>) ---"
run_test_file_content "Output Redir: Create file" "echo Output Content > $TEMP_DIR/outfile.txt" "outfile.txt" "Output Content"
run_test_file_perms "Output Redir: Check permissions (0600)" "echo Perms > $TEMP_DIR/perms.txt" "perms.txt" "600"
run_test_file_content "Output Redir: Truncate file" "echo New Content > $TEMP_DIR/overwrite.txt" "overwrite.txt" "New Content"
# Test permission error for output redir
mkdir -p "$TEMP_DIR/no_write_dir"
chmod 000 "$TEMP_DIR/no_write_dir"
run_test_no_file "Output Redir: Permission denied" "echo Denied > $TEMP_DIR/no_write_dir/denied.txt" "no_write_dir/denied.txt" "Permission denied"
chmod 755 "$TEMP_DIR/no_write_dir" # Cleanup permissions before removal

# --- 6. Error Handling & Invalid Syntax ---
echo -e "\n--- Testing Error Handling & Invalid Syntax ---"
run_test_output "Error: Command not found" "a_very_unlikely_command_name" "No such file or directory" "stderr"
# Test executable permission error
echo "#!/bin/bash\necho Executable" > "$TEMP_DIR/noexec.sh"
chmod 000 "$TEMP_DIR/noexec.sh"
run_test_output "Error: Execution permission denied" "./$TEMP_DIR/execvp" "Permission denied" "stderr"
chmod 755 "$TEMP_DIR/noexec.sh" # Cleanup permissions
# Test parent syntax errors (checking stderr as your code does)
run_test_output "Syntax Error: '&' alone" "&" "Invalid command" "stderr"
run_test_output "Syntax Error: 'cmd |'" "echo foo |" "Invalid pipe syntax" "stderr"
run_test_output "Syntax Error: '| cmd'" "| cat" "Invalid pipe syntax" "stderr"
run_test_output "Syntax Error: 'cmd | | cmd'" "echo foo | | cat" "Invalid pipe syntax" "stderr"
run_test_output "Syntax Error: Missing redir filename '<'" "cat <" "Missing filename" "stderr"
run_test_output "Syntax Error: Missing redir filename '>'" "echo foo >" "Missing filename" "stderr"

# --- 7. Stress Tests (Potential Zombies/Crashes) ---
echo -e "\n--- Testing Stress & Potential Issues ---"
# Run many short background jobs quickly to stress SIGCHLD handler
start_test "Stress: Multiple quick background jobs (&)"
multi_bg_cmd=""
for i in {1..20}; do multi_bg_cmd+="true & "; done
multi_bg_cmd+="echo DoneMultiBG" # A final foreground command to ensure shell processes the background ones
# Check for core dumps (segmentation fault indicator)
start_test "Check: Core dump existence"
if ls core* 1> /dev/null 2>&1 || ls "$TEMP_DIR/core*" 1> /dev/null 2>&1 ; then
    CORE_DUMPS_FOUND=1
    print_fail "Core dump file found!" "" "No core dump files" "$(ls core* 2>/dev/null)" "$(ls $TEMP_DIR/core* 2>/dev/null)"
else
    print_pass
fi


# --- 8. Signal Handling (Manual Tests Recommended) ---
echo -e "\n--- Signal Handling (Manual Checks Required) ---"
echo -e "${YELLOW}ℹ️ Manual Test 1: Foreground SIGINT${RESET}"
echo "  - Run '$SHELL_EXEC' manually."
echo "  - Type 'sleep 10' and press Enter."
echo "  - Press Ctrl+C while sleep is running."
echo "  - ${GREEN}Expected:${RESET} 'sleep' terminates immediately, shell prints a new prompt."
echo -e "${YELLOW}ℹ️ Manual Test 2: Background SIGINT${RESET}"
echo "  - Run '$SHELL_EXEC' manually."
echo "  - Type 'sleep 10 &' and press Enter."
echo "  - Shell should print a new prompt immediately."
echo "  - Press Ctrl+C."
echo "  - ${GREEN}Expected:${RESET} Shell ignores Ctrl+C (stays running at prompt). The background 'sleep' process continues running."
echo -e "${MAGENTA}ℹ️ Manual Test 3: Zombie Process Check${RESET}"
echo "  - Run '$SHELL_EXEC' manually."
echo "  - Type 'sleep 0.2 &' several times (e.g., 5-10 times) rapidly."
echo "  - Wait a second or two."
echo "  - In *another* terminal window, run: ${BLUE}ps -u $USER | grep '[z] defunct'${RESET}"
echo "  - (Replace $USER with your username if needed)."
echo "  - ${GREEN}Expected:${RESET} No processes should be listed as '<defunct>'. If they appear briefly and disappear, the handler is working. If they persist, there's a problem."

# --- Footer ---
echo -e "\n==============================="
echo "📊 Test Suite Summary"
echo "==============================="
echo -e "${GREEN}✅ PASSED: $PASS_COUNT${RESET}"
echo -e "${RED}❌ FAILED: $FAIL_COUNT${RESET}"
[ $CORE_DUMPS_FOUND -ne 0 ] && echo -e "${RED}🔥 WARNING: Core dump files indicate a crash occurred!${RESET}"
echo "==============================="

# Exit with success if no tests failed and no core dumps
if [ $FAIL_COUNT -eq 0 ] && [ $CORE_DUMPS_FOUND -eq 0 ]; then
    exit 0
else
    exit 1
fi