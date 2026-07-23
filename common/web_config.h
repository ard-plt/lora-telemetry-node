#pragma once
#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <Preferences.h>
#include "lora_modes.h"

// ============================================================================
// Web tabanlı config paneli — GPIO9'daki momentary butonu 3sn basılı tutunca
// açılan WiFi AP + captive portal üzerinden mod seçimi (KALİBRASYON/UZUN MENZİL)
// ve canlı istatistik gösterimi.
//
// TX ve RX node aynı modülü kullanır; farklılaşan kısımlar (batarya voltajı,
// RX'in link istatistikleri vb.) WebConfigCallbacks üzerinden main.cpp'den
// enjekte edilir. setMode/setAirRate callback'leri main.cpp'deki applyProfile()
// benzeri fonksiyonu tetikler — BridgeMode ve AirRate birbirinden bağımsız
// seçilir (bkz. lora_modes.h).
//
// NOT: common/ her iki projeye -I../common ile header-only dahil edilir
// (packet.h, lora_modes.h ile aynı desen) — bu yüzden ayrı bir .cpp yok,
// tüm gövdeler in-class (dolayısıyla implicit inline) tanımlı.
// ============================================================================

#define WEB_CONFIG_BUTTON_HOLD_MS    3000UL              // butonu bu kadar basılı tutunca AP açılır
#define WEB_CONFIG_AP_TIMEOUT_MS     (5UL * 60UL * 1000UL) // 5 dk hareketsizlikte AP kapanır
#define WEB_CONFIG_PAIRED_WINDOW_MS  3000UL              // son paket/gönderim bu süreden yeniyse "bagli"
#define WEB_CONFIG_CLOSE_DELAY_MS    800UL               // save_close yanıtının client'a ulaşması için gecikme

// Fabrika varsayılan AP şifresi — TÜM cihazlarda AYNI (eski MAC-türetilmiş
// HMAC şifre sistemi kaldırıldı, bkz. CLAUDE.md güvenlik notu: kapalı/
// kontrollü ortamlarda kabul edilebilir bir tercih; açık dağıtımda operatör
// web panelinden değiştirmeli). WPA2 alt sınırı 8 karakterdir.
#define WEB_CONFIG_DEFAULT_AP_PASSWORD "tuav-loralink"
#define WEB_CONFIG_AP_PW_MIN_LEN        8

// Tüm alanlar zorunlu — appendExtraStatusJson hariç (NULL verilebilir).
struct WebConfigCallbacks {
    const char* apSsid;          // örn. "TUAV-TX-Config"
    const char* nvsNamespace;    // örn. "txcfg" / "rxcfg" (Preferences namespace, <=15 char)

    BridgeMode (*getMode)();                 // mevcut bridge mode'u döndürür
    void       (*setMode)(BridgeMode mode);  // canlı uygular (applyProfile), NVS'e YAZMAZ

    // AirRate, BridgeMode'dan BAĞIMSIZ seçilir (bkz. lora_modes.h) ama aynı
    // lora_apply_profile() çağrısını tetikler — setMode/setAirRate ikisi de
    // "canlı uygula, NVS'e yazma" sözleşmesini paylaşır.
    AirRate (*getAirRate)();
    void    (*setAirRate)(AirRate rate);

    uint32_t (*getPacketCount)();      // TX: gönderilen, RX: alınan paket sayısı
    uint32_t (*getLastEventAgoMs)();   // son gönderim/alım üzerinden geçen ms

    // /status JSON'una ham alan ekler, örn: ",\"vbat_mv\":12345" — NULL olabilir
    void (*appendExtraStatusJson)(String& json);
};

class WebConfigPortal {
public:
    // Callback setini kaydeder. AP henüz AÇILMAZ — buton basılı tutulana kadar bekler.
    void begin(const WebConfigCallbacks& cb) {
        _cb = cb;
        _apPassword = loadApPassword(_cb.nvsNamespace);
        Serial.printf("[WebConfig] Hazir. Config'e girmek icin butonu %lus basili tut. (AP sifresi: %s)\n",
                      WEB_CONFIG_BUTTON_HOLD_MS / 1000UL, _apPassword.c_str());
    }

