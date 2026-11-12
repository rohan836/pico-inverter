#!/usr/bin/env python3
"""Live Web Server for 1.3-inch OLED Interface Prototyping."""

import io
import json
import sys
from http.server import HTTPServer, SimpleHTTPRequestHandler
from pathlib import Path
from urllib.parse import parse_qs, urlparse

from PIL import Image

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))

import preview_dashboard

PORT = 8080

HTML_PAGE = """<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<title>1.3" OLED Live Interface Simulator (128x64)</title>
<style>
  :root {
    --bg-base: #0a0d14;
    --card-bg: #121824;
    --border: #1e293b;
    --accent: #38bdf8;
    --text-main: #f1f5f9;
    --text-muted: #94a3b8;
  }
  * { box-sizing: border-box; margin: 0; padding: 0; }
  body {
    background: var(--bg-base);
    color: var(--text-main);
    font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, sans-serif;
    display: flex;
    flex-direction: column;
    align-items: center;
    padding: 30px 20px;
    min-height: 100vh;
  }
  h1 {
    font-size: 22px;
    font-weight: 600;
    margin-bottom: 6px;
    color: #e2e8f0;
    letter-spacing: -0.5px;
  }
  .subtitle {
    color: var(--text-muted);
    font-size: 13px;
    margin-bottom: 24px;
  }
  .display-container {
    background: #000;
    border: 12px solid #1a202c;
    border-radius: 14px;
    box-shadow: 0 20px 40px rgba(0,0,0,0.8), 0 0 30px rgba(56, 189, 248, 0.1);
    padding: 16px;
    display: inline-block;
    margin-bottom: 24px;
    position: relative;
  }
  .screen-bezel {
    border: 2px solid #2d3748;
    border-radius: 4px;
    padding: 4px;
    background: #05070a;
  }
  #oledCanvas {
    display: block;
    image-rendering: pixelated;
    image-rendering: crisp-edges;
    width: 640px;
    height: 320px;
    background: #000;
  }
  .legend-strip {
    display: flex;
    justify-content: space-between;
    font-size: 11px;
    color: #64748b;
    margin-top: 6px;
    font-family: monospace;
  }
  .control-panel {
    background: var(--card-bg);
    border: 1px solid var(--border);
    border-radius: 12px;
    padding: 20px;
    width: 100%;
    max-width: 672px;
    display: grid;
    grid-template-columns: 1fr 1fr;
    gap: 16px;
  }
  .control-group {
    display: flex;
    flex-direction: column;
    gap: 6px;
  }
  .control-group.full {
    grid-column: span 2;
  }
  label {
    font-size: 12px;
    font-weight: 500;
    color: var(--text-muted);
    display: flex;
    justify-content: space-between;
  }
  .val-badge {
    color: var(--accent);
    font-weight: 600;
    font-family: monospace;
  }
  input[type=range] {
    accent-color: var(--accent);
    cursor: pointer;
  }
  .btn-row {
    display: flex;
    gap: 12px;
    grid-column: span 2;
    margin-top: 8px;
  }
  button {
    flex: 1;
    padding: 10px;
    border: 1px solid var(--border);
    border-radius: 6px;
    font-size: 13px;
    font-weight: 600;
    cursor: pointer;
    background: #1e293b;
    color: var(--text-main);
    transition: all 0.15s ease;
  }
  button.active {
    background: #0284c7;
    border-color: #38bdf8;
    color: #fff;
    box-shadow: 0 0 12px rgba(56, 189, 248, 0.4);
  }
  .quick-presets {
    grid-column: span 2;
    display: flex;
    gap: 8px;
    font-size: 12px;
  }
  .chip {
    padding: 6px 12px;
    border-radius: 20px;
    background: #1e293b;
    border: 1px solid var(--border);
    color: #cbd5e1;
    cursor: pointer;
  }
  .chip:hover {
    background: #334155;
    color: #fff;
  }
</style>
</head>
<body>

<h1>1.3" OLED Live Interface Simulator</h1>
<p class="subtitle">Real-time bit-accurate preview of 128x64 display architecture</p>

<div class="display-container">
  <div class="screen-bezel">
    <canvas id="oledCanvas" width="128" height="64"></canvas>
  </div>
  <div class="legend-strip">
    <span>ZONE 1: FREQ (24px)</span>
    <span>ZONE 2: DUTY (18px)</span>
    <span>ZONE 3: PROGRESS / STATE</span>
  </div>
</div>

<div class="control-panel">
  <div class="control-group">
    <label>Frequency (Hz): <span class="val-badge" id="lblFreq">60 Hz</span></label>
    <input type="range" id="slFreq" min="1" max="400" value="60">
  </div>

  <div class="control-group">
    <label>Duty Cycle (%): <span class="val-badge" id="lblDuty">50 %</span></label>
    <input type="range" id="slDuty" min="1" max="100" value="50">
  </div>

  <div class="btn-row">
    <button id="btnPwm" class="active">PWM OUTPUT: ON</button>
    <button id="btnLim">LIMIT CLAMP: OFF</button>
  </div>

  <div class="quick-presets">
    <span style="align-self:center; color:#64748b;">Presets:</span>
    <span class="chip" onclick="setPreset(60, 50, true, false)">60Hz 50% Active</span>
    <span class="chip" onclick="setPreset(50, 25, true, false)">50Hz 25% Active</span>
    <span class="chip" onclick="setPreset(60, 50, false, false)">Standby (OFF)</span>
    <span class="chip" onclick="setPreset(120, 100, true, true)">120Hz 100% Clamped</span>
  </div>
</div>

<script>
const canvas = document.getElementById('oledCanvas');
const ctx = canvas.getContext('2d');
const slFreq = document.getElementById('slFreq');
const slDuty = document.getElementById('slDuty');
const lblFreq = document.getElementById('lblFreq');
const lblDuty = document.getElementById('lblDuty');
const btnPwm = document.getElementById('btnPwm');
const btnLim = document.getElementById('btnLim');

let pwmOn = true;
let limited = false;
let phase = 0;
let isFetching = false;

function setPreset(f, d, on, lim) {
  slFreq.value = f;
  slDuty.value = d;
  pwmOn = on;
  limited = lim;
  updateButtons();
  updateLabels();
}

function updateButtons() {
  btnPwm.textContent = 'PWM OUTPUT: ' + (pwmOn ? 'ON' : 'OFF');
  btnPwm.className = pwmOn ? 'active' : '';
  btnLim.textContent = 'LIMIT CLAMP: ' + (limited ? 'ACTIVE' : 'OFF');
  btnLim.className = limited ? 'active' : '';
}

function updateLabels() {
  lblFreq.textContent = slFreq.value + ' Hz';
  lblDuty.textContent = slDuty.value + ' %';
}

btnPwm.onclick = () => { pwmOn = !pwmOn; updateButtons(); };
btnLim.onclick = () => { limited = !limited; updateButtons(); };
slFreq.oninput = updateLabels;
slDuty.oninput = updateLabels;

async function fetchFrame() {
  if (isFetching) return;
  isFetching = true;
  const f = slFreq.value;
  const d = slDuty.value;
  const on = pwmOn ? 1 : 0;
  const lim = limited ? 1 : 0;
  const url = `/api/frame?freq=${f}&duty=${d}&on=${on}&lim=${lim}&phase=${phase}`;
  
  const img = new Image();
  img.onload = () => {
    ctx.drawImage(img, 0, 0);
    isFetching = false;
  };
  img.onerror = () => { isFetching = false; };
  img.src = url;
}

function loop() {
  if (pwmOn) {
    phase = (phase + 1) % 500;
  }
  fetchFrame();
  setTimeout(() => requestAnimationFrame(loop), 80);
}

updateButtons();
updateLabels();
loop();
</script>
</body>
</html>
"""

