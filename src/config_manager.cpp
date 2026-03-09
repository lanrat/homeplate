#include <WiFiManager.h>
#include <Preferences.h>
#include <qrcode.h>
#include "homeplate.h"

void renderQR(QRCode qrcode, uint32_t x, uint32_t y, uint32_t size);

HomePlateConfig plateCfg;

static Preferences preferences;
static const char *NVS_NAMESPACE = "homeplate";

// ---- NVS helpers ----

static void loadString(const char *key, char *dest, size_t destSize, const char *defaultVal)
{
    String val = preferences.getString(key, defaultVal);
    strlcpy(dest, val.c_str(), destSize);
}

static void saveString(const char *key, const char *value)
{
    preferences.putString(key, value);
}

// ---- Config logging ----

static const char *maskValue(const char *value)
{
    static char masked[8];
    if (strlen(value) == 0)
        return "(empty)";
    snprintf(masked, sizeof(masked), "***(%d)", (int)strlen(value));
    return masked;
}

void logConfig()
{
    Serial.println("[CONFIG] === Current Configuration ===");

    // Network
    Serial.printf("[CONFIG]   hostname          = %s\n", plateCfg.hostname);
    Serial.printf("[CONFIG]   staticIp          = %s\n", plateCfg.staticIp);
    Serial.printf("[CONFIG]   staticSubnet      = %s\n", plateCfg.staticSubnet);
    Serial.printf("[CONFIG]   staticGateway     = %s\n", plateCfg.staticGateway);
    Serial.printf("[CONFIG]   staticDns         = %s\n", plateCfg.staticDns);

    // NTP & Time
    Serial.printf("[CONFIG]   ntpServer         = %s\n", plateCfg.ntpServer);
    Serial.printf("[CONFIG]   timezone          = %s\n", plateCfg.timezone);

    // Sleep
    Serial.printf("[CONFIG]   sleepMinutes      = %d\n", plateCfg.sleepMinutes);
    Serial.printf("[CONFIG]   quickSleepSec     = %d\n", plateCfg.quickSleepSec);

    // Content
    Serial.printf("[CONFIG]   imageUrl          = %s\n", plateCfg.imageUrl);
    Serial.printf("[CONFIG]   defaultActivity   = %s\n", plateCfg.defaultActivityStr);

    // TRMNL
    Serial.printf("[CONFIG]   trmnlUrl          = %s\n", plateCfg.trmnlUrl);
    Serial.printf("[CONFIG]   trmnlId           = %s\n", plateCfg.trmnlId);
    Serial.printf("[CONFIG]   trmnlToken        = %s\n", maskValue(plateCfg.trmnlToken));
    Serial.printf("[CONFIG]   trmnlEnableLog    = %s\n", plateCfg.trmnlEnableLog ? "true" : "false");

    // Guest WiFi QR
    Serial.printf("[CONFIG]   qrWifiName        = %s\n", plateCfg.qrWifiName);
    Serial.printf("[CONFIG]   qrWifiPassword    = %s\n", maskValue(plateCfg.qrWifiPassword));

    // MQTT
    Serial.printf("[CONFIG]   mqttHost          = %s\n", plateCfg.mqttHost);
    Serial.printf("[CONFIG]   mqttPort          = %d\n", plateCfg.mqttPort);
    Serial.printf("[CONFIG]   mqttUser          = %s\n", plateCfg.mqttUser);
    Serial.printf("[CONFIG]   mqttPassword      = %s\n", maskValue(plateCfg.mqttPassword));
    Serial.printf("[CONFIG]   mqttNodeId        = %s\n", plateCfg.mqttNodeId);
    Serial.printf("[CONFIG]   mqttDeviceName    = %s\n", plateCfg.mqttDeviceName);
    Serial.printf("[CONFIG]   mqttExpireAfterSec= %u\n", plateCfg.mqttExpireAfterSec);

    // Display & OTA
    Serial.printf("[CONFIG]   displayLastUpdate = %s\n", plateCfg.displayLastUpdateTime ? "true" : "false");
    Serial.printf("[CONFIG]   enableOta         = %s\n", plateCfg.enableOta ? "true" : "false");

    // Internal
    Serial.printf("[CONFIG]   configured        = %s\n", plateCfg.configured ? "true" : "false");
    Serial.println("[CONFIG] === End Configuration ===");
}