    // loop() içinde HER iterasyonda çağrılmalı. delay() KULLANMAZ:
    //  - AP kapalıyken: sadece buton basılı tutma süresini ölçer
    //  - AP açıkken: DNS + HTTP isteklerini servis eder, 5dk timeout'u kontrol eder
    void update(int buttonPin) {
        bool pressed = (digitalRead(buttonPin) == LOW); // INPUT_PULLUP varsayımı

        if (!_active) {
            if (pressed) {
                if (!_pressing) {
                    _pressing = true;
                    _pressStartMs = millis();
                } else if (millis() - _pressStartMs >= WEB_CONFIG_BUTTON_HOLD_MS) {
                    _pressing = false;
                    _startAP();
                }
            } else {
                _pressing = false;
            }
            return;
        }

        // AP aktif — non-blocking servis
        _dns.processNextRequest();
        _server.handleClient();

        if (_closePending && millis() >= _closeAtMs) {
            _closePending = false;
            _stopAP();
            return;
        }

        if (millis() - _lastActivityMs > WEB_CONFIG_AP_TIMEOUT_MS) {
            Serial.println("[WebConfig] 5 dk hareketsizlik - AP kapatiliyor, normal moda donuluyor");
            _stopAP();
        }
    }

    bool isActive() const { return _active; }

    // setup()'ta applyProfile()'dan ÖNCE çağır — son kaydedilen bridge mode'u NVS'ten okur.
    static BridgeMode loadSavedMode(const char* nvsNamespace, BridgeMode fallback) {
        Preferences prefs;
        if (!prefs.begin(nvsNamespace, true)) return fallback; // ilk açılış: namespace yok, varsayılanı kullan
        uint8_t raw = prefs.getUChar("mode", (uint8_t)fallback); // NVS anahtarı "mode" — eski cihazlarla uyumlu
        prefs.end();
        return (raw == (uint8_t)BRIDGE_TRANSPARENT) ? BRIDGE_TRANSPARENT : BRIDGE_COMPRESSED;
    }

    // setup()'ta applyProfile()'dan ÖNCE çağır — son kaydedilen air rate'i NVS'ten okur.
    static AirRate loadSavedAirRate(const char* nvsNamespace, AirRate fallback) {
        Preferences prefs;
        if (!prefs.begin(nvsNamespace, true)) return fallback;
        uint8_t raw = prefs.getUChar("rate", (uint8_t)fallback);
        prefs.end();
        if (raw >= LORA_AIR_RATE_COUNT) return fallback; // bozuk/gecersiz deger - varsayilana don
        return (AirRate)raw;
    }

    // NVS'te "ap_password" anahtarı kayıtlıysa (ve WPA2 min uzunluğunu
    // karşılıyorsa) onu döndürür; yoksa/bozuksa fabrika varsayılanına
    // (WEB_CONFIG_DEFAULT_AP_PASSWORD, tüm cihazlarda AYNI) düşer. Eski
    // MAC-türetilmiş HMAC şifre sistemi kaldırıldı — bkz. CLAUDE.md güvenlik notu.
    static String loadApPassword(const char* nvsNamespace) {
        Preferences prefs;
        if (!prefs.begin(nvsNamespace, true)) return WEB_CONFIG_DEFAULT_AP_PASSWORD;
        String pw = prefs.getString("ap_password", WEB_CONFIG_DEFAULT_AP_PASSWORD);
        prefs.end();
        if (pw.length() < WEB_CONFIG_AP_PW_MIN_LEN) return WEB_CONFIG_DEFAULT_AP_PASSWORD; // bozuk/kisa deger - guvenli varsayilana don
        return pw;
    }

private:
    void _startAP() {
        Serial.println("\n[WebConfig] Buton 3sn basili tutuldu -> config moduna giriliyor");

        // Onceki bir AP oturumunda sifre degistirilip kaydedilmis olabilir -
        // her acilista NVS'ten TAZE oku (bkz. _handleSetApPassword: degisiklik
        // "bir sonraki AP acilisinda" gecerli olur, o an aktif oturumu etkilemez).
        _apPassword = loadApPassword(_cb.nvsNamespace);

        WiFi.mode(WIFI_AP);
        WiFi.softAP(_cb.apSsid, _apPassword.c_str());
        IPAddress ip = WiFi.softAPIP();

        Serial.printf("[WebConfig] AP acik: SSID=%s SIFRE=%s  http://%s/\n",
                      _cb.apSsid, _apPassword.c_str(), ip.toString().c_str());

        _dns.start(53, "*", ip);

        _server.on("/", HTTP_GET, [this]() { _handleRoot(); });
        _server.on("/set_mode", HTTP_GET, [this]() { _handleSetMode(); });
        _server.on("/set_rate", HTTP_GET, [this]() { _handleSetRate(); });
        _server.on("/set_ap_password", HTTP_POST, [this]() { _handleSetApPassword(); });
        _server.on("/status", HTTP_GET, [this]() { _handleStatus(); });
        _server.on("/save_close", HTTP_GET, [this]() { _handleSaveClose(); });
        _server.onNotFound([this]() { _handleRoot(); }); // captive portal: bilinmeyen istekler ana sayfaya düşer

        _server.begin();

        _active = true;
        _closePending = false;
        _touchActivity();
    }

