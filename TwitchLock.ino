/*
 * TwitchLock - ESP32-C3 Twitch Chat GPIO Trigger
 * Watches a Twitch channel IRC and triggers GPIO3 HIGH for 5s on match.
 *
 * Feature toggles (set to 0 to disable):
 */
#define ENABLE_OTA       1
#define ENABLE_WEBUI     1
#define ENABLE_DEBUG     1

#include <Arduino.h>
#include <WiFi.h>
#include <Preferences.h>
#include <WiFiClient.h>
#include <ESPmDNS.h>
#include <ArduinoOTA.h>
#include <WebServer.h>
#include <DNSServer.h>
#include "html.h"
#include <Secrets.h>

// ── GPIO ──────────────────────────────────────────────────────────────────────
#define TRIGGER_PIN      3
#define TRIGGER_MS    5000

// ── Twitch IRC ────────────────────────────────────────────────────────────────
#define HOSTNAME  "twitchlock"
#define TWITCH_HOST  "irc.chat.twitch.tv"
#define TWITCH_PORT  6667

#if ENABLE_DEBUG
  #define DBG(...)  Serial.printf(__VA_ARGS__)
#else
  #define DBG(...)
#endif

// ── Runtime state ────────────────────────────────────────────────────────────
Preferences prefs;
WebServer   server(80);
DNSServer   dns;
WiFiClient  irc;

String  channel, oauthToken, botName;
bool    triggerCommands, triggerAnyChatter, triggerVIP, triggerSub,
        triggerMod, triggerBroadcaster;
String  commandList;   // comma-separated !commands
String  whitelist;     // comma-separated usernames
String  blacklist;     // comma-separated usernames

bool    captiveMode    = false;
unsigned long triggerEnd = 0;

// ── Helpers ───────────────────────────────────────────────────────────────────
void loadPrefs() {
  prefs.begin("twitchlock", false);
  channel          = prefs.getString("channel",     "");
  oauthToken       = prefs.getString("oauth",       "");
  botName          = prefs.getString("botname",     "justinfan12345");
  triggerCommands  = prefs.getBool("tCmd",          true);
  triggerAnyChatter= prefs.getBool("tAny",          false);
  triggerVIP       = prefs.getBool("tVIP",          false);
  triggerSub       = prefs.getBool("tSub",          false);
  triggerMod       = prefs.getBool("tMod",          false);
  triggerBroadcaster=prefs.getBool("tBroad",        false);
  commandList      = prefs.getString("cmds",        "!lock");
  whitelist        = prefs.getString("whitelist",   "");
  blacklist        = prefs.getString("blacklist",   "");
  prefs.end();
}

void savePrefs() {
  prefs.begin("twitchlock", false);
  prefs.putString("channel",  channel);
  prefs.putString("oauth",    oauthToken);
  prefs.putString("botname",  botName);
  prefs.putBool("tCmd",       triggerCommands);
  prefs.putBool("tAny",       triggerAnyChatter);
  prefs.putBool("tVIP",       triggerVIP);
  prefs.putBool("tSub",       triggerSub);
  prefs.putBool("tMod",       triggerMod);
  prefs.putBool("tBroad",     triggerBroadcaster);
  prefs.putString("cmds",     commandList);
  prefs.putString("whitelist",whitelist);
  prefs.putString("blacklist",blacklist);
  prefs.end();
}

// Check if a username is in a comma-separated list (case-insensitive)
bool inList(const String &list, const String &user) {
  String u = user; u.toLowerCase();
  String l = list; l.toLowerCase();
  int start = 0;
  while (start < (int)l.length()) {
    int comma = l.indexOf(',', start);
    if (comma == -1) comma = l.length();
    String entry = l.substring(start, comma);
    entry.trim();
    if (entry == u) return true;
    start = comma + 1;
  }
  return false;
}

void doTrigger() {
  if (triggerEnd == 0) {
    DBG("[TRIGGER] GPIO%d HIGH for %dms\n", TRIGGER_PIN, TRIGGER_MS);
    digitalWrite(TRIGGER_PIN, HIGH);
  }
  triggerEnd = millis() + TRIGGER_MS;
}

