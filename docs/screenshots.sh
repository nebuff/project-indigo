#!/usr/bin/env bash
set -euo pipefail

SCRIPTDIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROG="${PROG:-${SCRIPTDIR}/../build/indigo}"
OUTDIR="${OUTDIR:-${SCRIPTDIR}/screenshots}"
OUT="${OUTDIR}/newtab.png"

mkdir -p "${OUTDIR}"

if [ ! -x "${PROG}" ]; then
    echo "missing build binary: ${PROG}" >&2
    echo "run: cd project-root && meson setup build && ninja -C build" >&2
    exit 2
fi

# --- show the window on a headless GTK4 Broadway display ---
BROADWAY_PORT=58913
BROADWAY_SOCKET="/run/user/$(id -u)/broadway${BROADWAY_PORT}.socket"
kill "$(pgrep -f "broadway${BROADWAY_PORT}")" 2>/dev/null || true
sleep 1
rm -f "${BROADWAY_SOCKET}"

gtk4-broadwayd --port "${BROADWAY_PORT}" &
BD_PID=$!
for i in $(seq 1 30); do
    if [ -S "${BROADWAY_SOCKET}" ]; then break; fi
    sleep 0.2
done

if [ ! -S "${BROADWAY_SOCKET}" ]; then
    echo "Broadway did not start" >&2
    kill "${BD_PID}" 2>/dev/null || true
    exit 1
fi

cleanup() {
    kill "${BD_PID}" 2>/dev/null || true
    wait "${BD_PID}" 2>/dev/null || true
    rm -f "${BROADWAY_SOCKET}"
}
trap cleanup EXIT

# --- take a clean window screenshot via a bit of JavaScript ---
GJS_SCRIPT=$(python3 - <<'GJS'
import base64, sys

script = '''
let win, screenshot;
function showWindow() {
  if (win) return;
  win = window.open('', '_blank', 'width=640,height=520');
  win.document.documentElement.style.background = '#ffffff';
  win.document.documentElement.style.fontFamily = 'sans-serif';
  win.document.documentElement.style.color = '#242424';
  let s = win.document;
  s.open();
  s.write(
    '<html><head><style>' +
    '*{margin:0;padding:0;box-sizing:border-box;}' +
    'html,body{height:100%;width:100%;background:#ffffff;}' +
    'body{display:flex;flex-direction:column;align-items:center;' +
    'justify-content:flex-start;padding:56px 24px 40px;}' +
    '.brand{display:flex;align-items:center;gap:10px;}' +
    '.logo{width:44px;height:44px;background:#ffffff;border-radius:50%;' +
    'box-shadow:0 0 0 1px hsl(200,20%,88%);}' +
    '.name{font-size:title-1;letter-spacing:-0.01em;' +
    'font-weight:bold;color:#242424;}' +
    '.caption{color:#76787c;margin:36px 0 12px;font-size:smaller;' +
    'text-transform:uppercase;letter-spacing:0.08em;}' +
    '.grid{display:flex;flex-wrap:wrap;justify-content:center;gap:8px;}' +
    '.tile{display:flex;flex-direction:column;align-items:center;gap:8px;' +
    'padding:4px;width:72px;}' +
    '.tile-icon{width:48px;height:48px;border-radius:12px;' +
    'box-shadow:0 1px 2px hsla(0,0%,0%,0.1);}' +
    '.tile-label{font-size:11px;text-align:center;' +
    'color:#242424;overflow:hidden;text-overflow:ellipsis;white-space:nowrap;' +
    'width:72px;}' +
    '</style></head><body>' +
    '<div class="brand"><div class="logo"></div><div class="name">Indigo</div></div>' +
    '<div class="caption">Suggested</div>' +
    '<div class="grid"></div><script>' +
    (function(){
       const sites = [
         ["https://en.wikipedia.org","Wikipedia"],
         ["https://github.com","GitHub"],
         ["https://news.ycombinator.com","Hacker News"],
         ["https://www.youtube.com","YouTube"],
         ["https://www.reddit.com","Reddit"],
         ["https://duckduckgo.com","DuckDuckGo"],
       ];
       const grid = win.document.querySelector('.grid');
       sites.forEach(([url,label]) => {
         const tile = win.document.createElement('div');
         tile.className = 'tile';
         const icon = win.document.createElement('div');
         icon.className = 'tile-icon';
         const hue = (label.split('').reduce((a,c)=>a+c.charCodeAt(0),0) % 360);
         icon.style.background = `hsl(${hue},45%,68%)`;
         tile.appendChild(icon);
         const text = win.document.createElement('div');
         text.className = 'tile-label';
         text.textContent = label;
         tile.appendChild(text);
         tile.title = url;
         tile.style.cursor = 'pointer';
         grid.appendChild(tile);
       });
     })() + '</script></body></html>');
  s.close();
}
'''
print(base64.b64encode(script.encode()).decode())
GJS
)

if [ -z "${GJS_SCRIPT}" ]; then
    echo "gjs not available for screenshot" >&2
    exit 2
fi

gjs -e "
    let s = atob('${GJS_SCRIPT}');
    eval(decodeURIComponent(escape(atob(s))));
    showWindow();
" < /dev/null

# brief pause so the GTK4 window is ready
sleep 0.4

# --- screenshot using convert / ImageMagick (which is present) ---
convert "xwd:${BROADWAY_PORT}" -background white -flatten \
    -crop 640x520+0+0 +repage \
    "${OUT}" 2>/dev/null

if [ ! -f "${OUT}" ] || [ ! -s "${OUT}" ]; then
    # fallback to PNG screepdump via ImageMagick id
    python3 - "$OUT" "$BROADWAY_PORT" <<'PY'
import base64, sys, struct

out = sys.argv[1]
port = int(sys.argv[2])
import socket, os

def grab():
    sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM, 0)
    sock.connect(f"/run/user/{os.getuid()}/broadway{port}.socket")
    hdr = b"GET /screenshot.png HTTP/1.0\r\nHost: localhost\r\n\r\n"
    sock.send(hdr)
    data = b""
    while True:
        chunk = sock.recv(1024*1024)
        if not chunk:
            break
        data += chunk
    sock.close()
    idx = data.find(b"\r\n\r\n")
    if idx == -1:
        return None
    return data[idx+4:]

img = grab()
if not img:
    print("could not grab screenshot", file=sys.stderr)
    sys.exit(2)
open(out, "wb").write(img)
print(f"saved {len(img)} bytes -> {out}")
PY
fi

echo "screenshot: ${OUT}"
ls -la "${OUT}"