    void _stopAP() {
        _server.stop();
        _dns.stop();
        WiFi.softAPdisconnect(true);
        WiFi.mode(WIFI_OFF);
        _active = false;
    }

    void _handleRoot() {
        _touchActivity();

        const char* modeName = (_cb.getMode() == BRIDGE_TRANSPARENT) ? "KALIBRASYON" : "UZUN MENZIL";
        const char* rateKbps = air_rate_kbps_str(_cb.getAirRate());

        String html;
        html.reserve(4200);
        html += F(
            "<!DOCTYPE html><html><head><meta charset='utf-8'>"
            "<meta name='viewport' content='width=device-width,initial-scale=1'>"
            "<title>TUAV LoRa Config</title><style>"
            "body{font-family:sans-serif;background:#111;color:#eee;margin:0;padding:16px}"
            "h1{font-size:1.2em;margin-top:0}"
            ".card{background:#1c1c1c;border-radius:8px;padding:16px;margin-bottom:12px}"
            "button,select,input{width:100%;padding:14px;margin:6px 0;font-size:1em;border:none;border-radius:6px;color:#fff;box-sizing:border-box}"
            "select,input{background:#2c2c2c}"
            ".cal{background:#2b6cb0}.lr{background:#2f855a}.close{background:#a33;margin-top:4px}"
            ".row{display:flex;justify-content:space-between;margin:4px 0}"
            ".dot{width:10px;height:10px;border-radius:50%;display:inline-block;margin-right:6px}"
            ".on{background:#2f855a}.off{background:#a33}"
            "</style></head><body>");
        html += F("<h1>TUAV LoRa Yapilandirma</h1>");
        html += "<div class='card'><div class='row'><b>Mevcut Mod</b><span id='mode'>" + String(modeName) + "</span></div>";
        html += F(
            "<button class='cal' onclick=\"setMode('cal')\">Kalibrasyona Gec</button>"
            "<button class='lr' onclick=\"setMode('lr')\">Uzun Menzile Gec</button></div>");
        html += "<div class='card'><div class='row'><b>Air Data Rate</b><span id='rate'>" + String(rateKbps) + " kbps</span></div>";
        html += F(
            "<select id='rateSel' onchange=\"setRate(this.value)\">"
            "<option value='0.3'>0.3 kbps</option><option value='1.2'>1.2 kbps</option>"
            "<option value='2.4'>2.4 kbps</option><option value='4.8'>4.8 kbps</option>"
            "<option value='9.6'>9.6 kbps</option><option value='19.2'>19.2 kbps</option>"
            "<option value='38.4'>38.4 kbps</option><option value='62.5'>62.5 kbps</option>"
            "</select></div>");
        html += F(
            "<div class='card'><div class='row'><span>Paket Sayisi</span><span id='cnt'>-</span></div>"
            "<div class='row'><span>Son Paket</span><span id='age'>-</span></div>"
            "<div class='row'><span>Baglanti</span><span><span id='dot' class='dot off'></span><span id='paired'>-</span></span></div>"
            "<div id='vbatRow' class='row' style='display:none'><span>Pixhawk Batarya</span><span id='vbat'>-</span></div>"
            "<div id='rssiRow' class='row' style='display:none'><span>RSSI</span><span id='rssi'>-</span></div>"
            "<div id='lostRow' class='row' style='display:none'><span>Kayip / Bozuk</span><span id='lostcorrupt'>-</span></div>"
            "<div id='effRow' class='row' style='display:none'><span>Verimlilik (30sn)</span><span id='eff'>-</span></div>"
            "<div id='syncRow' class='row' style='display:none'><span>Senkronizasyon</span><span id='syncStatus'>-</span></div>"
            "</div>");
        html += F(
            "<div class='card'><div class='row'><b>AP Sifresini Degistir</b></div>"
            "<input type='password' id='apPwInput' placeholder='Yeni sifre (min 8 karakter)'>"
            "<button onclick=\"setApPassword()\">Sifreyi Kaydet</button>"
            "<div class='row'><span id='apPwMsg'></span></div></div>");
        html += F("<button class='close' onclick=\"closeAp()\">Ucusa Hazir / Kaydet ve Kapat</button>");
        html += F(
            "<script>"
            // setMode/setRate artik yaniti optimistik olarak goster-mez (RX'te
            // gercek uygulama TX'in ACK'ini bekleyen asenkron bir senkronizasyon
            // olabilir) - sadece istegi ates eder, gercek durum poll()'un
            // okudugu /status'tan (mode/rate + varsa sync_status) gelir.
            "function setMode(m){fetch('/set_mode?m='+m);document.getElementById('mode').innerText='(senkronize ediliyor...)';}"
            "function setRate(k){fetch('/set_rate?kbps='+k);document.getElementById('rate').innerText='(senkronize ediliyor...)';}"
            "function closeAp(){fetch('/save_close').then(()=>{document.body.innerHTML='<h1>Kaydedildi. AP kapaniyor...</h1>';});}"
            // Frontend'de min-uzunluk kontrolu (backend de ayrica dogrular,
            // bkz. _handleSetApPassword - frontend kontrolu sadece UX icin,
            // guvenlik sinirini backend cizer).
            "function setApPassword(){"
            "var pw=document.getElementById('apPwInput').value;"
            "var msg=document.getElementById('apPwMsg');"
            "if(pw.length<8){msg.innerText='En az 8 karakter olmali';return;}"
            "fetch('/set_ap_password',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:'pw='+encodeURIComponent(pw)})"
            ".then(r=>r.json()).then(d=>{"
            "msg.innerText=d.ok?'Kaydedildi - bir sonraki AP acilisinda gecerli':(d.error||'Hata');"
            "if(d.ok)document.getElementById('apPwInput').value='';"
            "}).catch(()=>{msg.innerText='Hata';});"
            "}"
            "function poll(){fetch('/status').then(r=>r.json()).then(d=>{"
            "document.getElementById('mode').innerText=d.mode;"
            "document.getElementById('cnt').innerText=d.packet_count;"
            "document.getElementById('age').innerText=(d.last_event_ms/1000).toFixed(1)+'s once';"
            "document.getElementById('paired').innerText=d.paired?'Bagli':'Bagli degil';"
            "document.getElementById('dot').className='dot '+(d.paired?'on':'off');"
            "document.getElementById('rate').innerText=d.air_rate_kbps+' kbps';"
            "var rs=document.getElementById('rateSel');if(document.activeElement!==rs)rs.value=d.air_rate_kbps;"
            "if(d.vbat_mv!==undefined){document.getElementById('vbatRow').style.display='flex';"
            "document.getElementById('vbat').innerText=(d.vbat_mv/1000).toFixed(2)+' V';}"
            "if(d.rssi_dbm!==undefined){document.getElementById('rssiRow').style.display='flex';"
            "document.getElementById('rssi').innerText=d.rssi_valid?(d.rssi_dbm+' dBm'):'ariyor...';}"
            "if(d.rx_lost_count!==undefined){document.getElementById('lostRow').style.display='flex';"
            "document.getElementById('lostcorrupt').innerText=d.rx_lost_count+' / '+d.rx_corrupt_count;}"
            "if(d.link_efficiency_pct!==undefined){document.getElementById('effRow').style.display='flex';"
            "document.getElementById('eff').innerText='%'+d.link_efficiency_pct;}"
            "if(d.sync_status!==undefined){document.getElementById('syncRow').style.display='flex';"
            "var ss=d.sync_status;var st=ss==='pending'?'Senkronize ediliyor...':ss==='ok'?'Senkronize edildi':"
            "ss==='failed'?'Basarisiz - TX yanit vermedi, TX menzilde mi kontrol edin':ss;"
            "document.getElementById('syncStatus').innerText=st;}"
            "}).catch(()=>{});}"
            "setInterval(poll,2000);poll();"
            "</script></body></html>");

        _server.send(200, "text/html", html);
    }