// ---- Config load/save ----

void loadConfig()
{
    // Initialize with compile-time defaults
    strlcpy(plateCfg.hostname, HOSTNAME, sizeof(plateCfg.hostname));
    strlcpy(plateCfg.staticIp, STATIC_IP, sizeof(plateCfg.staticIp));
    strlcpy(plateCfg.staticSubnet, STATIC_SUBNET, sizeof(plateCfg.staticSubnet));
    strlcpy(plateCfg.staticGateway, STATIC_GATEWAY, sizeof(plateCfg.staticGateway));
    strlcpy(plateCfg.staticDns, STATIC_DNS, sizeof(plateCfg.staticDns));
    strlcpy(plateCfg.ntpServer, NTP_SERVER, sizeof(plateCfg.ntpServer));
    strlcpy(plateCfg.timezone, TIMEZONE, sizeof(plateCfg.timezone));
    plateCfg.sleepMinutes = TIME_TO_SLEEP_MIN;
    plateCfg.quickSleepSec = TIME_TO_QUICK_SLEEP_SEC;
    strlcpy(plateCfg.imageUrl, IMAGE_URL, sizeof(plateCfg.imageUrl));
    strlcpy(plateCfg.defaultActivityStr, DEFAULT_ACTIVITY_STR, sizeof(plateCfg.defaultActivityStr));
    strlcpy(plateCfg.trmnlUrl, TRMNL_URL, sizeof(plateCfg.trmnlUrl));
    strlcpy(plateCfg.trmnlId, TRMNL_ID, sizeof(plateCfg.trmnlId));
    strlcpy(plateCfg.trmnlToken, TRMNL_TOKEN, sizeof(plateCfg.trmnlToken));
    plateCfg.trmnlEnableLog = TRMNL_ENABLE_LOG_DEFAULT;
    strlcpy(plateCfg.qrWifiName, QR_WIFI_NAME, sizeof(plateCfg.qrWifiName));
    strlcpy(plateCfg.qrWifiPassword, QR_WIFI_PASSWORD, sizeof(plateCfg.qrWifiPassword));
    strlcpy(plateCfg.mqttHost, MQTT_HOST, sizeof(plateCfg.mqttHost));
    plateCfg.mqttPort = MQTT_PORT;
    strlcpy(plateCfg.mqttUser, MQTT_USER, sizeof(plateCfg.mqttUser));
    strlcpy(plateCfg.mqttPassword, MQTT_PASSWORD, sizeof(plateCfg.mqttPassword));
    strlcpy(plateCfg.mqttNodeId, MQTT_NODE_ID, sizeof(plateCfg.mqttNodeId));
    strlcpy(plateCfg.mqttDeviceName, MQTT_DEVICE_NAME, sizeof(plateCfg.mqttDeviceName));
    plateCfg.mqttExpireAfterSec = MQTT_EXPIRE_AFTER_SEC;
    plateCfg.displayLastUpdateTime = DISPLAY_LAST_UPDATE_TIME;
    plateCfg.enableOta = ENABLE_OTA;

    // Load from NVS (overrides compile-time defaults)
    preferences.begin(NVS_NAMESPACE, true); // read-only

    loadString("hostname", plateCfg.hostname, sizeof(plateCfg.hostname), plateCfg.hostname);
    loadString("static_ip", plateCfg.staticIp, sizeof(plateCfg.staticIp), plateCfg.staticIp);
    loadString("static_sub", plateCfg.staticSubnet, sizeof(plateCfg.staticSubnet), plateCfg.staticSubnet);
    loadString("static_gw", plateCfg.staticGateway, sizeof(plateCfg.staticGateway), plateCfg.staticGateway);
    loadString("static_dns", plateCfg.staticDns, sizeof(plateCfg.staticDns), plateCfg.staticDns);
    loadString("ntp_server", plateCfg.ntpServer, sizeof(plateCfg.ntpServer), plateCfg.ntpServer);
    loadString("timezone", plateCfg.timezone, sizeof(plateCfg.timezone), plateCfg.timezone);
    plateCfg.sleepMinutes = preferences.getUShort("sleep_min", plateCfg.sleepMinutes);
    plateCfg.quickSleepSec = preferences.getUShort("quick_sleep", plateCfg.quickSleepSec);
    loadString("image_url", plateCfg.imageUrl, sizeof(plateCfg.imageUrl), plateCfg.imageUrl);
    loadString("def_activity", plateCfg.defaultActivityStr, sizeof(plateCfg.defaultActivityStr), plateCfg.defaultActivityStr);
    loadString("trmnl_url", plateCfg.trmnlUrl, sizeof(plateCfg.trmnlUrl), plateCfg.trmnlUrl);
    loadString("trmnl_id", plateCfg.trmnlId, sizeof(plateCfg.trmnlId), plateCfg.trmnlId);
    loadString("trmnl_token", plateCfg.trmnlToken, sizeof(plateCfg.trmnlToken), plateCfg.trmnlToken);
    plateCfg.trmnlEnableLog = preferences.getBool("trmnl_log", plateCfg.trmnlEnableLog);
    loadString("qr_name", plateCfg.qrWifiName, sizeof(plateCfg.qrWifiName), plateCfg.qrWifiName);
    loadString("qr_pass", plateCfg.qrWifiPassword, sizeof(plateCfg.qrWifiPassword), plateCfg.qrWifiPassword);
    loadString("mqtt_host", plateCfg.mqttHost, sizeof(plateCfg.mqttHost), plateCfg.mqttHost);
    plateCfg.mqttPort = preferences.getUShort("mqtt_port", plateCfg.mqttPort);
    loadString("mqtt_user", plateCfg.mqttUser, sizeof(plateCfg.mqttUser), plateCfg.mqttUser);
    loadString("mqtt_pass", plateCfg.mqttPassword, sizeof(plateCfg.mqttPassword), plateCfg.mqttPassword);
    loadString("mqtt_nodeid", plateCfg.mqttNodeId, sizeof(plateCfg.mqttNodeId), plateCfg.mqttNodeId);
    loadString("mqtt_devname", plateCfg.mqttDeviceName, sizeof(plateCfg.mqttDeviceName), plateCfg.mqttDeviceName);
    plateCfg.mqttExpireAfterSec = preferences.getUInt("mqtt_expire", plateCfg.mqttExpireAfterSec);
    plateCfg.displayLastUpdateTime = preferences.getBool("disp_time", plateCfg.displayLastUpdateTime);
    plateCfg.enableOta = preferences.getBool("enable_ota", plateCfg.enableOta);

    plateCfg.configured = preferences.getBool("configured", false);

    preferences.end();

    // Compute derived defaults
    if (strlen(plateCfg.mqttNodeId) == 0)
    {
        strlcpy(plateCfg.mqttNodeId, plateCfg.hostname, sizeof(plateCfg.mqttNodeId));
    }
    if (plateCfg.sleepMinutes < 1)
    {
        plateCfg.sleepMinutes = 1;
    }
    if (plateCfg.mqttPort == 0)
    {
        plateCfg.mqttPort = 1883;
    }

    // Apply timezone
    setenv("TZ", plateCfg.timezone, 1);
    tzset();

    Serial.printf("[CONFIG] Loaded config: hostname=%s, tz=%s, sleep=%dmin\n",
                  plateCfg.hostname, plateCfg.timezone, plateCfg.sleepMinutes);

    if (!sleepBoot)
    {
        Serial.println("[CONFIG] Fresh boot detected, logging full config:");
        logConfig();
    }
}

