#!/usr/bin/env bash
set -euo pipefail

display_id=":1"
chat_name="cppclawbot"
timeout_secs=120
poll_interval_secs=1
keep_draft=0
screenshot_path=""
restore_focus=1
restore_window_id=""
log_file=""
message=""

usage() {
  cat <<'EOF'
Usage:
  telegram_validate_message.sh [--display :1] [--chat cppclawbot]
                               [--timeout 120] [--poll-interval 1]
                               [--log-file /path/to/log]
                               [--keep-draft] [--screenshot /tmp/file.png]
                               [--no-restore-focus]
                               [--restore-window 0x01234567]
                               ["message text"]

If no message is supplied, a unique ASCII probe message is generated.
The script sends the message via telegram_send_message.sh, waits for
RavBot to log receipt of that exact text, and then waits for a Telegram
delivery success/failure line after receipt.
EOF
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --display)
      display_id="${2:?missing display id}"
      shift 2
      ;;
    --chat)
      chat_name="${2:?missing chat name}"
      shift 2
      ;;
    --timeout)
      timeout_secs="${2:?missing timeout seconds}"
      shift 2
      ;;
    --poll-interval)
      poll_interval_secs="${2:?missing poll interval seconds}"
      shift 2
      ;;
    --log-file)
      log_file="${2:?missing log file}"
      shift 2
      ;;
    --keep-draft)
      keep_draft=1
      shift
      ;;
    --screenshot)
      screenshot_path="${2:?missing screenshot path}"
      shift 2
      ;;
    --no-restore-focus)
      restore_focus=0
      shift
      ;;
    --restore-window)
      restore_window_id="${2:?missing window id}"
      shift 2
      ;;
    -h|--help)
      usage
      exit 0
      ;;
    --)
      shift
      break
      ;;
    -*)
      echo "Unknown option: $1" >&2
      usage >&2
      exit 2
      ;;
    *)
      break
      ;;
  esac
done

if [[ $# -gt 1 ]]; then
  usage >&2
  exit 2
fi

if [[ $# -eq 1 ]]; then
  message="$1"
else
  message="Validation probe $(date +%H%M%S). Reply OK."
fi
receipt_probe="${message:0:40}"

if [[ -z "$log_file" ]]; then
  log_file="$HOME/.ravbot/logs/ravbot_$(date +%F).log"
fi

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
send_script="${script_dir}/telegram_send_message.sh"

if [[ ! -x "$send_script" ]]; then
  echo "Required send script not found or not executable: $send_script" >&2
  exit 1
fi

mkdir -p "$(dirname "$log_file")"
touch "$log_file"

if ! [[ "$timeout_secs" =~ ^[0-9]+$ ]] || ! [[ "$poll_interval_secs" =~ ^[0-9]+$ ]] ||
    [[ "$timeout_secs" -le 0 ]] || [[ "$poll_interval_secs" -le 0 ]]; then
  echo "timeout and poll interval must be positive integers" >&2
  exit 2
fi

start_line="$(wc -l < "$log_file")"

send_args=(
  --display "$display_id"
  --chat "$chat_name"
)
if [[ "$keep_draft" -eq 1 ]]; then
  send_args+=(--keep-draft)
fi
if [[ -n "$screenshot_path" ]]; then
  send_args+=(--screenshot "$screenshot_path")
fi
if [[ "$restore_focus" -eq 0 ]]; then
  send_args+=(--no-restore-focus)
fi
if [[ -n "$restore_window_id" ]]; then
  send_args+=(--restore-window "$restore_window_id")
fi

bash "$send_script" "${send_args[@]}" "$message"

find_after_line() {
  local file="$1"
  local line="$2"
  local pattern="$3"
  awk -v start="$line" 'NR > start' "$file" | rg -n "$pattern" | head -n 1
}

received_line=""
received_text=""
received_chat_id=""
reply_line=""
reply_text=""
deadline=$((SECONDS + timeout_secs))

while (( SECONDS < deadline )); do
  if [[ -z "$received_line" ]]; then
    received_match="$(
      awk -v start="$start_line" 'NR > start' "$log_file" |
        rg -n 'Received message from (Telegram )?[0-9]+' |
        rg -F "$receipt_probe" |
        head -n 1 || true
    )"
    if [[ -n "$received_match" ]]; then
      rel_line="${received_match%%:*}"
      line_text="${received_match#*:}"
      received_line=$((start_line + rel_line))
      received_text="$line_text"
      received_chat_id="$(
        printf '%s\n' "$received_text" |
          sed -n 's/.*session=agent:main:telegram:\([^): ]*\).*/\1/p'
      )"
    fi
  fi

  if [[ -n "$received_line" && -z "$reply_line" ]]; then
    if [[ -n "$received_chat_id" ]]; then
      reply_match="$(
        find_after_line \
          "$log_file" "$received_line" \
          "Sent response to Telegram chat ${received_chat_id}|Failed to deliver response to Telegram chat ${received_chat_id}" || true
      )"
    else
      reply_match="$(
        find_after_line \
          "$log_file" "$received_line" \
          'Sent response to Telegram chat|Failed to deliver response to Telegram|Telegram send failed' || true
      )"
    fi
    if [[ -n "$reply_match" ]]; then
      rel_line="${reply_match%%:*}"
      reply_line=$((received_line + rel_line))
      reply_text="${reply_match#*:}"
      break
    fi
  fi

  sleep "$poll_interval_secs"
done

if [[ -z "$received_line" ]]; then
  echo "Timed out waiting for Telegram receipt in $log_file for message: $message" >&2
  tail -n 40 "$log_file" >&2 || true
  exit 1
fi

if [[ -z "$reply_line" ]]; then
  echo "Timed out waiting for Telegram reply delivery in $log_file for message: $message" >&2
  tail -n 60 "$log_file" >&2 || true
  exit 1
fi

reply_status="success"
if [[ "$reply_text" != *"Sent response to Telegram chat"* ]]; then
  reply_status="failure"
fi

printf 'message=%s\nreceipt_probe=%s\nlog_file=%s\nreceived_line=%s\nreceived_chat_id=%s\nreceived=%s\nreply_line=%s\nreply_status=%s\nreply=%s\n' \
  "$message" "$receipt_probe" "$log_file" "$received_line" \
  "${received_chat_id:-unknown}" "$received_text" "$reply_line" \
  "$reply_status" "$reply_text"

if [[ "$reply_status" != "success" ]]; then
  exit 1
fi