// Parse a Twitch IRC PRIVMSG line and decide whether to trigger
void handleLine(const String &line) {
  // Format: @badges=...;...;user-type=xxx :nick!nick@nick.tmi.twitch.tv PRIVMSG #channel :message
  if (line.indexOf("PRIVMSG") < 0) return;

  // Extract badge string
  String badges = "";
  int atEnd = line.indexOf(' ');
  if (line.startsWith("@") && atEnd > 0) {
    String tags = line.substring(1, atEnd);
    // badges=vip/1,subscriber/0,...
    int bi = tags.indexOf("badges=");
    if (bi >= 0) {
      int semi = tags.indexOf(';', bi);
      badges = tags.substring(bi + 7, semi < 0 ? tags.length() : semi);
      badges.toLowerCase();
    }
  }

  // Extract nick
  int excl = line.indexOf('!');
  int colon1 = line.indexOf(':');
  String nick = "";
  if (excl > 0 && colon1 > 0 && excl < line.indexOf('@')) {
    // find the colon before nick
    int nc = line.indexOf(':', 1);
    nick = line.substring(nc + 1, excl);
  } else {
    // fallback
    int nc = line.indexOf(':', 1);
    int ex = line.indexOf('!', nc);
    if (nc > 0 && ex > nc) nick = line.substring(nc + 1, ex);
  }
  nick.toLowerCase();

  // Extract message
  // second colon after PRIVMSG #channel :
  int privIdx = line.indexOf("PRIVMSG");
  int msgColon = line.indexOf(':', privIdx);
  String msg = "";
  if (msgColon >= 0) msg = line.substring(msgColon + 1);
  msg.trim();

  DBG("[IRC] nick=%s badges=%s msg=%s\n", nick.c_str(), badges.c_str(), msg.c_str());

  // Blacklist check – always block
  if (inList(blacklist, nick)) { DBG("[SKIP] blacklisted\n"); return; }

  // Determine badge roles
  bool isBroadcaster = badges.indexOf("broadcaster") >= 0;
  bool isMod         = badges.indexOf("moderator")   >= 0 || isBroadcaster;
  bool isVIP         = badges.indexOf("vip")         >= 0;
  bool isSub         = badges.indexOf("subscriber")  >= 0 || badges.indexOf("/") >= 0;
  // (subscriber/N pattern means subscriber)
  if (badges.indexOf("subscriber/") >= 0) isSub = true;

  // Decide if this user/message qualifies
  bool userOk = false;
  if (inList(whitelist, nick))    userOk = true;
  if (triggerBroadcaster && isBroadcaster) userOk = true;
  if (triggerMod  && isMod)      userOk = true;
  if (triggerVIP  && isVIP)      userOk = true;
  if (triggerSub  && isSub)      userOk = true;
  if (triggerAnyChatter)         userOk = true;

  if (!userOk) { DBG("[SKIP] user not qualified\n"); return; }

  // Command check
  if (triggerCommands) {
    String cmds = commandList;
    cmds.toLowerCase();
    String msgL = msg; msgL.toLowerCase();
    int start = 0;
    bool matched = false;
    while (start < (int)cmds.length()) {
      int comma = cmds.indexOf(',', start);
      if (comma == -1) comma = cmds.length();
      String cmd = cmds.substring(start, comma);
      cmd.trim();
      if (cmd.length() > 0 && msgL.startsWith(cmd)) { matched = true; break; }
      start = comma + 1;
    }
    if (!matched) { DBG("[SKIP] command not matched\n"); return; }
  }

  doTrigger();
}

// ── IRC connection ────────────────────────────────────────────────────────────
void ircConnect() {
  if (channel.isEmpty()) return;
  DBG("[IRC] Connecting to %s:%d\n", TWITCH_HOST, TWITCH_PORT);
  if (!irc.connect(TWITCH_HOST, TWITCH_PORT)) {
    DBG("[IRC] Connection failed\n");
    return;
  }
  String pass = oauthToken.length() > 0 ? oauthToken : "SCHMOOPIIE";
  String nick = botName.length() > 0  ? botName    : "justinfan12345";
  irc.printf("PASS %s\r\n", pass.c_str());
  irc.printf("NICK %s\r\n", nick.c_str());
  irc.printf("CAP REQ :twitch.tv/tags twitch.tv/commands\r\n");
  irc.printf("JOIN #%s\r\n", channel.c_str());
  DBG("[IRC] Joined #%s\n", channel.c_str());
}

// ── Web UI handlers ───────────────────────────────────────────────────────────
#if ENABLE_WEBUI
void handleRoot() {
  server.send(200, "text/html", buildHTML(
    channel, oauthToken, botName,
    triggerCommands, triggerAnyChatter, triggerVIP, triggerSub, triggerMod, triggerBroadcaster,
    commandList, whitelist, blacklist
  ));
}

void handleSave() {
  channel           = server.arg("channel");      channel.trim();
  oauthToken        = server.arg("oauth");        oauthToken.trim();
  botName           = server.arg("botname");      botName.trim();
  triggerCommands   = server.hasArg("tCmd");
  triggerAnyChatter = server.hasArg("tAny");
  triggerVIP        = server.hasArg("tVIP");
  triggerSub        = server.hasArg("tSub");
  triggerMod        = server.hasArg("tMod");
  triggerBroadcaster= server.hasArg("tBroad");
  commandList       = server.arg("cmds");         commandList.trim();
  whitelist         = server.arg("whitelist");    whitelist.trim();
  blacklist         = server.arg("blacklist");    blacklist.trim();
  savePrefs();
  // Reconnect IRC with new settings
  irc.stop();
  delay(100);
  ircConnect();
  server.sendHeader("Location", "/");
  server.send(302, "text/plain", "Saved");
}

