// Web mirror: a read-only web page showing exactly what is on the TFT.
//
//   GET /            small HTML page that reloads the picture about once a second
//   GET /screen.bmp  the screen, read back from the display controller, as a 24-bit BMP
//   GET /status      plain text: uptime, free heap, capture count and timing
//
// The picture is read back with TFT_eSPI::readRect (this needs TFT_MISO and SPI_READ_FREQUENCY in
// platformio.ini), 8 rows at a time, so only about 13 KB of RAM is used and no frame buffer exists.
// The server uses the built-in synchronous WebServer and is serviced from loop(), so it never runs
// at the same time as the drawing code. There is no login: keep it on the local network only.
#pragma once

#include <WebServer.h>
#include <WiFi.h>
#include <TFT_eSPI.h>
#include "config.h"

static WebServer   web_server(80);
static TFT_eSPI*   web_tft             = nullptr;
static uint32_t    web_captures        = 0;     // screenshots served so far
static uint32_t    web_last_start_ms   = 0;     // when the last screenshot started
static uint32_t    web_last_took_ms    = 0;     // how long the last screenshot took to read and send
static uint32_t    web_rejected        = 0;     // requests refused because they came too soon

static const char WEB_PAGE[] PROGMEM = R"HTML(<!doctype html>
<html><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Roost</title>
<style>
body{margin:0;background:#000;color:#999;font:14px sans-serif;text-align:center}
img{display:block;margin:0 auto;width:100%;max-width:960px;image-rendering:pixelated}
#m{min-height:1.5em;padding-top:4px}
</style></head><body>
<img id="s" alt="Roost display">
<div id="m"></div>
<script>
var img=document.getElementById('s'),msg=document.getElementById('m'),started=0;
function wait(){setTimeout(next,Math.max(50,1000-(Date.now()-started)));}
function next(){
  if(document.hidden){setTimeout(next,1000);return;}   // no updates while the tab is hidden
  started=Date.now();
  img.src='/screen.bmp?t='+started;
}
img.onload=function(){msg.textContent='';wait();};
img.onerror=function(){msg.textContent='No picture - retrying';wait();};
next();
</script></body></html>
)HTML";

static void webPutLE32(uint8_t* p, uint32_t v) {
   p[0] = v; p[1] = v >> 8; p[2] = v >> 16; p[3] = v >> 24;
}

static void webHandleRoot() {
   web_server.sendHeader("Cache-Control", "no-store");
   web_server.send_P(200, "text/html", WEB_PAGE);
}

static void webHandleStatus() {
   char buf[220];
   snprintf(buf, sizeof buf,
            "uptime_s=%lu\nfree_heap=%u\nmin_free_heap=%u\nwifi_rssi=%d\ncaptures=%lu\nrejected=%lu\nlast_capture_ms=%lu\n",
            (unsigned long)(millis() / 1000), (unsigned)ESP.getFreeHeap(), (unsigned)ESP.getMinFreeHeap(), (int)WiFi.RSSI(),
            (unsigned long)web_captures, (unsigned long)web_rejected, (unsigned long)web_last_took_ms);
   web_server.sendHeader("Cache-Control", "no-store");
   web_server.send(200, "text/plain", buf);
}

// Read the whole screen back and stream it as a top-down 24-bit BMP (320 x 240, 230,454 bytes).
static void webHandleScreen() {
   const uint32_t start = millis();
   if (web_captures > 0 && (start - web_last_start_ms) < WEB_MIRROR_MIN_INTERVAL_MS) {
      web_rejected++;
      web_server.send(429, "text/plain", "Too many requests");        // protects the display from being kept busy
      return;
   }
   web_last_start_ms = start;

   const int W = SCREEN_W, H = SCREEN_H, ROWS = WEB_MIRROR_STRIP_ROWS;
   static uint16_t strip[SCREEN_W * WEB_MIRROR_STRIP_ROWS];            // RGB565 as returned by readRect (byte swapped)
   static uint8_t  rgb[SCREEN_W * 3 * WEB_MIRROR_STRIP_ROWS];          // BMP pixels (B, G, R)

   const uint32_t pixel_bytes = (uint32_t)W * H * 3;                   // 960 bytes per row: no padding needed
   uint8_t header[54] = {0};
   header[0] = 'B'; header[1] = 'M';
   webPutLE32(header + 2,  54 + pixel_bytes);                          // file size
   webPutLE32(header + 10, 54);                                        // offset of the pixel data
   webPutLE32(header + 14, 40);                                        // info header size
   webPutLE32(header + 18, W);
   webPutLE32(header + 22, (uint32_t)(-H));                            // negative height: rows run top to bottom
   header[26] = 1;                                                     // planes
   header[28] = 24;                                                    // bits per pixel
   webPutLE32(header + 34, pixel_bytes);

   web_server.setContentLength(54 + pixel_bytes);
   web_server.sendHeader("Cache-Control", "no-store");
   web_server.send(200, "image/bmp", "");
   web_server.sendContent((const char*)header, sizeof header);

   for (int y = 0; y < H; y += ROWS) {
      const int rows = (H - y) < ROWS ? (H - y) : ROWS;
      web_tft->readRect(0, y, W, rows, strip);
      uint8_t* out = rgb;
      for (int i = 0; i < W * rows; i++) {
         const uint16_t c = (uint16_t)((strip[i] << 8) | (strip[i] >> 8));   // undo readRect's byte swap
         const uint8_t r5 = c >> 11, g6 = (c >> 5) & 0x3F, b5 = c & 0x1F;
         *out++ = (b5 << 3) | (b5 >> 2);                                      // expand 5/6/5 bits to 8 bits each
         *out++ = (g6 << 2) | (g6 >> 4);
         *out++ = (r5 << 3) | (r5 >> 2);
      }
      web_server.sendContent((const char*)rgb, (size_t)W * 3 * rows);
   }

   web_captures++;
   web_last_took_ms = millis() - start;
   LOGF("web mirror: screenshot %lu took %lu ms\n", (unsigned long)web_captures, (unsigned long)web_last_took_ms);
}

static void webMirrorBegin(TFT_eSPI& tft) {
   web_tft = &tft;
   web_server.on("/", HTTP_GET, webHandleRoot);
   web_server.on("/screen.bmp", HTTP_GET, webHandleScreen);
   web_server.on("/status", HTTP_GET, webHandleStatus);
   web_server.onNotFound([]() { web_server.send(404, "text/plain", "Not found"); });
   web_server.begin();
   Serial.print("Web mirror: http://");
   Serial.println(WiFi.localIP());
}

// Call from loop() (and from any long-running loop that would otherwise starve the page).
static inline void webMirrorLoop() {
   web_server.handleClient();
}
