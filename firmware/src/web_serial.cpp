/**
 * Web Serial Monitor — Implementation
 *
 * Adapted from Field Compass web serial monitor for DCC.
 * HTML stored as PROGMEM to save ~3KB SRAM on ESP32-S3.
 */

#include "web_serial.h"
#include "logger.h"
#include "sd_manager.h"
#include "wifi_manager.h"
#include <WebServer.h>
#include <ESPmDNS.h>

static WebServer s_server(80);
static bool      s_started  = false;
static uint16_t  s_webReadPos = 0;   /* ring buffer read position for web client */

/* ═══════════════════════════════════════════════════════════
 *  HTML Pages (PROGMEM — stored in flash, not SRAM)
 * ═══════════════════════════════════════════════════════════ */

/* --- Root landing page --- */
static const char PAGE_ROOT[] PROGMEM = R"rawhtml(<!DOCTYPE html><html><head>
<title>DCC</title><meta name='color-scheme' content='dark light'>
<style>
:root{--bg:#1a1a1a;--fg:#e0e0e0;--link:#00ffff;--header:#00ffff;}
@media(prefers-color-scheme:light){:root{--bg:#f5f5f5;--fg:#222;--link:#006666;--header:#008888;}}
body{font-family:sans-serif;margin:0;padding:20px;background:var(--bg);color:var(--fg);}
a{color:var(--link);font-size:18px;}
h2{color:var(--header);margin:0 0 15px 0;}
.links{display:flex;flex-direction:column;gap:12px;}
</style></head><body>
<h2>Desk Command Center</h2>
<div class='links'>
<a href='/serial'>Serial Monitor</a>
<a href='/logs'>Log Files</a>
</div>
</body></html>)rawhtml";

/* --- Serial monitor page --- */
static const char PAGE_SERIAL[] PROGMEM = R"rawhtml(<!DOCTYPE html><html><head>
<title>DCC Serial</title><meta name='color-scheme' content='dark light'>
<style>
:root{--bg:#1a1a1a;--fg:#e0e0e0;--log-bg:#000;--log-fg:#00ff00;
--btn-bg:#2a2a2a;--btn-fg:#00ff00;--btn-border:#444;--link:#00ffff;
--header:#00ffff;--warn:#ffcc00;--error:#ff4444;}
@media(prefers-color-scheme:light){:root{--bg:#f5f5f5;--fg:#222;
--log-bg:#222;--log-fg:#00cc00;--btn-bg:#ddd;--btn-fg:#006600;
--btn-border:#999;--link:#006666;--header:#008888;--warn:#cc8800;--error:#cc0000;}}
body{font-family:sans-serif;margin:0;padding:10px;background:var(--bg);color:var(--fg);}
.log{background:var(--log-bg);color:var(--log-fg);font-family:monospace;padding:10px;
font-size:12px;height:500px;overflow-y:auto;border-radius:8px;white-space:pre-wrap;}
.log .w{color:var(--warn);}.log .e{color:var(--error);}
.controls{margin:10px 0;display:flex;align-items:center;gap:8px;}
button{padding:8px 16px;background:var(--btn-bg);color:var(--btn-fg);
border:1px solid var(--btn-border);border-radius:4px;cursor:pointer;}
button:hover{opacity:0.8;}
.badge{display:inline-block;padding:4px 12px;border-radius:12px;font-size:12px;
background:#333;color:#aaa;cursor:pointer;user-select:none;}
.badge.live{background:#004400;color:#00ff00;}
a{color:var(--link);}
</style></head><body>
<h2 style='color:var(--header);margin:0 0 10px 0;'>DCC Serial Monitor</h2>
<div class='log' id='log'></div>
<div class='controls'>
<button onclick='copyLog()'>Copy</button>
<button onclick='clearLog()'>Clear</button>
<span class='badge live' id='scBtn' onclick='toggleScroll()'>Live &#x25BC;</span>
<a href='/logs'>Logs</a>
<a href='/'>Home</a>
</div>
<script>
var log=document.getElementById('log');
var scBtn=document.getElementById('scBtn');
var autoScroll=true;
function cls(l){
if(/ERROR|FAILED/i.test(l))return 'e';
if(/WARN|NOT FOUND/i.test(l))return 'w';
return '';
}
function appendLines(t){
var lines=t.split('\n');
for(var i=0;i<lines.length;i++){
if(i===lines.length-1&&lines[i]==='')break;
var s=document.createElement('span');
var c=cls(lines[i]);
if(c)s.className=c;
s.textContent=lines[i]+(i<lines.length-1?'\n':'');
log.appendChild(s);
}
}
function nearBottom(){return log.scrollHeight-log.scrollTop-log.clientHeight<30;}
log.addEventListener('scroll',function(){
if(nearBottom()){if(!autoScroll){autoScroll=true;updBtn();}}
else{if(autoScroll){autoScroll=false;updBtn();}}
});
function updBtn(){
if(autoScroll){scBtn.textContent='Live \u25BC';scBtn.className='badge live';}
else{scBtn.textContent='Paused \u25B6';scBtn.className='badge';}
}
function toggleScroll(){
autoScroll=!autoScroll;
if(autoScroll)log.scrollTop=log.scrollHeight;
updBtn();
}
async function poll(){
try{
var r=await fetch('/serial-data');
var t=await r.text();
if(t.length>0){appendLines(t);if(autoScroll)log.scrollTop=log.scrollHeight;}
}catch(e){}
}
function clearLog(){log.innerHTML='';autoScroll=true;updBtn();}
function copyLog(){
try{
navigator.clipboard.writeText(log.textContent)
.then(function(){alert('Copied!');})
.catch(function(){copyFallback();});
}catch(e){copyFallback();}
}
function copyFallback(){
var ta=document.createElement('textarea');
ta.value=log.textContent;
ta.style.position='fixed';ta.style.left='-9999px';
document.body.appendChild(ta);ta.select();
try{document.execCommand('copy');alert('Copied!');}catch(e){alert('Copy failed');}
document.body.removeChild(ta);
}
setInterval(poll,100);
</script>
</body></html>)rawhtml";

/* ═══════════════════════════════════════════════════════════
 *  Route Handlers
 * ═══════════════════════════════════════════════════════════ */

static void handleRoot() {
    s_server.send_P(200, "text/html", PAGE_ROOT);
}

static void handleSerial() {
    s_server.send_P(200, "text/html", PAGE_SERIAL);
}

/**
 * GET /serial-data — return incremental text from ring buffer.
 * Tracks s_webReadPos across calls for a single-client model.
 */
static void handleSerialData() {
    String out = Logger::ringReadIncremental(s_webReadPos);
    s_server.send(200, "text/plain", out);
}

/**
 * GET /logs — list SD log files with sizes + download links.
 */
static void handleLogs() {
    /* Flush pending buffer before listing */
    Logger::flushSD();

    String html = "<!DOCTYPE html><html><head><title>DCC Logs</title>";
    html += "<meta name='color-scheme' content='dark light'>";
    html += "<style>";
    html += ":root{--bg:#1a1a1a;--fg:#e0e0e0;--link:#00ffff;--header:#00ffff;}";
    html += "@media(prefers-color-scheme:light){:root{--bg:#f5f5f5;--fg:#222;"
            "--link:#006666;--header:#008888;}}";
    html += "body{font-family:sans-serif;margin:0;padding:20px;"
            "background:var(--bg);color:var(--fg);}";
    html += "table{border-collapse:collapse;width:100%;margin-top:10px;}";
    html += "th,td{text-align:left;padding:8px;border-bottom:1px solid #444;}";
    html += "a{color:var(--link);}";
    html += "h2{color:var(--header);margin:0 0 10px 0;}";
    html += "</style></head><body>";
    html += "<h2>Serial Logs</h2>";

    if (!SDManager::isMounted()) {
        html += "<p>SD card not available.</p>";
    } else if (!SDManager::exists("/logs")) {
        html += "<p>No log files yet.</p>";
    } else {
        html += "<table><tr><th>File</th><th>Size</th><th>Action</th></tr>";

        /* Direct SD iteration (can't use SDManager::listDir here because
           its plain function pointer callback can't capture `html`). */
        File dir = SD.open("/logs");
        if (dir && dir.isDirectory()) {
            File entry = dir.openNextFile();
            while (entry) {
                if (!entry.isDirectory()) {
                    const char* name = entry.name();
                    size_t sz = entry.size();
                    html += "<tr><td>";
                    html += name;
                    html += "</td><td>";
                    if (sz >= 1024) {
                        html += String(sz / 1024.0, 1) + " KB";
                    } else {
                        html += String((unsigned long)sz) + " B";
                    }
                    html += "</td><td><a href='/logs/download?file=";
                    html += name;
                    html += "'>Download</a></td></tr>";
                }
                entry.close();
                entry = dir.openNextFile();
            }
            dir.close();
        }

        html += "</table>";
    }

    html += "<p style='margin-top:15px;'><a href='/serial'>Serial Monitor</a>"
            " &nbsp; <a href='/'>Home</a></p>";
    html += "</body></html>";
    s_server.send(200, "text/html", html);
}

/**
 * GET /logs/download?file=<name> — stream a log file.
 * Path traversal prevention: only allow [a-z0-9_.-] in filename.
 */
static void handleLogDownload() {
    if (!s_server.hasArg("file")) {
        s_server.send(400, "text/plain", "Missing file parameter");
        return;
    }

    String filename = s_server.arg("file");

    /* Security: whitelist characters to prevent path traversal */
    for (unsigned int i = 0; i < filename.length(); i++) {
        char c = filename[i];
        if (!isalnum(c) && c != '_' && c != '.' && c != '-') {
            s_server.send(400, "text/plain", "Invalid filename");
            return;
        }
    }

    /* Flush active log buffer if this is the current session file */
    Logger::flushSD();

    String fullPath = "/logs/" + filename;
    File f = SDManager::openRead(fullPath.c_str());
    if (!f) {
        s_server.send(404, "text/plain", "File not found");
        return;
    }

    String disposition = "attachment; filename=" + filename;
    s_server.sendHeader("Content-Disposition", disposition);
    s_server.streamFile(f, "text/plain");
    f.close();
}

/* ═══════════════════════════════════════════════════════════
 *  Public API
 * ═══════════════════════════════════════════════════════════ */

void WebSerial::init() {
    if (s_started) return;
    if (WifiManager::state() != WifiState::CONNECTED) return;

    LOG_INFO("WEB: Starting web server...");

    /* mDNS: access via http://dcc.local/ */
    if (MDNS.begin("dcc")) {
        LOG_INFO("WEB: mDNS OK (dcc.local)");
    }

    /* Register routes */
    s_server.on("/",              HTTP_GET, handleRoot);
    s_server.on("/serial",        HTTP_GET, handleSerial);
    s_server.on("/serial-data",   HTTP_GET, handleSerialData);
    s_server.on("/logs",          HTTP_GET, handleLogs);
    s_server.on("/logs/download", HTTP_GET, handleLogDownload);

    s_server.begin();
    s_started = true;

    /* Initialize web client read position to current ring head
       so it doesn't replay the entire buffer on first connect */
    s_webReadPos = Logger::ringHead();

    LOG_INFO("WEB: Server OK — http://%s/",
             WiFi.localIP().toString().c_str());
}

void WebSerial::handleClient() {
    if (!s_started) return;
    s_server.handleClient();
}

bool WebSerial::isRunning() {
    return s_started;
}