    // Yanit artik mode/rate'i ECHO ETMEZ: RX'te setMode() gercekte E22'yi
    // hemen degistirmeyebilir (once TX'e senkronizasyon istegi gonderir,
    // bkz. rx-node/lib/sync_master/sync_master.h) - gercek durum /status'tan
    // (mode + varsa sync_status) okunur, JS de artik buna gore calisiyor.
    void _handleSetMode() {
        _touchActivity();
        String m = _server.arg("m");
        BridgeMode mode = (m == "cal") ? BRIDGE_TRANSPARENT : BRIDGE_COMPRESSED;
        _cb.setMode(mode);
        _server.send(200, "application/json", "{\"ok\":true}");
    }

    void _handleSetRate() {
        _touchActivity();
        String kbps = _server.arg("kbps");
        AirRate rate;
        if (!air_rate_from_kbps_str(kbps, rate)) {
            _server.send(400, "application/json", "{\"ok\":false,\"error\":\"gecersiz kbps\"}");
            return;
        }
        _cb.setAirRate(rate);
        _server.send(200, "application/json", "{\"ok\":true}");
    }

    // Yeni AP sifresini NVS'e yazar - CANLI AP oturumunu etkilemez (WiFi.softAP
    // zaten eski sifreyle acik, degistirmek su an bagli olan kullaniciyi -
    // sifreyi degistirenin ta kendisini - koparirdi). Yeni sifre bir SONRAKI
    // AP acilisinda (_startAP()'in NVS'ten taze okumasiyla) gecerli olur.
    void _handleSetApPassword() {
        _touchActivity();
        String pw = _server.arg("pw");
        if (pw.length() < WEB_CONFIG_AP_PW_MIN_LEN) {
            _server.send(400, "application/json", "{\"ok\":false,\"error\":\"sifre en az 8 karakter olmali\"}");
            return;
        }

        Preferences prefs;
        if (prefs.begin(_cb.nvsNamespace, false)) {
            prefs.putString("ap_password", pw);
            prefs.end();
        }

        Serial.println("[WebConfig] Yeni AP sifresi NVS'e kaydedildi - bir sonraki AP acilisinda gecerli olacak");
        _server.send(200, "application/json", "{\"ok\":true}");
    }