void handleWifiSave() {
  String ssid = server.arg("ssid");
  String psk  = server.arg("psk");
  prefs.begin("wifi", false);
  prefs.putString("ssid", ssid);
  prefs.putString("psk",  psk);
  prefs.end();
  server.send(200, "text/html",
    "<h2>WiFi saved. Rebooting...</h2>"
    "<p>Reconnect to your network and visit http://" HOSTNAME ".local</p>");
  delay(1500);
  ESP.restart();
}

void setupWebServer() {
  server.on("/",       HTTP_GET,  handleRoot);
  server.on("/save",   HTTP_POST, handleSave);
  server.on("/wifi",   HTTP_POST, handleWifiSave);
  server.onNotFound([](){ server.sendHeader("Location","http://192.168.4.1"); server.send(302); });
  server.begin();
  DBG("[WEB] Server started\n");
}
#endif // ENABLE_WEBUI

// ── WiFi / Captive Portal ─────────────────────────────────────────────────────
void startCaptivePortal() {
  captiveMode = true;
  DBG("[WIFI] Starting captive portal AP\n");
  WiFi.mode(WIFI_AP);
  WiFi.softAP(HOSTNAME "_setup");
  dns.start(53, "*", IPAddress(192,168,4,1));
#if ENABLE_WEBUI
  setupWebServer();
#endif
}

bool connectWifi() {
  // Try saved creds first, then Secrets.h
  prefs.begin("wifi", true);
  String savedSSID = prefs.getString("ssid", "");
  String savedPSK  = prefs.getString("psk",  "");
  prefs.end();

  String ssid = savedSSID.length() > 0 ? savedSSID : MYSSID;
  String psk  = savedPSK.length()  > 0 ? savedPSK  : MYPSK;

  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid.c_str(), psk.c_str());
  WiFi.setTxPower(WIFI_POWER_11dBm);
  DBG("[WIFI] Connecting to %s\n", ssid.c_str());

  unsigned long t = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - t < 15000) {
    delay(250); DBG(".");
  }
  DBG("\n");
  if (WiFi.status() == WL_CONNECTED) {
    DBG("[WIFI] Connected, IP=%s\n", WiFi.localIP().toString().c_str());
    return true;
  }
  return false;
}

// ── Setup ─────────────────────────────────────────────────────────────────────
void setup() {
#if ENABLE_DEBUG
  Serial.begin(115200);
  delay(500);
  DBG("[BOOT] TwitchLock starting\n");
#endif

  pinMode(TRIGGER_PIN, OUTPUT);
  digitalWrite(TRIGGER_PIN, LOW);

  loadPrefs();

  if (!connectWifi()) {
    startCaptivePortal();
    return; // Stay in captive portal loop
  }

  MDNS.begin(HOSTNAME);
  DBG("[mDNS] http://%s.local\n", HOSTNAME);

#if ENABLE_OTA
  ArduinoOTA.setHostname(HOSTNAME);
  ArduinoOTA.onStart([]()  { DBG("[OTA] Start\n"); });
  ArduinoOTA.onError([](ota_error_t e) { DBG("[OTA] Error %u\n", e); });
  ArduinoOTA.begin();
  DBG("[OTA] Ready\n");
#endif

#if ENABLE_WEBUI
  setupWebServer();
#endif

  ircConnect();
}

// ── Loop ──────────────────────────────────────────────────────────────────────
void loop() {
  if (captiveMode) {
    dns.processNextRequest();
#if ENABLE_WEBUI
    server.handleClient();
#endif
    return;
  }

#if ENABLE_OTA
  ArduinoOTA.handle();
#endif
#if ENABLE_WEBUI
  server.handleClient();
#endif

  // GPIO timer
  if (triggerEnd > 0 && millis() >= triggerEnd) {
    digitalWrite(TRIGGER_PIN, LOW);
    DBG("[TRIGGER] GPIO%d LOW\n", TRIGGER_PIN);
    triggerEnd = 0;
  }

  // IRC reconnect if dropped
  if (WiFi.status() != WL_CONNECTED) {
    DBG("[WIFI] Lost connection, reconnecting...\n");
    WiFi.reconnect();
    delay(3000);
    return;
  }
  if (!irc.connected()) {
    DBG("[IRC] Disconnected, reconnecting in 5s...\n");
    delay(5000);
    ircConnect();
    return;
  }

  // Read IRC lines
  while (irc.available()) {
    String line = irc.readStringUntil('\n');
    line.trim();
    if (line.startsWith("PING")) {
      irc.println("PONG :tmi.twitch.tv");
      DBG("[IRC] PONG\n");
    } else {
      handleLine(line);
    }
  }
}
