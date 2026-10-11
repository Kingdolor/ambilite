/* Ambilite – Fernbedienungs-Oberflaeche (webOS Web-App) */
(function () {
  'use strict';

  var SERVICE = 'luna://com.ambilite.tv.service/';
  var HBCHANNEL = 'luna://org.webosbrew.hbchannel.service/exec';
  var HB_ELEVATE = '/media/developer/apps/usr/palm/services/org.webosbrew.hbchannel.service/elevate-service';

  var KEY = { LEFT: 37, UP: 38, RIGHT: 39, DOWN: 40, ENTER: 13, BACK: 461, BACK_PC: 8 };
  var MODES = [
    { id: 'video', label: 'Bild (Ambilight)' },
    { id: 'color', label: 'Feste Farbe' },
    { id: 'effect', label: 'Effekt' }
  ];
  var COLORS = [
    { c: 0xFF8A00, label: 'Orange' }, { c: 0xFFB45A, label: 'Warmweiß' }, { c: 0xFFFFFF, label: 'Weiß' },
    { c: 0xFF0000, label: 'Rot' }, { c: 0xFF2A6D, label: 'Pink' }, { c: 0x9B30FF, label: 'Lila' },
    { c: 0x0040FF, label: 'Blau' }, { c: 0x00D0FF, label: 'Cyan' }, { c: 0x00FF40, label: 'Grün' }
  ];
  var EFFECTS = [
    { id: 'rainbow', label: 'Regenbogen' }, { id: 'breathe', label: 'Atmen' },
    { id: 'candle', label: 'Kerze' }, { id: 'aurora', label: 'Polarlicht' }
  ];
  var SMOOTH = ['Direkt', 'Klassisch', 'Adaptiv'];

  var settings = null;
  var status = {};
  var focus = 0;
  var elevating = false;
  var bridges = [];
  var update = { state: 'idle', release: null, text: 'OK = nach Updates suchen' };

  // ---------------------------------------------------------------- luna
  function call(uri, params, cb) {
    var b;
    try {
      b = new PalmServiceBridge(); // eslint-disable-line no-undef
    } catch (e) {
      msg('Kein webOS – nur Vorschau');
      if (cb) cb({ returnValue: false });
      return;
    }
    bridges.push(b);
    b.onservicecallback = function (m) {
      var i = bridges.indexOf(b);
      if (i >= 0) bridges.splice(i, 1);
      var r;
      try { r = JSON.parse(m); } catch (e) { r = { returnValue: false }; }
      if (cb) cb(r);
    };
    b.call(uri, JSON.stringify(params || {}));
  }
  function svc(method, params, cb) { call(SERVICE + method, params, cb); }
  function hbExec(cmd, cb) { call(HBCHANNEL, { command: cmd }, cb); }

  function msg(t) {
    var el = document.getElementById('msg');
    el.textContent = t || '';
    if (t) {
      clearTimeout(msg.t);
      msg.t = setTimeout(function () { el.textContent = ''; }, 6000);
    }
  }

  // ---------------------------------------------------------------- helpers
  function onOff(v) { return v ? '<span class="on">An</span>' : '<span class="offc">Aus</span>'; }
  function clamp(v, a, b) { return Math.max(a, Math.min(b, v)); }
  function round1(v) { return Math.round(v * 10) / 10; }
  function idxOf(list, key, val) {
    for (var i = 0; i < list.length; i++) if (list[i][key] === val) return i;
    return 0;
  }
  function hdrNow() { return !!(settings && settings.hdr_auto && status.hdrActive); }
  function pk(k) { return hdrNow() ? 'hdr_' + k : k; } // edit the active profile
  function save(patch) {
    for (var k in patch) settings[k] = patch[k];
    render();
    svc('setSettings', patch, function (r) { if (!r.returnValue) msg('Speichern fehlgeschlagen'); });
  }
  function swatch(c) {
    var hex = ('000000' + c.toString(16)).slice(-6);
    return '<span class="sw" style="background:#' + hex + '"></span>';
  }
  function cmpVer(a, b) {
    var x = String(a).replace(/^v/, '').split('.'), y = String(b).replace(/^v/, '').split('.');
    for (var i = 0; i < 3; i++) {
      var d = (parseInt(x[i], 10) || 0) - (parseInt(y[i], 10) || 0);
      if (d) return d;
    }
    return 0;
  }

  // ---------------------------------------------------------------- menu model
  var allItems = [
    {
      label: function () { return 'Ambilight'; },
      value: function () { return onOff(settings.enabled); },
      ok: function () {
        svc('toggle', {}, function () { refreshSettings(); setTimeout(refreshStatus, 400); });
        settings.enabled = !settings.enabled; render();
      },
      lr: function () { this.ok(); }
    },
    {
      label: function () { return 'Modus'; },
      value: function () { return MODES[idxOf(MODES, 'id', settings.mode)].label; },
      lr: function (d) { save({ mode: MODES[(idxOf(MODES, 'id', settings.mode) + d + 3) % 3].id }); }
    },
    {
      show: function () { return settings.mode === 'color'; },
      label: function () { return 'Farbe'; },
      value: function () {
        var i = idxOf(COLORS, 'c', settings.color);
        return swatch(settings.color) + (COLORS[i].c === settings.color ? COLORS[i].label : 'Eigene');
      },
      lr: function (d) {
        var i = idxOf(COLORS, 'c', settings.color);
        save({ color: COLORS[(i + d + COLORS.length) % COLORS.length].c });
      }
    },
    {
      show: function () { return settings.mode === 'effect'; },
      label: function () { return 'Effekt'; },
      value: function () { return EFFECTS[idxOf(EFFECTS, 'id', settings.effect)].label; },
      lr: function (d) {
        var i = idxOf(EFFECTS, 'id', settings.effect);
        save({ effect: EFFECTS[(i + d + EFFECTS.length) % EFFECTS.length].id });
      }
    },
    {
      show: function () { return settings.mode === 'effect'; },
      label: function () { return 'Tempo'; },
      value: function () { return settings.effect_speed + ' / 10'; },
      lr: function (d) { save({ effect_speed: clamp(settings.effect_speed + d, 1, 10) }); }
    },
    {
      label: function () { return 'Helligkeit' + (hdrNow() ? ' (HDR)' : ''); },
      value: function () { return settings[pk('brightness')] + ' %'; },
      lr: function (d) { var p = {}; p[pk('brightness')] = clamp(settings[pk('brightness')] + 5 * d, 5, 100); save(p); }
    },
    {
      show: function () { return settings.mode === 'video'; },
      label: function () { return 'Farbsättigung' + (hdrNow() ? ' (HDR)' : ''); },
      value: function () { return settings[pk('saturation')].toFixed(1) + '×'; },
      lr: function (d) { var p = {}; p[pk('saturation')] = round1(clamp(settings[pk('saturation')] + 0.1 * d, 0, 3)); save(p); }
    },
    {
      show: function () { return settings.mode === 'video'; },
      label: function () { return 'Gamma' + (hdrNow() ? ' (HDR)' : ''); },
      value: function () { return settings[pk('gamma')].toFixed(1); },
      lr: function (d) { var p = {}; p[pk('gamma')] = round1(clamp(settings[pk('gamma')] + 0.1 * d, 0.5, 3)); save(p); }
    },
    {
      show: function () { return settings.mode === 'video'; },
      label: function () { return 'HDR-Profil automatisch'; },
      value: function () { return onOff(settings.hdr_auto); },
      ok: function () { save({ hdr_auto: !settings.hdr_auto }); },
      lr: function () { this.ok(); }
    },
    {
      show: function () { return settings.mode === 'video'; },
      label: function () { return 'Glättung'; },
      value: function () { return SMOOTH[settings.smoothing_mode]; },
      lr: function (d) { save({ smoothing_mode: (settings.smoothing_mode + d + 3) % 3 }); }
    },
    {
      show: function () { return settings.mode === 'video' && settings.smoothing_mode === 2; },
      label: function () { return 'Ruhe bei kleinen Änderungen'; },
      value: function () { return settings.adaptive_strength + ' %'; },
      lr: function (d) { save({ adaptive_strength: clamp(settings.adaptive_strength + 10 * d, 0, 100) }); }
    },
    {
      show: function () { return settings.mode === 'video' && settings.smoothing_mode === 2; },
      label: function () { return 'Nachleuchten beim Abdunkeln'; },
      value: function () { return settings.release_ms + ' ms'; },
      lr: function (d) { save({ release_ms: clamp(settings.release_ms + 50 * d, 0, 600) }); }
    },
    {
      show: function () { return settings.mode === 'video' && settings.smoothing_mode === 1; },
      label: function () { return 'Glättungszeit'; },
      value: function () { return settings.smoothing_ms + ' ms'; },
      lr: function (d) { save({ smoothing_ms: clamp(settings.smoothing_ms + 20 * d, 0, 500) }); }
    },
    {
      show: function () { return settings.mode === 'video'; },
      label: function () { return 'Nachbar-LEDs mischen'; },
      value: function () { return settings.spatial + ' %'; },
      lr: function (d) { save({ spatial: clamp(settings.spatial + 10 * d, 0, 100) }); }
    },
    {
      show: function () { return settings.mode === 'video'; },
      label: function () { return 'Schwarzbalken erkennen'; },
      value: function () { return onOff(settings.blackborder); },
      ok: function () { save({ blackborder: !settings.blackborder }); },
      lr: function () { this.ok(); }
    },
    {
      label: function () { return 'Autostart beim Einschalten'; },
      value: function () { return onOff(settings.autostart); },
      ok: function () { save({ autostart: !settings.autostart }); },
      lr: function () { this.ok(); }
    },
    {
      label: function () { return 'Fernbedienungs-Taste'; },
      value: function () {
        if (status.hotkeyLearning) return 'Jetzt Taste drücken …';
        return settings.hotkey ? 'Code ' + settings.hotkey + ' (◀ löschen)' : 'OK = Taste lernen';
      },
      ok: function () {
        svc('learnHotkey', {}, function () { msg('Drücke jetzt die Taste, die das Ambilight schalten soll'); refreshStatus(); });
      },
      left: function () { save({ hotkey: 0 }); msg('Taste entfernt'); }
    },
    {
      label: function () { return 'iPhone-Code nötig'; },
      value: function () {
        return onOff(settings.api_require_key) + (status.apiPort ? ' · Port ' + status.apiPort : ' · <span class="offc">aus</span>');
      },
      ok: function () { save({ api_require_key: !settings.api_require_key }); },
      lr: function () { this.ok(); }
    },
    {
      show: function () { return !!settings.api_require_key; },
      label: function () { return 'iPhone-Code'; },
      value: function () { return '<b class="code">' + (settings.api_key || '–') + '</b> · OK = neuer Code'; },
      ok: function () {
        var code = String(Math.floor(100000 + Math.random() * 900000));
        svc('setSettings', { api_key: code }, function () { settings.api_key = code; render(); msg('Neuer Code – im iPhone neu koppeln'); });
      }
    },
    {
      label: function () { return 'Update'; },
      value: function () { return update.text; },
      ok: function () { updateAction(); }
    },
    {
      label: function () { return 'LED-Test'; },
      value: function () { return 'OK = Rot · Grün · Blau · Weiß · Lauflicht'; },
      ok: function () {
        if (status.isRunning) { msg('Für den Test zuerst Ambilight ausschalten'); return; }
        svc('testPattern', {}, function (r) { msg(r.returnValue ? 'Test läuft …' : 'Test nicht möglich (LED-Ausgang belegt?)'); });
      }
    },
    {
      label: function () { return 'PicCap/HyperHDR beenden'; },
      value: function () { return 'OK = Prozesse stoppen'; },
      ok: function () {
        hbExec('killall hyperion-webos hyperhdr 2>/dev/null; true', function (r) {
          msg(r.returnValue ? 'Alte Dienste beendet' : 'Homebrew Channel nicht erreichbar');
        });
      }
    }
  ];

  function items() {
    var out = [];
    for (var i = 0; i < allItems.length; i++) if (!allItems[i].show || allItems[i].show()) out.push(allItems[i]);
    return out;
  }

  // ---------------------------------------------------------------- updates (GitHub releases)
  function findIpk(rel) {
    var assets = rel.assets || [];
    for (var i = 0; i < assets.length; i++) {
      if (/\.ipk$/.test(assets[i].name)) {
        var sha = null;
        if (assets[i].digest && /^sha256:/.test(assets[i].digest)) sha = assets[i].digest.slice(7);
        if (!sha && rel.body) {
          var m = /sha256[:\s`]*([a-f0-9]{64})/i.exec(rel.body);
          if (m) sha = m[1].toLowerCase();
        }
        return { url: assets[i].browser_download_url, sha256: sha };
      }
    }
    return null;
  }

  function updateAction() {
    if (update.state === 'available' && update.release) {
      var ipk = findIpk(update.release);
      if (!ipk || !ipk.sha256) { msg('Release ohne IPK/Prüfsumme'); return; }
      update.state = 'installing';
      update.text = 'Installiere ' + update.release.tag_name + ' …';
      render();
      svc('install', { url: ipk.url, sha256: ipk.sha256 }, function (r) {
        if (!r.returnValue) { update.state = 'idle'; update.text = 'Start fehlgeschlagen'; render(); }
        else msg('Update läuft – die App schließt sich gleich, danach neu öffnen');
      });
      return;
    }
    if (!settings.github_repo) { update.text = 'Kein GitHub-Repository eingestellt'; render(); return; }
    update.text = 'Suche …';
    render();
    var x = new XMLHttpRequest();
    x.open('GET', 'https://api.github.com/repos/' + settings.github_repo + '/releases?per_page=10');
    x.onload = function () {
      var list = [];
      try { list = JSON.parse(x.responseText); } catch (e) { list = []; }
      var best = null;
      for (var i = 0; i < list.length; i++) {
        if (list[i].draft || list[i].prerelease || !findIpk(list[i])) continue;
        if (!best || cmpVer(list[i].tag_name, best.tag_name) > 0) best = list[i];
      }
      if (best && cmpVer(best.tag_name, status.version) > 0) {
        update.state = 'available';
        update.release = best;
        update.text = '<span class="on">' + best.tag_name + ' verfügbar</span> – OK = installieren';
      } else {
        update.state = 'idle';
        update.text = 'Aktuell (' + (status.version || '?') + ') · OK = erneut suchen';
      }
      render();
    };
    x.onerror = function () { update.text = 'GitHub nicht erreichbar'; render(); };
    x.send();
  }

  // ---------------------------------------------------------------- rendering
  function render() {
    if (!settings) return;
    var list = items();
    if (focus >= list.length) focus = list.length - 1;
    var ul = document.getElementById('menu');
    var html = '';
    for (var i = 0; i < list.length; i++) {
      var it = list[i];
      var hasLR = !!(it.lr || it.left);
      html += '<li data-i="' + i + '" class="' + (i === focus ? 'focus' : '') + '">' +
        '<span>' + it.label() + '</span><span class="val">' +
        (hasLR ? '<span class="arrow" data-a="l">◀</span>' : '') +
        '<span>' + it.value() + '</span>' +
        (hasLR && it.lr ? '<span class="arrow" data-a="r">▶</span>' : '') +
        '</span></li>';
    }
    ul.innerHTML = html;
    var f = ul.querySelector('li.focus');
    if (f && f.scrollIntoView) f.scrollIntoView({ block: 'nearest' });
    document.getElementById('glow').className = settings.enabled ? 'glow' : 'glow off';
  }

  function renderStatus() {
    function set(id, t) { document.getElementById(id).textContent = t; }
    set('version', status.version ? 'v' + status.version : '');
    var svcTxt = 'nicht erreichbar';
    if (status.returnValue) {
      svcTxt = status.isRunning ? (status.videoActive ? 'läuft' : 'Lichtmodus') : (status.powerPaused ? 'Standby' : 'gestoppt');
    }
    set('stService', svcTxt);
    set('stRoot', status.elevated ? 'ja' : (elevating ? 'wird eingerichtet …' : 'nein'));
    var be = [];
    if (status.videoBackend) be.push(status.videoBackend.replace('lib', '').replace('_backend.so', ''));
    if (status.uiBackend) be.push(status.uiBackend.replace('lib', '').replace('_backend.so', ''));
    set('stBackend', be.length ? be.join(' + ') : '–');
    set('stFps', status.videoActive ? (status.framerate || 0).toFixed(0) : '–');
    set('stProc', status.videoActive ? Math.round(status.procUs || 0) + ' µs' : '–');
    set('stSerial', status.serialOpen ? (status.serialPath + ' · ' + status.ledCount + ' LEDs') :
      (status.isRunning ? 'nicht verbunden' : (status.ledCount ? status.ledCount + ' LEDs' : '–')));
    set('stProfile', (status.profile === 'hdr' ? 'HDR' : 'SDR') + (status.hdrType ? ' (' + status.hdrType + ')' : ''));
    set('stBorder', status.videoActive ? (status.borderTop || status.borderLeft ?
      (status.borderTop + ' / ' + status.borderLeft + ' px') : 'keine') : '–');
    if (status.updateState === 'running' && update.state !== 'installing') {
      update.state = 'installing'; update.text = 'Update läuft …'; render();
    } else if (status.updateState === 'failed' && update.state === 'installing') {
      update.state = 'idle'; update.text = '<span class="offc">Update fehlgeschlagen</span> – OK = erneut suchen'; render();
    } else if (status.updateState === 'done' && update.state === 'installing') {
      update.state = 'idle'; update.text = 'Aktualisiert auf ' + status.version; render();
    }
  }

  // ---------------------------------------------------------------- service state
  function refreshSettings() {
    svc('getSettings', {}, function (r) {
      if (r.returnValue) { settings = r; render(); }
    });
  }

  var lastProfile = null;
  function refreshStatus() {
    svc('status', {}, function (r) {
      status = r || {};
      if (r.returnValue && !r.elevated && !elevating) elevate();
      if (r.returnValue && r.elevated) elevating = false;
      if (settings && typeof r.hotkey === 'number' && r.hotkey !== settings.hotkey && !r.hotkeyLearning) {
        settings.hotkey = r.hotkey;
      }
      if (r.returnValue && !settings) refreshSettings();
      renderStatus();
      if (settings && (status.hotkeyLearning || status.profile !== lastProfile)) render();
      lastProfile = status.profile;
    });
  }

  function elevate() {
    elevating = true;
    msg('Dienst wird mit Root-Rechten eingerichtet …');
    hbExec(HB_ELEVATE + ' com.ambilite.tv.service; killall ambilite 2>/dev/null; true', function (r) {
      if (!r.returnValue) {
        msg('Root fehlt: Homebrew Channel nicht erreichbar oder TV nicht gerootet');
        elevating = false;
      } else {
        setTimeout(function () { refreshSettings(); refreshStatus(); }, 1500);
      }
    });
  }

  // ---------------------------------------------------------------- input
  function act(it, d) {
    if (d < 0 && it.left) it.left();
    else if (it.lr) it.lr(d);
  }

  document.addEventListener('keydown', function (e) {
    if (!settings) return;
    var list = items();
    var it = list[focus];
    switch (e.keyCode) {
      case KEY.UP: focus = (focus + list.length - 1) % list.length; render(); break;
      case KEY.DOWN: focus = (focus + 1) % list.length; render(); break;
      case KEY.LEFT: act(it, -1); break;
      case KEY.RIGHT: act(it, 1); break;
      case KEY.ENTER: if (it.ok) it.ok(); else act(it, 1); break;
      case KEY.BACK: case KEY.BACK_PC: window.close(); break;
      default: return;
    }
    e.preventDefault();
  });

  // Magic Remote pointer
  document.getElementById('menu').addEventListener('click', function (e) {
    var li = e.target.closest('li');
    if (!li) return;
    focus = parseInt(li.getAttribute('data-i'), 10);
    var it = items()[focus];
    var a = e.target.getAttribute('data-a');
    if (a === 'l') act(it, -1);
    else if (a === 'r') act(it, 1);
    else if (it.ok) it.ok();
    else act(it, 1);
    render();
  });
  document.getElementById('menu').addEventListener('mouseover', function (e) {
    var li = e.target.closest('li');
    if (!li) return;
    var i = parseInt(li.getAttribute('data-i'), 10);
    if (i !== focus) { focus = i; render(); }
  });

  refreshStatus();
  setInterval(refreshStatus, 1500);
}());