void saveConfig()
{
    Serial.println("[CONFIG] Saving config to NVS:");
    logConfig();

    preferences.begin(NVS_NAMESPACE, false); // read-write

    saveString("hostname", plateCfg.hostname);
    saveString("static_ip", plateCfg.staticIp);
    saveString("static_sub", plateCfg.staticSubnet);
    saveString("static_gw", plateCfg.staticGateway);
    saveString("static_dns", plateCfg.staticDns);
    saveString("ntp_server", plateCfg.ntpServer);
    saveString("timezone", plateCfg.timezone);
    preferences.putUShort("sleep_min", plateCfg.sleepMinutes);
    preferences.putUShort("quick_sleep", plateCfg.quickSleepSec);
    saveString("image_url", plateCfg.imageUrl);
    saveString("def_activity", plateCfg.defaultActivityStr);
    saveString("trmnl_url", plateCfg.trmnlUrl);
    saveString("trmnl_id", plateCfg.trmnlId);
    saveString("trmnl_token", plateCfg.trmnlToken);
    preferences.putBool("trmnl_log", plateCfg.trmnlEnableLog);
    saveString("qr_name", plateCfg.qrWifiName);
    saveString("qr_pass", plateCfg.qrWifiPassword);
    saveString("mqtt_host", plateCfg.mqttHost);
    preferences.putUShort("mqtt_port", plateCfg.mqttPort);
    saveString("mqtt_user", plateCfg.mqttUser);
    saveString("mqtt_pass", plateCfg.mqttPassword);
    saveString("mqtt_nodeid", plateCfg.mqttNodeId);
    saveString("mqtt_devname", plateCfg.mqttDeviceName);
    preferences.putUInt("mqtt_expire", plateCfg.mqttExpireAfterSec);
    preferences.putBool("disp_time", plateCfg.displayLastUpdateTime);
    preferences.putBool("enable_ota", plateCfg.enableOta);
    preferences.putBool("configured", true);

    preferences.end();
    Serial.println("[CONFIG] Config saved to NVS");
}

