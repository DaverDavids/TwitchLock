#pragma once
#include <Arduino.h>

String buildHTML(
  const String &channel,
  const String &oauth,
  const String &botname,
  bool tCmd, bool tAny, bool tVIP, bool tSub, bool tMod, bool tBroad,
  const String &cmds,
  const String &whitelist,
  const String &blacklist
) {
  auto chk = [](bool b) -> String { return b ? " checked" : ""; };

  String html = R"rawhtml(
<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>TwitchLock Config</title>
<style>
  body{font-family:sans-serif;background:#0e0e10;color:#efeff1;max-width:520px;margin:30px auto;padding:0 16px}
  h1{color:#9147ff;margin-bottom:4px}h2{color:#bf94ff;font-size:1em;margin:20px 0 6px}
  input[type=text],input[type=password],textarea{
    width:100%;box-sizing:border-box;background:#18181b;color:#efeff1;
    border:1px solid #3a3a3d;border-radius:4px;padding:7px;font-size:.95em;margin-top:3px}
  textarea{height:68px;resize:vertical}
  label{display:flex;align-items:center;gap:8px;margin:6px 0;cursor:pointer}
  input[type=checkbox]{width:18px;height:18px;accent-color:#9147ff}
  button{background:#9147ff;color:#fff;border:none;border-radius:4px;padding:10px 28px;
    font-size:1em;cursor:pointer;margin-top:14px;width:100%}
  button:hover{background:#7c3aed}
  .note{font-size:.8em;color:#adadb8;margin-top:3px}
  hr{border:none;border-top:1px solid #3a3a3d;margin:18px 0}
</style>
</head>
<body>
<h1>&#x1F512; TwitchLock</h1>
<form method="POST" action="/save">

  <h2>Twitch Channel</h2>
  <input type="text" name="channel" placeholder="channelname (no #)" value=")rawhtml";
  html += channel;
  html += R"rawhtml("><br>

  <h2>IRC Credentials</h2>
  <input type="text"     name="botname" placeholder="Bot username (blank = anonymous)" value=")rawhtml";
  html += botname;
  html += R"rawhtml("><br>
  <input type="password" name="oauth"   placeholder="oauth:xxxxx (blank = anonymous)" value=")rawhtml";
  html += oauth;
  html += R"rawhtml("><br>
  <p class="note">Leave both blank to join anonymously (read-only, no chat send).</p>

  <hr>
  <h2>Who Can Trigger</h2>
  <label><input type="checkbox" name="tBroad")rawhtml";
  html += chk(tBroad);
  html += R"rawhtml(> Broadcaster</label>
  <label><input type="checkbox" name="tMod")rawhtml";
  html += chk(tMod);
  html += R"rawhtml(> Moderators</label>
  <label><input type="checkbox" name="tVIP")rawhtml";
  html += chk(tVIP);
  html += R"rawhtml(> VIPs</label>
  <label><input type="checkbox" name="tSub")rawhtml";
  html += chk(tSub);
  html += R"rawhtml(> Subscribers</label>
  <label><input type="checkbox" name="tAny")rawhtml";
  html += chk(tAny);
  html += R"rawhtml(> Any chatter</label>

  <p class="note">Whitelisted users below always qualify regardless of badges.</p>

  <hr>
  <h2>Trigger Commands</h2>
  <label><input type="checkbox" name="tCmd")rawhtml";
  html += chk(tCmd);
  html += R"rawhtml(> Require message to match a command below</label>
  <textarea name="cmds" placeholder="!lock, !open, !trigger">)rawhtml";
  html += cmds;
  html += R"rawhtml(</textarea>
  <p class="note">Comma-separated. If unchecked, any message from a qualified user triggers.</p>

  <hr>
  <h2>Whitelist <span style="font-weight:normal;color:#adadb8">(always allowed)</span></h2>
  <textarea name="whitelist" placeholder="user1, user2">)rawhtml";
  html += whitelist;
  html += R"rawhtml(</textarea>

  <h2>Blacklist <span style="font-weight:normal;color:#adadb8">(never triggers)</span></h2>
  <textarea name="blacklist" placeholder="spammer1, bot2">)rawhtml";
  html += blacklist;
  html += R"rawhtml(</textarea>

  <button type="submit">&#x1F4BE; Save &amp; Apply</button>
</form>

<hr>
<h2>WiFi Credentials</h2>
<form method="POST" action="/wifi">
  <input type="text"     name="ssid" placeholder="SSID"><br>
  <input type="password" name="psk"  placeholder="Password"><br>
  <button type="submit">Save WiFi &amp; Reboot</button>
</form>
</body>
</html>
)rawhtml";
  return html;
}
