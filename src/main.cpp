#include <Arduino.h>
#include <FS.h>
#include <LittleFS.h>
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <ESP8266mDNS.h>
#include <WiFiManager.h>
#include <ArduinoJson.h>
#include <time.h>
#include <AsyncElegantOTA.h>

// -------- Config defaults --------
#ifndef VTZ_DEFAULT
#define VTZ_DEFAULT "ICT-7" // Vietnam
#endif
#ifndef NTP_DEFAULT
#define NTP_DEFAULT "pool.ntp.org"
#endif

// -------- Globals --------
ESP8266WebServer server(80);
String g_timezone = VTZ_DEFAULT;
String g_ntp = NTP_DEFAULT;

// -------- Helpers --------
String contentType(const String &path) {
  if (path.endsWith(".htm") || path.endsWith(".html")) return "text/html";
  if (path.endsWith(".css")) return "text/css";
  if (path.endsWith(".js")) return "application/javascript";
  if (path.endsWith(".json")) return "application/json";
  if (path.endsWith(".png")) return "image/png";
  if (path.endsWith(".gif")) return "image/gif";
  if (path.endsWith(".jpg") || path.endsWith(".jpeg")) return "image/jpeg";
  if (path.endsWith(".gz")) return "application/octet-stream";
  return "text/plain";
}

bool readJsonFile(const char* path, DynamicJsonDocument &doc) {
  File f = LittleFS.open(path, "r");
  if (!f) return false;
  DeserializationError e = deserializeJson(doc, f);
  f.close();
  return !e;
}

bool writeJsonFile(const char* path, DynamicJsonDocument &doc) {
  File f = LittleFS.open(path, "w");
  if (!f) return false;
  serializeJson(doc, f);
  f.close();
  return true;
}

void loadConfig() {
  DynamicJsonDocument cfg(1024);
  if (readJsonFile("/config.json", cfg)) {
    if (cfg.containsKey("timezone")) g_timezone = cfg["timezone"].as<String>();
    if (cfg.containsKey("ntp")) g_ntp = cfg["ntp"].as<String>();
  } else {
    cfg["timezone"] = g_timezone;
    cfg["ntp"] = g_ntp;
    writeJsonFile("/config.json", cfg);
  }
}

void applyTimezone() {
  setenv("TZ", g_timezone.c_str(), 1);
  tzset();
}

void applyNTP() {
  // Use zero offsets; timezone handled via TZ/localtime()
  configTime(0, 0, g_ntp.c_str());
}

String isoTime(time_t t) {
  struct tm tm;
  localtime_r(&t, &tm);
  char buf[32];
  strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%S%z", &tm);
  return String(buf);
}

void handleGet(const char* pathFS, bool gz = false) {
  String path = String(pathFS);
  if (gz && !path.endsWith(".gz")) path += ".gz";
  if (!LittleFS.exists(path)) { server.send(404, "text/plain", "Not Found"); return; }
  File f = LittleFS.open(path, "r");
  server.streamFile(f, contentType(path));
  f.close();
}