uint32_t getMqttExpireAfterSec()
{
    if (plateCfg.mqttExpireAfterSec > 0)
        return plateCfg.mqttExpireAfterSec;
    return plateCfg.sleepMinutes * 60 * 2;
}

uint16_t getNtpSyncInterval()
{
    if (plateCfg.sleepMinutes <= 0)
        return 1;
    return (24 * 60) / plateCfg.sleepMinutes;
}

bool isConfigured()
{
    return plateCfg.configured;
}

// ---- WiFiManager ----

bool startWiFiManager(bool forcePortal)
{
    WiFiManager wm;

    wm.setConnectRetries(5);
    wm.setConnectTimeout(20);
    wm.setConfigPortalTimeout(15 * 60); // 15 minutes

    // Custom menu: GitHub link and version
    char menuHtml[256];
    snprintf(menuHtml, sizeof(menuHtml),
        "<br/><a href='https://github.com/lanrat/homeplate' target='_blank'>HomePlate on GitHub</a>"
        "<br/><small>Version: %s | %s (%s)</small>", VERSION, DEVICE_MODEL, CONFIG_IDF_TARGET);
    wm.setCustomMenuHTML(menuHtml);

    // Show config screen when AP starts
    wm.setAPCallback([](WiFiManager *myWm)
                     { displayConfigModeScreen(myWm->getConfigPortalSSID().c_str()); });

    // ---- Create custom parameters ----

    // Section: Network
    WiFiManagerParameter h_net("<hr><h3>Network</h3>");
    WiFiManagerParameter p_hostname("hostname", "Hostname", plateCfg.hostname, sizeof(plateCfg.hostname) - 1);
    WiFiManagerParameter p_sip("static_ip", "Static IP (blank=DHCP)", plateCfg.staticIp, sizeof(plateCfg.staticIp) - 1);
    WiFiManagerParameter p_ssub("static_sub", "Static Subnet", plateCfg.staticSubnet, sizeof(plateCfg.staticSubnet) - 1);
    WiFiManagerParameter p_sgw("static_gw", "Static Gateway", plateCfg.staticGateway, sizeof(plateCfg.staticGateway) - 1);
    WiFiManagerParameter p_sdns("static_dns", "Static DNS", plateCfg.staticDns, sizeof(plateCfg.staticDns) - 1);

    // Section: Time
    WiFiManagerParameter h_time("<hr><h3>Time</h3>");
    WiFiManagerParameter p_ntp("ntp_server", "NTP Server", plateCfg.ntpServer, sizeof(plateCfg.ntpServer) - 1);
    WiFiManagerParameter p_tz("timezone", "Timezone (<a href='https://github.com/nayarsystems/posix_tz_db/blob/master/zones.csv' target='_blank'>POSIX TZ list</a>)", plateCfg.timezone, sizeof(plateCfg.timezone) - 1);

    // Section: Sleep
    WiFiManagerParameter h_sleep("<hr><h3>Sleep</h3>");
    char sleepMinStr[8];
    snprintf(sleepMinStr, sizeof(sleepMinStr), "%d", plateCfg.sleepMinutes);
    WiFiManagerParameter p_sleep("sleep_min", "Sleep Minutes", sleepMinStr, 7);
    char quickSleepStr[8];
    snprintf(quickSleepStr, sizeof(quickSleepStr), "%d", plateCfg.quickSleepSec);
    WiFiManagerParameter p_qsleep("quick_sleep", "Quick Sleep Seconds", quickSleepStr, 7);

    // Section: Content
    WiFiManagerParameter h_content("<hr><h3>Content</h3>");
    WiFiManagerParameter p_imgurl("image_url", "Image URL", plateCfg.imageUrl, sizeof(plateCfg.imageUrl) - 1);
    WiFiManagerParameter p_activity("def_activity", "Default Activity (HomeAssistant/Trmnl/Info/GuestWifi)", plateCfg.defaultActivityStr, sizeof(plateCfg.defaultActivityStr) - 1);

    // Section: TRMNL
    WiFiManagerParameter h_trmnl("<hr><h3>TRMNL</h3>");
    WiFiManagerParameter p_turl("trmnl_url", "TRMNL URL", plateCfg.trmnlUrl, sizeof(plateCfg.trmnlUrl) - 1);
    WiFiManagerParameter p_tid("trmnl_id", "TRMNL ID", plateCfg.trmnlId, sizeof(plateCfg.trmnlId) - 1);
    WiFiManagerParameter p_ttoken("trmnl_token", "TRMNL Token", plateCfg.trmnlToken, sizeof(plateCfg.trmnlToken) - 1);
    WiFiManagerParameter p_tlog("trmnl_log", "TRMNL Logging", "T", 2, plateCfg.trmnlEnableLog ? "type=\"checkbox\" checked" : "type=\"checkbox\"");

    // Section: QR WiFi
    WiFiManagerParameter h_qr("<hr><h3>Guest WiFi QR Code</h3>");
    WiFiManagerParameter p_qrname("qr_name", "QR WiFi SSID", plateCfg.qrWifiName, sizeof(plateCfg.qrWifiName) - 1);
    WiFiManagerParameter p_qrpass("qr_pass", "QR WiFi Password", plateCfg.qrWifiPassword, sizeof(plateCfg.qrWifiPassword) - 1);

    // Section: MQTT
    WiFiManagerParameter h_mqtt("<hr><h3>MQTT</h3>");
    WiFiManagerParameter p_mqhost("mqtt_host", "MQTT Host (blank=disabled)", plateCfg.mqttHost, sizeof(plateCfg.mqttHost) - 1);
    char mqttPortStr[8];
    snprintf(mqttPortStr, sizeof(mqttPortStr), "%d", plateCfg.mqttPort);
    WiFiManagerParameter p_mqport("mqtt_port", "MQTT Port", mqttPortStr, 7);
    WiFiManagerParameter p_mquser("mqtt_user", "MQTT User", plateCfg.mqttUser, sizeof(plateCfg.mqttUser) - 1);
    WiFiManagerParameter p_mqpass("mqtt_pass", "MQTT Password", plateCfg.mqttPassword, sizeof(plateCfg.mqttPassword) - 1);
    WiFiManagerParameter p_mqnid("mqtt_nodeid", "MQTT Node ID (blank=hostname)", plateCfg.mqttNodeId, sizeof(plateCfg.mqttNodeId) - 1);
    WiFiManagerParameter p_mqdn("mqtt_devname", "MQTT Device Name", plateCfg.mqttDeviceName, sizeof(plateCfg.mqttDeviceName) - 1);
    char mqttExpStr[12];
    snprintf(mqttExpStr, sizeof(mqttExpStr), "%u", plateCfg.mqttExpireAfterSec);
    WiFiManagerParameter p_mqexp("mqtt_expire", "MQTT Expire After Sec (0=auto)", mqttExpStr, 11);

    // Section: Display & OTA
    WiFiManagerParameter h_disp("<hr><h3>Display &amp; OTA</h3>");
    WiFiManagerParameter p_dtime("disp_time", "Show Update Time", "T", 2, plateCfg.displayLastUpdateTime ? "type=\"checkbox\" checked" : "type=\"checkbox\"");
    WiFiManagerParameter p_ota("enable_ota", "Enable OTA", "T", 2, plateCfg.enableOta ? "type=\"checkbox\" checked" : "type=\"checkbox\"");

    // ---- Add all parameters ----
    wm.addParameter(&h_net);
    wm.addParameter(&p_hostname);
    wm.addParameter(&p_sip);
    wm.addParameter(&p_ssub);
    wm.addParameter(&p_sgw);
    wm.addParameter(&p_sdns);

    wm.addParameter(&h_time);
    wm.addParameter(&p_ntp);
    wm.addParameter(&p_tz);

    wm.addParameter(&h_sleep);
    wm.addParameter(&p_sleep);
    wm.addParameter(&p_qsleep);

    wm.addParameter(&h_content);
    wm.addParameter(&p_imgurl);
    wm.addParameter(&p_activity);

    wm.addParameter(&h_trmnl);
    wm.addParameter(&p_turl);
    wm.addParameter(&p_tid);
    wm.addParameter(&p_ttoken);
    wm.addParameter(&p_tlog);

    wm.addParameter(&h_qr);
    wm.addParameter(&p_qrname);
    wm.addParameter(&p_qrpass);

    wm.addParameter(&h_mqtt);
    wm.addParameter(&p_mqhost);
    wm.addParameter(&p_mqport);
    wm.addParameter(&p_mquser);
    wm.addParameter(&p_mqpass);
    wm.addParameter(&p_mqnid);
    wm.addParameter(&p_mqdn);
    wm.addParameter(&p_mqexp);

    wm.addParameter(&h_disp);
    wm.addParameter(&p_dtime);
    wm.addParameter(&p_ota);

    // ---- Save callback ----
    wm.setSaveParamsCallback([&]()
                             {
        strlcpy(plateCfg.hostname, p_hostname.getValue(), sizeof(plateCfg.hostname));
        strlcpy(plateCfg.staticIp, p_sip.getValue(), sizeof(plateCfg.staticIp));
        strlcpy(plateCfg.staticSubnet, p_ssub.getValue(), sizeof(plateCfg.staticSubnet));
        strlcpy(plateCfg.staticGateway, p_sgw.getValue(), sizeof(plateCfg.staticGateway));
        strlcpy(plateCfg.staticDns, p_sdns.getValue(), sizeof(plateCfg.staticDns));
        strlcpy(plateCfg.ntpServer, p_ntp.getValue(), sizeof(plateCfg.ntpServer));
        strlcpy(plateCfg.timezone, p_tz.getValue(), sizeof(plateCfg.timezone));

        int sm = atoi(p_sleep.getValue());
        plateCfg.sleepMinutes = (sm > 0) ? sm : 1;
        plateCfg.quickSleepSec = atoi(p_qsleep.getValue());

        strlcpy(plateCfg.imageUrl, p_imgurl.getValue(), sizeof(plateCfg.imageUrl));
        strlcpy(plateCfg.defaultActivityStr, p_activity.getValue(), sizeof(plateCfg.defaultActivityStr));
        strlcpy(plateCfg.trmnlUrl, p_turl.getValue(), sizeof(plateCfg.trmnlUrl));
        strlcpy(plateCfg.trmnlId, p_tid.getValue(), sizeof(plateCfg.trmnlId));
        strlcpy(plateCfg.trmnlToken, p_ttoken.getValue(), sizeof(plateCfg.trmnlToken));
        plateCfg.trmnlEnableLog = (strncmp(p_tlog.getValue(), "T", 1) == 0);
        strlcpy(plateCfg.qrWifiName, p_qrname.getValue(), sizeof(plateCfg.qrWifiName));
        strlcpy(plateCfg.qrWifiPassword, p_qrpass.getValue(), sizeof(plateCfg.qrWifiPassword));
        strlcpy(plateCfg.mqttHost, p_mqhost.getValue(), sizeof(plateCfg.mqttHost));
        int mp = atoi(p_mqport.getValue());
        plateCfg.mqttPort = (mp > 0) ? mp : 1883;
        strlcpy(plateCfg.mqttUser, p_mquser.getValue(), sizeof(plateCfg.mqttUser));
        strlcpy(plateCfg.mqttPassword, p_mqpass.getValue(), sizeof(plateCfg.mqttPassword));
        strlcpy(plateCfg.mqttNodeId, p_mqnid.getValue(), sizeof(plateCfg.mqttNodeId));
        strlcpy(plateCfg.mqttDeviceName, p_mqdn.getValue(), sizeof(plateCfg.mqttDeviceName));
        plateCfg.mqttExpireAfterSec = atoi(p_mqexp.getValue());
        plateCfg.displayLastUpdateTime = (strncmp(p_dtime.getValue(), "T", 1) == 0);
        plateCfg.enableOta = (strncmp(p_ota.getValue(), "T", 1) == 0);

        // Compute derived
        if (strlen(plateCfg.mqttNodeId) == 0)
            strlcpy(plateCfg.mqttNodeId, plateCfg.hostname, sizeof(plateCfg.mqttNodeId));

        // Apply timezone
        setenv("TZ", plateCfg.timezone, 1);
        tzset();

        saveConfig();
        Serial.println("[CONFIG] Configuration saved via WiFiManager, rebooting...");
        vTaskDelay(500 / portTICK_PERIOD_MS); // let serial flush
        ESP.restart(); });

    // ---- Try to connect ----
    bool connected;
    if (forcePortal)
    {
        Serial.println("[CONFIG] Forcing config portal...");
        // Connect WiFi first, then open portal for configuration
        WiFi.begin();
        connected = wm.startConfigPortal("HomePlate-Setup");
    }
    else
    {
        Serial.println("[CONFIG] Starting WiFiManager...");
        connected = wm.autoConnect("HomePlate-Setup");
    }

    if (connected)
    {
        Serial.println("[CONFIG] WiFi connected via WiFiManager");
    }
    else
    {
        Serial.println("[CONFIG] WiFiManager timeout - no connection");
    }

    return connected;
}

