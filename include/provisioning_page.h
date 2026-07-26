// The HTML setup page served by the device's own web server in AP mode
// (see provisioning.cpp). Deliberately plain: system fonts, no external
// requests, no build-time asset pipeline — it has to work standalone from
// flash, served to whatever browser the phone/laptop that joined the AP
// happens to have. Visual language (brass/teal, the section numbering)
// echoes docs/mockups/provisioning.html without trying to match it byte
// for byte.
#pragma once

#include <Arduino.h>

static const char kProvisioningPageHtml[] PROGMEM = R"HTML(<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Tab5 Weather Setup</title>
<style>
  :root {
    --ink: #1c2024; --sub: #52585e; --paper: #f2ede1; --panel: #e9e2d2;
    --line: rgba(28,32,36,0.16); --brass: #a8681c;
  }
  * { box-sizing: border-box; }
  body {
    margin: 0; background: var(--paper); color: var(--ink);
    font: 15px/1.5 -apple-system, "Segoe UI", Roboto, sans-serif;
    padding: 22px 18px 60px; max-width: 480px; margin: 0 auto;
  }
  h1 { font-size: 20px; margin: 4px 0 6px; }
  p.lead { color: var(--sub); font-size: 13px; margin: 0 0 22px; }
  .sec-label {
    font: 600 11px/1 -apple-system, sans-serif; letter-spacing: 0.08em;
    text-transform: uppercase; color: var(--sub); margin: 0 0 8px;
  }
  fieldset { border: none; padding: 0; margin: 0 0 22px; }
  .net {
    display: flex; align-items: center; gap: 10px; padding: 10px 12px;
    border: 1px solid var(--line); border-radius: 9px; margin-bottom: 6px; cursor: pointer;
  }
  .net.sel { border-color: var(--brass); background: var(--panel); }
  .net span.rssi { margin-left: auto; font-size: 11px; color: var(--sub); }
  label { display: block; font-size: 12px; font-weight: 600; margin: 10px 0 4px; }
  input[type=text], input[type=password] {
    width: 100%; font-size: 15px; padding: 10px 12px; border-radius: 8px;
    border: 1px solid var(--line); background: var(--panel); color: var(--ink);
  }
  .help { font-size: 11.5px; color: var(--sub); margin-top: 4px; }
  .units { display: flex; gap: 8px; }
  .units button {
    flex: 1; padding: 10px; border-radius: 8px; border: 1px solid var(--line);
    background: var(--panel); color: var(--sub); font-size: 13px;
  }
  .units button.on { background: var(--ink); color: var(--paper); border-color: var(--ink); }
  button.submit {
    width: 100%; padding: 14px; border-radius: 10px; border: none;
    background: var(--brass); color: #fff; font-size: 15px; font-weight: 700; margin-top: 8px;
  }
  button.submit:disabled { opacity: 0.6; }
  #status { text-align: center; font-size: 12.5px; color: var(--sub); margin-top: 12px; min-height: 1.4em; }
  #scanning { font-size: 12.5px; color: var(--sub); }
</style>
</head>
<body>
  <h1>Connect your display</h1>
  <p class="lead">These details are saved on the device only and used to fetch your forecast.</p>

  <form id="f">
    <fieldset>
      <div class="sec-label">1. Wi-Fi network</div>
      <div id="networks"><div id="scanning">Scanning&hellip;</div></div>
      <label for="ssidManual">Network name (if not listed above)</label>
      <input type="text" id="ssidManual" name="ssidManual" autocomplete="off" placeholder="HomeNet-5G">
      <label for="password">Network password</label>
      <input type="password" id="password" name="password" autocomplete="off">
    </fieldset>

    <fieldset>
      <div class="sec-label">2. Location</div>
      <label for="location">City or ZIP</label>
      <input type="text" id="location" name="location" placeholder="Bellevue, WA" required>
      <div class="help">No GPS on this device &mdash; resolved to coordinates once, after setup.</div>
    </fieldset>

    <fieldset>
      <div class="sec-label">3. Google Weather API key</div>
      <label for="apikey">API key</label>
      <input type="password" id="apikey" name="apikey" autocomplete="off" required>
      <div class="help">From Google Cloud Console, restricted to the Weather API.</div>
    </fieldset>

    <fieldset>
      <div class="sec-label">4. Units</div>
      <div class="units">
        <button type="button" class="on" data-units="imperial">&deg;Fahrenheit</button>
        <button type="button" data-units="metric">&deg;Celsius</button>
      </div>
    </fieldset>

    <button class="submit" type="submit" id="submitBtn">Save &amp; Connect</button>
    <div id="status"></div>
  </form>

<script>
  let selectedSsid = "";
  let units = "imperial";

  fetch('/scan').then(r => r.json()).then(list => {
    const wrap = document.getElementById('networks');
    wrap.innerHTML = '';
    if (!list.length) { wrap.innerHTML = '<div id="scanning">Network scanning isn\'t available on this display &mdash; type your network\'s name below instead.</div>'; return; }
    list.forEach((n, i) => {
      const row = document.createElement('div');
      row.className = 'net' + (i === 0 ? ' sel' : '');
      row.innerHTML = '<span>' + n.ssid + '</span><span class="rssi">' + n.rssi + ' dBm</span>';
      row.addEventListener('click', () => {
        document.querySelectorAll('.net').forEach(el => el.classList.remove('sel'));
        row.classList.add('sel');
        selectedSsid = n.ssid;
      });
      wrap.appendChild(row);
      if (i === 0) selectedSsid = n.ssid;
    });
  }).catch(() => {
    document.getElementById('networks').innerHTML = '<div id="scanning">Scan failed &mdash; reload to retry.</div>';
  });

  document.querySelectorAll('.units button').forEach(btn => {
    btn.addEventListener('click', () => {
      document.querySelectorAll('.units button').forEach(b => b.classList.remove('on'));
      btn.classList.add('on');
      units = btn.dataset.units;
    });
  });

  document.getElementById('f').addEventListener('submit', (e) => {
    e.preventDefault();
    const statusEl = document.getElementById('status');
    const submitBtn = document.getElementById('submitBtn');
    const manualSsid = document.getElementById('ssidManual').value.trim();
    const ssid = manualSsid || selectedSsid;
    if (!ssid) { statusEl.textContent = 'Pick a Wi-Fi network, or type its name, first.'; return; }

    submitBtn.disabled = true;
    statusEl.textContent = 'Saving…';

    const body = new URLSearchParams({
      ssid: ssid,
      password: document.getElementById('password').value,
      location: document.getElementById('location').value,
      apikey: document.getElementById('apikey').value,
      units: units,
    });

    fetch('/save', { method: 'POST', body })
      .then(r => r.json())
      .then(res => {
        if (res.ok) {
          statusEl.textContent = 'Saved. The display is restarting and joining your network now.';
        } else {
          statusEl.textContent = res.error || 'Something went wrong.';
          submitBtn.disabled = false;
        }
      })
      .catch(() => {
        statusEl.textContent = 'Could not reach the display.';
        submitBtn.disabled = false;
      });
  });
</script>
</body>
</html>
)HTML";