class OLEDServer(SimpleHTTPRequestHandler):
    def do_GET(self):
        parsed = urlparse(self.path)
        if parsed.path == "/":
            self.send_response(200)
            self.send_header("Content-Type", "text/html; charset=utf-8")
            self.end_headers()
            self.wfile.write(HTML_PAGE.encode("utf-8"))
            return

        if parsed.path == "/api/frame":
            qs = parse_qs(parsed.query)
            freq = int(qs.get("freq", ["60"])[0])
            duty = int(qs.get("duty", ["50"])[0])
            on = qs.get("on", ["1"])[0] == "1"
            lim = qs.get("lim", ["0"])[0] == "1"
            phase = int(qs.get("phase", ["0"])[0])

            frame = preview_dashboard.render(freq, duty, on, lim, phase)

            # Generate crisp 128x64 RGBA image
            img = Image.new("RGBA", (128, 64), (0, 0, 0, 255))
            for y in range(64):
                for x in range(128):
                    if frame.px[y][x]:
                        img.putpixel((x, y), (140, 235, 255, 255))

            buf = io.BytesIO()
            img.save(buf, format="PNG")
            data = buf.getvalue()

            self.send_response(200)
            self.send_header("Content-Type", "image/png")
            self.send_header("Content-Length", str(len(data)))
            self.send_header("Cache-Control", "no-cache, no-store, must-revalidate")
            self.end_headers()
            self.wfile.write(data)
            return

        super().do_GET()

def main():
    server = HTTPServer(("127.0.0.1", PORT), OLEDServer)
    print(f"OLED Live Preview Server running on http://127.0.0.1:{PORT}")
    server.serve_forever()

if __name__ == "__main__":
    main()
