#!/usr/bin/env bash
set -u

HOST="${HOST:-127.0.0.1}"
PORT_MAIN="${PORT_MAIN:-8080}"
PORT_ALT="${PORT_ALT:-8081}"
API_HOST="${API_HOST:-api.local}"
ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
START_SERVER=0
SERVER_PID=""

PASS_COUNT=0
FAIL_COUNT=0

need_cmd() {
    command -v "$1" >/dev/null 2>&1 || {
        echo "Missing command: $1"
        exit 1
    }
}

pass() {
    PASS_COUNT=$((PASS_COUNT + 1))
    printf "[PASS] %s\n" "$1"
}

fail() {
    FAIL_COUNT=$((FAIL_COUNT + 1))
    printf "[FAIL] %s\n" "$1"
}

expect_code() {
    name="$1"
    expected="$2"
    shift 2
    code="$(curl -s -o /dev/null -w "%{http_code}" "$@")"
    if [ "$code" = "$expected" ]; then
        pass "$name => $code"
    else
        fail "$name => got $code expected $expected"
    fi
}

expect_raw_status() {
    name="$1"
    expected="$2"
    payload="$3"
    line="$(printf "%b" "$payload" | nc -w 1 "$HOST" "$PORT_MAIN" | head -n 1 | tr -d '\r')"
    if [ "$line" = "$expected" ]; then
        pass "$name => $line"
    else
        fail "$name => got '$line' expected '$expected'"
    fi
}

cleanup() {
    if [ "$START_SERVER" -eq 1 ] && [ -n "$SERVER_PID" ]; then
        kill "$SERVER_PID" >/dev/null 2>&1 || true
        wait "$SERVER_PID" 2>/dev/null || true
    fi
    rm -f "$ROOT_DIR/.delete_canary" "$ROOT_DIR/.tmp_big_eval.bin"
    rm -f "$ROOT_DIR/www/<xss>.txt"
}

trap cleanup EXIT

for arg in "$@"; do
    case "$arg" in
        --start)
            START_SERVER=1
            ;;
        *)
            echo "Usage: $0 [--start]"
            exit 1
            ;;
    esac
done

need_cmd curl
need_cmd nc
need_cmd seq
need_cmd xargs
need_cmd head
need_cmd grep
need_cmd sort
need_cmd uniq

if [ "$START_SERVER" -eq 1 ]; then
    pkill webserv >/dev/null 2>&1 || true
    (cd "$ROOT_DIR" && ./webserv config/default.conf >/tmp/webserv_full_scan.log 2>&1 &) 
    SERVER_PID=$!
    sleep 1
fi

echo "== Core HTTP =="
expect_code "GET / 8080" 200 "http://$HOST:$PORT_MAIN/"
expect_code "GET / 8081" 200 "http://$HOST:$PORT_ALT/"
expect_code "VHOST api.local" 200 -H "Host: $API_HOST" "http://$HOST:$PORT_MAIN/"
expect_code "POST /files" 405 -X POST -d "x=1" "http://$HOST:$PORT_MAIN/files"
expect_code "GET /old" 301 --max-redirs 0 "http://$HOST:$PORT_MAIN/old"
expect_code "CGI GET" 200 "http://$HOST:$PORT_MAIN/cgi-bin/hello.py"
expect_code "Upload small" 201 -X POST --data-binary "abc" "http://$HOST:$PORT_MAIN/upload"
expect_code "OPTIONS /" 501 -X OPTIONS "http://$HOST:$PORT_MAIN/"
expect_code "PUT /" 501 -X PUT -d "x" "http://$HOST:$PORT_MAIN/"

echo "== Parser/Protocol Abuse =="
expect_raw_status "Bad request line" "HTTP/1.1 400 Bad Request" "BADREQUEST\r\n\r\n"
expect_raw_status "HTTP/9.9 rejected" "HTTP/1.1 400 Bad Request" "GET / HTTP/9.9\r\nHost: localhost\r\n\r\n"
expect_raw_status "Malformed header no colon" "HTTP/1.1 400 Bad Request" "GET / HTTP/1.1\r\nHost localhost\r\n\r\n"
expect_raw_status "Duplicate CL mismatch" "HTTP/1.1 400 Bad Request" "POST /upload HTTP/1.1\r\nHost: localhost\r\nContent-Length: 3\r\nContent-Length: 10\r\n\r\nabc"
expect_raw_status "Invalid CL token" "HTTP/1.1 400 Bad Request" "POST /upload HTTP/1.1\r\nHost: localhost\r\nContent-Length: 1x\r\n\r\na"
expect_raw_status "CL+TE conflict" "HTTP/1.1 400 Bad Request" "POST /upload HTTP/1.1\r\nHost: localhost\r\nContent-Length: 1\r\nTransfer-Encoding: chunked\r\n\r\n3\r\nabc\r\n0\r\n\r\n"
expect_raw_status "Chunk invalid size" "HTTP/1.1 400 Bad Request" "POST /upload HTTP/1.1\r\nHost: localhost\r\nTransfer-Encoding: chunked\r\n\r\nG\r\nabc\r\n0\r\n\r\n"
expect_raw_status "Chunk valid" "HTTP/1.1 201 Created" "POST /upload HTTP/1.1\r\nHost: localhost\r\nTransfer-Encoding: chunked\r\n\r\n3\r\nabc\r\n0\r\n\r\n"

BIG_HEADER="$(printf 'a%.0s' $(seq 1 20000))"
expect_code "Huge header" 400 -H "X-Long: $BIG_HEADER" "http://$HOST:$PORT_MAIN/"

echo "== Traversal/XSS =="
expect_code "Traversal GET" 403 --path-as-is "http://$HOST:$PORT_MAIN/../../README.md"

echo "canary" > "$ROOT_DIR/.delete_canary"
expect_code "Traversal DELETE" 403 --path-as-is -X DELETE "http://$HOST:$PORT_MAIN/../../.delete_canary"
if [ -f "$ROOT_DIR/.delete_canary" ]; then
    pass "Traversal DELETE kept canary file"
else
    fail "Traversal DELETE removed canary file"
fi

mkdir -p "$ROOT_DIR/www"
touch "$ROOT_DIR/www/<xss>.txt"
if curl -s "http://$HOST:$PORT_MAIN/files/" | grep -q "&lt;xss&gt;.txt"; then
    pass "Autoindex escapes HTML filename"
else
    fail "Autoindex does not escape HTML filename"
fi

echo "== Limits =="
head -c 2097152 </dev/zero > "$ROOT_DIR/.tmp_big_eval.bin"
expect_code "api.local 2MB POST" 413 -H "Host: $API_HOST" -X POST --data-binary "@$ROOT_DIR/.tmp_big_eval.bin" "http://$HOST:$PORT_MAIN/"

echo "== Concurrency Smoke =="
codes_file="$(mktemp)"
seq 1 800 | xargs -P40 -I{} curl -s -o /dev/null -w "%{http_code}\n" "http://$HOST:$PORT_MAIN/" > "$codes_file"
if [ "$(grep -c '^200$' "$codes_file")" -eq 800 ]; then
    pass "800 concurrent GET / all 200"
else
    fail "Concurrency smoke had non-200 statuses"
    sort "$codes_file" | uniq -c
fi
rm -f "$codes_file"

echo
printf "Summary: pass=%d fail=%d\n" "$PASS_COUNT" "$FAIL_COUNT"
if [ "$FAIL_COUNT" -ne 0 ]; then
    exit 1
fi
exit 0
