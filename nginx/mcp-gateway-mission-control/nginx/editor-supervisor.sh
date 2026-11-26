#!/bin/sh
# Demo-only supervisor: runs NGINX as a daemon and applies config changes
# requested by the Mission Control editor.
#
# The repository copy of nginx.conf (/demo-source/nginx.conf, read-only) is never
# modified. Every container start loads it fresh, so restarting this container
# discards any live edits made during rehearsal.
set -u
SRC=/demo-source/nginx.conf
LIVE=/etc/nginx/nginx.conf
CTL=/demo-control

rm -f "$CTL/request" "$CTL/candidate" "$CTL/previous" "$CTL/test-log"
cp "$SRC" "$LIVE" && cp "$SRC" "$CTL/original" && cp "$SRC" "$CTL/live.conf" || exit 1
printf 'ok: loaded repository nginx.conf at container start\n' > "$CTL/status"
nginx || exit 1
trap 'nginx -s quit >/dev/null 2>&1 || true; exit 0' TERM INT

# install_and_reload <file> <ok-message> <fail-message>
install_and_reload() {
  cp "$LIVE" "$CTL/previous" || { printf 'error: could not snapshot current config\n' > "$CTL/status"; return; }
  if ! cat "$1" > "$LIVE" 2> "$CTL/test-log"; then
    cat "$CTL/previous" > "$LIVE"
    printf 'error: could not write nginx.conf (see details)\n' > "$CTL/status"
    return
  fi
  if nginx -t > "$CTL/test-log" 2>&1 && nginx -s reload >> "$CTL/test-log" 2>&1; then
    cp "$LIVE" "$CTL/live.conf"
    printf '%s\n' "$2" > "$CTL/status"
  else
    cat "$CTL/previous" > "$LIVE"
    nginx -s reload >/dev/null 2>&1 || true
    printf '%s\n' "$3" > "$CTL/status"
  fi
}

while :; do
  if [ -f "$CTL/request" ]; then
    action=$(cat "$CTL/request" 2>/dev/null || true)
    rm -f "$CTL/request"
    case "$action" in
      apply)
        if [ -f "$CTL/candidate" ]; then
          install_and_reload "$CTL/candidate" \
            'ok: configuration validated and reload requested' \
            'error: invalid configuration; previous configuration restored (see validation details)'
        else
          printf 'error: no candidate configuration\n' > "$CTL/status"
        fi
        rm -f "$CTL/candidate" ;;
      restore)
        install_and_reload "$CTL/original" \
          'ok: original configuration restored and reloaded' \
          'error: restore failed; previous configuration restored (see validation details)' ;;
      *)
        printf 'error: invalid request\n' > "$CTL/status" ;;
    esac
  fi
  sleep 0.25
done
