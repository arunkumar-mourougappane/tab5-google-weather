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
    --line: rgba(28,32,36,0.16); --brass: #a8681c; --teal: #2c655f;
  }
  * { box-sizing: border-box; }
  body {
    margin: 0; background: var(--paper); color: var(--ink);
    font: 15px/1.5 -apple-system, "Segoe UI", Roboto, sans-serif;
    padding: 22px 18px 60px; max-width: 480px; margin: 0 auto;
  }
  .mark {
    display: flex; align-items: center; gap: 8px; font: 600 11px/1 -apple-system, sans-serif;
    letter-spacing: 0.1em; text-transform: uppercase; color: var(--brass); margin-bottom: 8px;
  }
  h1 { font-size: 20px; margin: 4px 0 6px; }
  p.lead { color: var(--sub); font-size: 13px; margin: 0 0 22px; }
  .sec-label {
    font: 600 11px/1 -apple-system, sans-serif; letter-spacing: 0.08em;
    text-transform: uppercase; color: var(--sub); margin: 0 0 8px;
  }
  fieldset { border: none; padding: 0; margin: 0 0 22px; }
  label { display: block; font-size: 12px; font-weight: 600; margin: 10px 0 4px; }
  .input-wrap { position: relative; display: flex; align-items: center; }
  input[type=text], input[type=password] {
    width: 100%; font-size: 15px; padding: 10px 38px 10px 12px; border-radius: 8px;
    border: 1px solid var(--line); background: var(--panel); color: var(--ink);
  }
  .eye {
    position: absolute; right: 10px; width: 18px; height: 18px; padding: 0; border: none;
    background: none; color: var(--sub); cursor: pointer;
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
  .privacy { margin-top: 14px; font-size: 10.5px; color: var(--sub); text-align: center; line-height: 1.6; }
</style>
</head>
<body>
  <div class="mark">
    <svg width="14" height="14" viewBox="0 0 24 24" fill="none"><rect x="2" y="4" width="20" height="14" rx="2" stroke="currentColor" stroke-width="1.8"/><line x1="8" y1="21" x2="16" y2="21" stroke="currentColor" stroke-width="1.8" stroke-linecap="round"/></svg>
    Tab5 Weather Setup
  </div>
  <h1>Connect your display</h1>
  <p class="lead">These details are saved on the device only and used to fetch your forecast &mdash; nothing is sent anywhere else.</p>

  <form id="f">
    <fieldset>
      <div class="sec-label">1. Wi-Fi network</div>
      <label for="ssid">Network name</label>
      <input type="text" id="ssid" name="ssid" autocomplete="off" placeholder="HomeNet-5G" required>
      <label for="password">Network password</label>
      <div class="input-wrap">
        <input type="password" id="password" name="password" autocomplete="off">
        <button type="button" class="eye" data-toggle="password" aria-label="Show password">&#128065;</button>
      </div>
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
      <div class="input-wrap">
        <input type="password" id="apikey" name="apikey" autocomplete="off" required>
        <button type="button" class="eye" data-toggle="apikey" aria-label="Show API key">&#128065;</button>
      </div>
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
    <div class="privacy">Nothing leaves this network except a request to weather.googleapis.com<br>once setup is complete.</div>
  </form>

<script>
  let units = "imperial";

  document.querySelectorAll('.eye').forEach(btn => {
    btn.addEventListener('click', () => {
      const input = document.getElementById(btn.dataset.toggle);
      input.type = input.type === 'password' ? 'text' : 'password';
    });
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
    const ssid = document.getElementById('ssid').value.trim();
    if (!ssid) { statusEl.textContent = 'Enter your Wi-Fi network name first.'; return; }

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
