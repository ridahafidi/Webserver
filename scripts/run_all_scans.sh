#!/usr/bin/env bash
set -u

ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
REPORT_DIR="$ROOT_DIR/reports"
TIMESTAMP="$(date +%Y%m%d_%H%M%S)"
RUN_DIR="$REPORT_DIR/scan_$TIMESTAMP"
SUMMARY_FILE="$RUN_DIR/summary.txt"
FULL_LOG="$RUN_DIR/full_scan.log"
STRESS_LOG="$RUN_DIR/stress_scan.log"
SERVER_LOG="$RUN_DIR/server.log"

START_SERVER=0
QUICK_MODE=0
SERVER_PID=""

# Defaults for stress script when launched from wrapper.
STRESS_TOTAL="${STRESS_TOTAL:-1200}"
STRESS_CONCURRENCY="${STRESS_CONCURRENCY:-40}"
CGI_TOTAL="${CGI_TOTAL:-400}"
CGI_CONCURRENCY="${CGI_CONCURRENCY:-20}"

need_cmd() {
    command -v "$1" >/dev/null 2>&1 || {
        echo "Missing command: $1"
        exit 1
    }
}

cleanup() {
    if [ "$START_SERVER" -eq 1 ] && [ -n "$SERVER_PID" ]; then
        kill "$SERVER_PID" >/dev/null 2>&1 || true
        wait "$SERVER_PID" 2>/dev/null || true
    fi
}

trap cleanup EXIT

for arg in "$@"; do
    case "$arg" in
        --start)
            START_SERVER=1
            ;;
        --quick)
            QUICK_MODE=1
            ;;
        *)
            echo "Usage: $0 [--start] [--quick]"
            echo "  --start : start webserv once from this wrapper"
            echo "  --quick : lighter stress values (faster check)"
            exit 1
            ;;
    esac
done

if [ "$QUICK_MODE" -eq 1 ]; then
    STRESS_TOTAL=300
    STRESS_CONCURRENCY=20
    CGI_TOTAL=120
    CGI_CONCURRENCY=10
fi

need_cmd mkdir
need_cmd date
need_cmd tee
need_cmd bash

mkdir -p "$RUN_DIR"

{
    echo "Webserv unified scan"
    echo "timestamp=$TIMESTAMP"
    echo "root=$ROOT_DIR"
    echo "start_server=$START_SERVER"
    echo "quick_mode=$QUICK_MODE"
    echo "stress_total=$STRESS_TOTAL"
    echo "stress_concurrency=$STRESS_CONCURRENCY"
    echo "cgi_total=$CGI_TOTAL"
    echo "cgi_concurrency=$CGI_CONCURRENCY"
} > "$SUMMARY_FILE"

if [ "$START_SERVER" -eq 1 ]; then
    pkill webserv >/dev/null 2>&1 || true
    (cd "$ROOT_DIR" && ./webserv config/default.conf >"$SERVER_LOG" 2>&1) &
    SERVER_PID=$!
    sleep 1
fi

echo "Running full_scan.sh..."
if (cd "$ROOT_DIR" && bash scripts/full_scan.sh) | tee "$FULL_LOG"; then
    FULL_STATUS=0
    echo "full_scan_status=PASS" >> "$SUMMARY_FILE"
else
    FULL_STATUS=$?
    echo "full_scan_status=FAIL($FULL_STATUS)" >> "$SUMMARY_FILE"
fi

echo "Running stress_server_cgi.sh..."
if (cd "$ROOT_DIR" && bash scripts/stress_server_cgi.sh \
    --total="$STRESS_TOTAL" \
    --concurrency="$STRESS_CONCURRENCY" \
    --cgi-total="$CGI_TOTAL" \
    --cgi-concurrency="$CGI_CONCURRENCY") | tee "$STRESS_LOG"; then
    STRESS_STATUS=0
    echo "stress_scan_status=PASS" >> "$SUMMARY_FILE"
else
    STRESS_STATUS=$?
    echo "stress_scan_status=FAIL($STRESS_STATUS)" >> "$SUMMARY_FILE"
fi

if [ "$FULL_STATUS" -eq 0 ] && [ "$STRESS_STATUS" -eq 0 ]; then
    echo "overall_status=PASS" >> "$SUMMARY_FILE"
    echo "All scans passed. Report folder: $RUN_DIR"
    exit 0
fi

echo "overall_status=FAIL" >> "$SUMMARY_FILE"
echo "Some scans failed. Report folder: $RUN_DIR"
exit 1