void setupRoutes() {
  server.on("/", HTTP_GET, []() { handleGet("/settings.html", true); });

  server.on("/config.json", HTTP_GET, [](){
    DynamicJsonDocument cfg(1024);
    readJsonFile("/config.json", cfg);
    cfg["uptime"] = (uint32_t)millis()/1000;
    String out; serializeJson(cfg, out);
    server.send(200, "application/json", out);
  });
  server.on("/config.json", HTTP_PUT, [](){
    if (!server.hasArg("plain")) { server.send(400, "text/plain", "No Body"); return; }
    DynamicJsonDocument cfg(1024);
    DeserializationError e = deserializeJson(cfg, server.arg("plain"));
    if (e) { server.send(400, "text/plain", "Bad JSON"); return; }
    if (cfg.containsKey("timezone")) g_timezone = cfg["timezone"].as<String>();
    if (cfg.containsKey("ntp")) g_ntp = cfg["ntp"].as<String>();
    writeJsonFile("/config.json", cfg);
    applyTimezone();
    applyNTP();
    server.send(200, "application/json", "{\"ok\":true}");
  });

  server.on("/ntp.json", HTTP_GET, [](){
    DynamicJsonDocument doc(256);
    doc["ntp"] = g_ntp;
    doc["timezone"] = g_timezone;
    String out; serializeJson(doc, out);
    server.send(200, "application/json", out);
  });

  server.on("/wifi.json", HTTP_GET, [](){
    DynamicJsonDocument doc(512);
    doc["ssid"] = WiFi.SSID();
    doc["rssi"] = WiFi.RSSI();
    doc["ip"] = WiFi.localIP().toString();
    doc["mac"] = WiFi.macAddress();
    String out; serializeJson(doc, out);
    server.send(200, "application/json", out);
  });

  server.on("/time.json", HTTP_GET, [](){
    time_t now = time(nullptr);
    struct tm ltm;
    localtime_r(&now, &ltm);
    DynamicJsonDocument doc(512);
    doc["epoch"] = (uint32_t)now;
    doc["iso"] = isoTime(now);
    char buf[32]; strftime(buf, sizeof(buf), "%H:%M:%S", &ltm);
    doc["local"] = buf;
    doc["tz"] = g_timezone;
    String out; serializeJson(doc, out);
    server.send(200, "application/json", out);
  });

  // List files
  server.on("/fs/list", HTTP_GET, [](){
    DynamicJsonDocument doc(4096);
    JsonArray arr = doc.to<JsonArray>();
    Dir dir = LittleFS.openDir("/");
    while (dir.next()) {
      JsonObject o = arr.createNestedObject();
      o["name"] = dir.fileName();
      o["size"] = (uint32_t)dir.fileSize();
    }
    String out; serializeJson(arr, out);
    server.send(200, "application/json", out);
  });

  // Delete file
  server.on("/fs/delete", HTTP_DELETE, [](){
    if (!server.hasArg("path")) { server.send(400, "text/plain", "missing path"); return; }
    String p = server.arg("path");
    if (!p.startsWith("/")) p = "/" + p;
    bool ok = LittleFS.remove(p);
    server.send(200, "application/json", ok ? "{\"ok\":true}" : "{\"ok\":false}");
  });

  // Upload file (multipart/form-data)
  server.on("/fs/upload", HTTP_POST, []() {
    server.send(200, "application/json", "{\"ok\":true}");
  }, []() {
    HTTPUpload& up = server.upload();
    static File f;
    if (up.status == UPLOAD_FILE_START) {
      String filename = up.filename;
      if (!filename.startsWith("/")) filename = "/" + filename;
      f = LittleFS.open(filename, "w");
    } else if (up.status == UPLOAD_FILE_WRITE) {
      if (f) f.write(up.buf, up.currentSize);
    } else if (up.status == UPLOAD_FILE_END) {
      if (f) f.close();
    }
  });

  AsyncElegantOTA.begin(&server); // /update
  server.begin();
}

void ensureFSDefaults() {
  if (!LittleFS.exists("/config.json")) {
    DynamicJsonDocument cfg(256);
    cfg["timezone"] = g_timezone;
    cfg["ntp"] = g_ntp;
    writeJsonFile("/config.json", cfg);
  }
  if (!LittleFS.exists("/ntp.json")) {
    DynamicJsonDocument doc(256);
    doc["ntp"] = g_ntp;
    doc["timezone"] = g_timezone;
    writeJsonFile("/ntp.json", doc);
  }
}

void setup() {
  Serial.begin(115200);
  delay(200);

  if (!LittleFS.begin()) {
    Serial.println("LittleFS mount failed, formatting...");
    LittleFS.format();
    LittleFS.begin();
  }

  WiFiManager wm;
  wm.setConfigPortalTimeout(180);
  bool res = wm.autoConnect("HelloCubic-Setup");
  if (!res) {
    Serial.println("WiFi connect failed; rebooting...");
    delay(1000);
    ESP.restart();
  }

  loadConfig();
  applyTimezone();
  applyNTP();

  if (MDNS.begin("hellocubic")) {
    MDNS.addService("http", "tcp", 80);
  }

  ensureFSDefaults();
  setupRoutes();

  Serial.printf("Ready, IP: %s, TZ: %s, NTP: %s\n",
                WiFi.localIP().toString().c_str(),
                g_timezone.c_str(),
                g_ntp.c_str());
}

void loop() {
  server.handleClient();
  // ElegantOTA uses Async; no special loop needed here
}