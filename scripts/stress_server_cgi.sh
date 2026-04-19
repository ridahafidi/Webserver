#!/usr/bin/env bash
set -u

HOST="${HOST:-127.0.0.1}"
PORT="${PORT:-8080}"
TOTAL="${TOTAL:-4000}"
CONCURRENCY="${CONCURRENCY:-80}"
CGI_TOTAL="${CGI_TOTAL:-1200}"
CGI_CONCURRENCY="${CGI_CONCURRENCY:-40}"
ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
START_SERVER=0
SERVER_PID=""

need_cmd() {
    command -v "$1" >/dev/null 2>&1 || {
        echo "Missing command: $1"
        exit 1
    }
}

cleanup() {
    rm -f "$ROOT_DIR/.stress_post.bin"
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
        --total=*)
            TOTAL="${arg#*=}"
            ;;
        --concurrency=*)
            CONCURRENCY="${arg#*=}"
            ;;
        --cgi-total=*)
            CGI_TOTAL="${arg#*=}"
            ;;
        --cgi-concurrency=*)
            CGI_CONCURRENCY="${arg#*=}"
            ;;
        *)
            echo "Usage: $0 [--start] [--total=N] [--concurrency=N] [--cgi-total=N] [--cgi-concurrency=N]"
            exit 1
            ;;
    esac
done

need_cmd curl
need_cmd seq
need_cmd xargs
need_cmd sort
need_cmd uniq
need_cmd awk

if [ "$START_SERVER" -eq 1 ]; then
    pkill webserv >/dev/null 2>&1 || true
    (cd "$ROOT_DIR" && ./webserv config/default.conf >/tmp/webserv_stress.log 2>&1 &) 
    SERVER_PID=$!
    sleep 1
fi

echo "Stress config: TOTAL=$TOTAL CONCURRENCY=$CONCURRENCY CGI_TOTAL=$CGI_TOTAL CGI_CONCURRENCY=$CGI_CONCURRENCY"

echo
printf "[1/4] Static GET stress on / ...\n"
static_codes="$(mktemp)"
static_times="$(mktemp)"
seq 1 "$TOTAL" | xargs -P"$CONCURRENCY" -I{} sh -c \
  "curl -s -o /dev/null -w '%{http_code} %{time_total}\n' 'http://$HOST:$PORT/'" \
  > "${static_codes}.raw"
awk '{print $1}' "${static_codes}.raw" > "$static_codes"
awk '{print $2}' "${static_codes}.raw" > "$static_times"
sort "$static_codes" | uniq -c
awk '{sum+=$1; if(min==0 || $1<min) min=$1; if($1>max) max=$1} END {if(NR>0) printf("time_total avg=%.6fs min=%.6fs max=%.6fs\n", sum/NR, min, max)}' "$static_times"

echo
printf "[2/4] Mixed endpoint stress (/ /files /nope) ...\n"
mixed_codes="$(mktemp)"
(
  seq 1 "$((TOTAL / 3))" | xargs -P"$CONCURRENCY" -I{} curl -s -o /dev/null -w "%{http_code}\n" "http://$HOST:$PORT/"
  seq 1 "$((TOTAL / 3))" | xargs -P"$CONCURRENCY" -I{} curl -s -o /dev/null -w "%{http_code}\n" "http://$HOST:$PORT/files/"
  seq 1 "$((TOTAL / 3))" | xargs -P"$CONCURRENCY" -I{} curl -s -o /dev/null -w "%{http_code}\n" "http://$HOST:$PORT/nope_stress"
) > "$mixed_codes"
sort "$mixed_codes" | uniq -c

echo
printf "[3/4] CGI GET stress ...\n"
cgi_get_codes="$(mktemp)"
seq 1 "$CGI_TOTAL" | xargs -P"$CGI_CONCURRENCY" -I{} curl -s -o /dev/null -w "%{http_code}\n" "http://$HOST:$PORT/cgi-bin/hello.py" > "$cgi_get_codes"
sort "$cgi_get_codes" | uniq -c

echo
printf "[4/4] CGI POST stress ...\n"
head -c 256 </dev/zero > "$ROOT_DIR/.stress_post.bin"
cgi_post_codes="$(mktemp)"
seq 1 "$CGI_TOTAL" | xargs -P"$CGI_CONCURRENCY" -I{} curl -s -o /dev/null -w "%{http_code}\n" -X POST --data-binary "@$ROOT_DIR/.stress_post.bin" "http://$HOST:$PORT/cgi-bin/hello.py" > "$cgi_post_codes"
sort "$cgi_post_codes" | uniq -c

echo
printf "Post-stress health check: "
curl -s -o /dev/null -w "%{http_code} %{time_total}s\n" "http://$HOST:$PORT/"

rm -f "$static_codes" "$static_times" "${static_codes}.raw" "$mixed_codes" "$cgi_get_codes" "$cgi_post_codes"

echo "Done."
