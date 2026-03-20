#!/usr/bin/env bash
set -euo pipefail

display_id=":1"
chat_name="cppclawbot"
keep_draft=0
screenshot_path=""
restore_focus=1
restore_window_id=""

usage() {
  cat <<'EOF'
Usage:
  telegram_send_message.sh [--display :1] [--chat cppclawbot]
                           [--keep-draft] [--screenshot /tmp/file.png]
                           [--no-restore-focus]
                           [--restore-window 0x01234567]
                           "message text"
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

if [[ $# -ne 1 ]]; then
  usage >&2
  exit 2
fi

message="$1"

export DISPLAY="$display_id"
export XAUTHORITY="${XAUTHORITY:-$HOME/.Xauthority}"

run_xdo() {
  xdotool "$@"
}

current_active_window() {
  xdotool getactivewindow 2>/dev/null || true
}

previous_window_id="$restore_window_id"
if [[ -z "$previous_window_id" && "$restore_focus" -eq 1 ]]; then
  previous_window_id="$(current_active_window)"
fi

switch_to_english_input() {
  if command -v ibus >/dev/null 2>&1; then
    ibus engine xkb:us::eng >/dev/null 2>&1 || true
  fi
  if command -v fcitx5-remote >/dev/null 2>&1; then
    fcitx5-remote -s keyboard-us >/dev/null 2>&1 || true
  fi
  if command -v fcitx-remote >/dev/null 2>&1; then
    fcitx-remote -c >/dev/null 2>&1 || true
  fi
}

wid="$(wmctrl -lx | awk '$3 ~ /Telegram.TelegramDesktop/ {print $1; exit}')"
if [[ -z "$wid" ]]; then
  echo "Telegram Desktop window not found" >&2
  exit 1
fi

run_xdo windowactivate --sync "$wid" || true
sleep 0.5

eval "$(DISPLAY="$display_id" xdotool getwindowgeometry --shell "$wid" |
  sed 's/^/TG_/')"

search_x=$((TG_WIDTH * 16 / 100))
search_y=$((TG_HEIGHT * 4 / 100))
input_x=$((TG_WIDTH * 56 / 100))
input_y=$((TG_HEIGHT * 96 / 100))

switch_to_english_input
run_xdo key --window "$wid" Escape || true
sleep 0.2

run_xdo mousemove --window "$wid" "$search_x" "$search_y" click 1
sleep 0.3
run_xdo key --clearmodifiers --window "$wid" ctrl+a
sleep 0.2
run_xdo key --window "$wid" BackSpace
sleep 0.2
run_xdo type --delay 1 --window "$wid" "$chat_name"
sleep 0.8
run_xdo key --window "$wid" Return
sleep 1.0

switch_to_english_input
run_xdo mousemove --window "$wid" "$input_x" "$input_y" click 1
sleep 0.3
if [[ "$keep_draft" -eq 0 ]]; then
  run_xdo key --clearmodifiers --window "$wid" ctrl+a
  sleep 0.2
  run_xdo key --window "$wid" BackSpace
  sleep 0.2
fi
run_xdo type --delay 1 --window "$wid" "$message"
sleep 0.4
run_xdo key --window "$wid" Return
sleep 0.8

if [[ -n "$screenshot_path" ]]; then
  DISPLAY="$display_id" gnome-screenshot -w -f "$screenshot_path" \
    >/dev/null 2>&1 || true
fi

if [[ "$restore_focus" -eq 1 && -n "$previous_window_id" &&
      "$previous_window_id" != "$wid" ]]; then
  run_xdo windowactivate --sync "$previous_window_id" || true
  sleep 0.3
fi

printf 'telegram_window=%s\nrestore_window=%s\nchat=%s\nmessage=%s\n' \
  "$wid" "$previous_window_id" "$chat_name" "$message"