    void _handleStatus() {
        _touchActivity();

        uint32_t age = _cb.getLastEventAgoMs();
        bool paired = age < WEB_CONFIG_PAIRED_WINDOW_MS;

        String json = "{";
        json += "\"mode\":\"";
        json += (_cb.getMode() == BRIDGE_TRANSPARENT) ? "KALIBRASYON" : "UZUN MENZIL";
        json += "\",";
        json += "\"air_rate_kbps\":\"";
        json += air_rate_kbps_str(_cb.getAirRate());
        json += "\",";
        json += "\"packet_count\":" + String(_cb.getPacketCount()) + ",";
        json += "\"last_event_ms\":" + String(age) + ",";
        json += "\"paired\":";
        json += paired ? "true" : "false";
        if (_cb.appendExtraStatusJson) _cb.appendExtraStatusJson(json);
        json += "}";

        _server.send(200, "application/json", json);
    }

    void _handleSaveClose() {
        _touchActivity();
        _persistMode(_cb.nvsNamespace, _cb.getMode());
        _persistAirRate(_cb.nvsNamespace, _cb.getAirRate());
        _server.send(200, "application/json", "{\"ok\":true}");

        Serial.println("[WebConfig] Mod+rate NVS'e kaydedildi, AP kapatiliyor");
        _closePending = true;
        _closeAtMs = millis() + WEB_CONFIG_CLOSE_DELAY_MS; // yanit client'a ulassin diye kisa gecikme (non-blocking)
    }

    void _touchActivity() { _lastActivityMs = millis(); }

    static void _persistMode(const char* ns, BridgeMode mode) {
        Preferences prefs;
        if (!prefs.begin(ns, false)) return;
        prefs.putUChar("mode", (uint8_t)mode); // NVS anahtarı "mode" — eski cihazlarla uyumlu
        prefs.end();
    }

    static void _persistAirRate(const char* ns, AirRate rate) {
        Preferences prefs;
        if (!prefs.begin(ns, false)) return;
        prefs.putUChar("rate", (uint8_t)rate);
        prefs.end();
    }

    WebConfigCallbacks _cb{};
    WebServer _server{80};
    DNSServer _dns;
    String    _apPassword;

    bool     _active         = false;
    bool     _pressing       = false;
    uint32_t _pressStartMs   = 0;
    uint32_t _lastActivityMs = 0;

    bool     _closePending = false;
    uint32_t _closeAtMs    = 0;
};