// ---- Config mode display ----

void displayConfigModeScreen(const char *apSsid)
{
    Serial.printf("[CONFIG] Config portal started, AP SSID: %s\n", apSsid);

    // Generate QR code for the open AP WiFi network
    char qrBuf[256];
    snprintf(qrBuf, sizeof(qrBuf), "WIFI:S:%s;T:nopass;;", apSsid);

    static uint8_t version = 5;
    QRCode qrcode;
    uint8_t qrcodeData[qrcode_getBufferSize(version)];
    qrcode_initText(&qrcode, qrcodeData, version, ECC_MEDIUM, qrBuf);
    uint32_t qrSize = 12;

    uint32_t qrY = (E_INK_HEIGHT - (qrcode.size * qrSize)) / 2;
    uint32_t qrX = E_INK_WIDTH - (qrcode.size * qrSize) - 100;

    i2cStart();
    displayStart();
    display.selectDisplayMode(INKPLATE_1BIT);
    display.setTextColor(BLACK, WHITE);
    display.clearDisplay();

    // Title
    display.setFont(&Roboto_64);
    display.setTextSize(1);
    centerTextX("HomePlate Setup", 0, E_INK_WIDTH, 100, false);

    // Instructions
    display.setFont(&Roboto_32);
    centerTextX("Connect to WiFi network:", 0, qrX - 50, 250, false);

    // AP SSID
    display.setFont(&Roboto_32);
    centerTextX(apSsid, 0, qrX - 50, 350, false);

    // Additional instructions
    display.setFont(&Roboto_16);
    centerTextX("Then open the configuration page", 0, qrX - 50, 450, false);
    centerTextX("to set up your HomePlate.", 0, qrX - 50, 480, false);
    centerTextX("Will sleep after 15 minutes", 0, qrX - 50, 540, false);
    centerTextX("if not configured.", 0, qrX - 50, 570, false);

    displayEnd();

    // Render QR code (renderQR takes/releases displayStart)
    renderQR(qrcode, qrX, qrY, qrSize);

    // Push to display (i2c already held from above)
    displayStart();
    display.display();
    displayEnd();
    i2cEnd();
}

void displayUnconfiguredScreen()
{
    i2cStart();
    displayStart();
    display.selectDisplayMode(INKPLATE_1BIT);
    display.setTextColor(BLACK, WHITE);
    display.clearDisplay();

    display.setFont(&Roboto_64);
    display.setTextSize(1);
    centerTextX("HomePlate", 0, E_INK_WIDTH, 300, false);

    display.setFont(&Roboto_32);
    centerTextX("Unconfigured - Sleeping", 0, E_INK_WIDTH, 420, false);

    display.setFont(&Roboto_16);
    centerTextX("Will retry on next wake cycle.", 0, E_INK_WIDTH, 500, false);

    display.display();
    displayEnd();
    i2cEnd();
}
