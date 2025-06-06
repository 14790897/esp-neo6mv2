#include <Arduino.h>
#include <secrets.h>
// #include <SoftwareSerial.h>
#include <TinyGPS++.h> //TinyGPSPlus.h不行，而TinyGPS++.h可以
#include <WiFi.h>
#include <WebServer.h>
#include <LittleFS.h>
#include <DNSServer.h>
#include <ESPmDNS.h>
#ifdef USE_OLED_SCREEN
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Wire.h>
#define OLED_SCL 5
#define OLED_SDA 4
#endif
#ifdef USE_ST7735_SCREEN
#include <Adafruit_GFX.h>
#include <Adafruit_ST7735.h>
#include <SPI.h>
#define TFT_CS D8
#define TFT_RST D4
#define TFT_DC D3
Adafruit_ST7735 tft(TFT_CS, TFT_DC, TFT_RST);
#endif

// 定义 GPS 模块的 RX 和 TX 引脚（硬件串口）
#define RX_PIN 0
#define TX_PIN 1

// 创建 SoftwareSerial 和 TinyGPSPlus 对象
// SoftwareSerial gpsSerial(RX_PIN, TX_PIN);
// 定义为第个硬件串口
HardwareSerial gpsSerial(1); // 使用硬件串口1

TinyGPSPlus gps;
WebServer server(80);
DNSServer dnsServer; // 用于Captive Portal

#ifdef USE_OLED_SCREEN
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);
#endif

String logBuffer = "";
String lineBuffer = "";

// 码表相关变量
bool tripActive = false;
unsigned long tripStartTime = 0;
unsigned long tripEndTime = 0;
String tripFileName = "";

// WiFi 配置相关变量
#ifdef wifi_ssid
const char *wifiSsid = wifi_ssid;
const char *wifiPass = wifi_password;
bool wifiConfigured = true;
#else
String wifiSsid = "";
String wifiPass = "";
bool wifiConfigured = false;
#endif

// 新增：WiFi掉线AP切换相关变量
unsigned long wifiLostTime = 0;
bool apModeActive = false;
bool configModeActive = false;                   // 配置模式标志
bool wifiRetrying = false;                       // WiFi重连状态标志
unsigned long lastWifiRetryTime = 0;             // 上次尝试重连WiFi的时间
const unsigned long WIFI_RETRY_INTERVAL = 60000; // 每60秒尝试重连一次WiFi

// GPS数据超时检测变量
unsigned long lastGpsUpdateTime = 0;
const unsigned long GPS_TIMEOUT_MS = 10000; // 10秒超时

void addLog(const String &msg) {
  logBuffer += msg + "<br>";
  // 限制日志长度，避免内存溢出
  if (logBuffer.length() > 3000) {
    logBuffer = logBuffer.substring(logBuffer.length() - 3000);
  }
  Serial.println(msg);
}

void saveWifiConfig()
{
  File f = LittleFS.open("/wifi.txt", "w");
  if (f)
  {
    f.println(wifiSsid);
    f.println(wifiPass);
    f.close();
  }
}

String gpsDataInnerHtml()
{
  String html = "<div class='gps-data'>";
  html += "<h2>GPS 实时数据</h2>";

  // 检查GPS数据是否超时
  unsigned long currentTime = millis();
  bool gpsTimeout = (lastGpsUpdateTime > 0) && (currentTime - lastGpsUpdateTime > GPS_TIMEOUT_MS);

  if (gps.location.isValid() && !gpsTimeout)
  {
    html += "<p>纬度: <b>" + String(gps.location.lat(), 6) + "</b></p>";
    html += "<p>经度: <b>" + String(gps.location.lng(), 6) + "</b></p>";
    html += "<p>海拔: <b>" + String(gps.altitude.meters()) + " m</b></p>";
    html += "<p>速度: <b>" + String(gps.speed.kmph()) + " km/h</b></p>";
    // 显示最后更新时间
    unsigned long timeSinceUpdate = currentTime - lastGpsUpdateTime;
    html += "<p style='color:#666;'>最后更新: <b>" + String(timeSinceUpdate / 1000) + " 秒前</b></p>";
  }
  else if (gpsTimeout)
  {
    unsigned long timeSinceUpdate = currentTime - lastGpsUpdateTime;
    html += "<p style='color:#ff6600;'>⚠️ GPS数据超时 (" + String(timeSinceUpdate / 1000) + " 秒未更新)</p>";
    html += "<p style='color:#c00;'>请检查GPS模块连接或等待卫星信号...</p>";
  }
  else
  {
    html += "<p style='color:#c00;'>等待 GPS 定位数据...</p>";
  }
  html += "</div>";
  html += "<div class='log-title'>串口日志</div>";
  html += "<div class='log-box'>" + logBuffer + "</div>";
  return html;
}

void handleRoot() {
  File f = LittleFS.open("/index.html", "r");
  if (f)
  {
    String html = f.readString();
    server.send(200, "text/html", html);
    f.close();
  }
  else
  {
    server.send(500, "text/plain", "index.html not found in LittleFS");
  }
}

void handleStyle()
{
  File f = LittleFS.open("/style.css", "r");
  if (f)
  {
    String css = f.readString();
    server.send(200, "text/css", css);
    f.close();
  }
  else
  {
    server.send(404, "text/plain", "style.css not found");
  }
}

void handleScript()
{
  File f = LittleFS.open("/script.js", "r");
  if (f)
  {
    String js = f.readString();
    server.send(200, "application/javascript", js);
    f.close();
  }
  else
  {
    server.send(404, "text/plain", "script.js not found");
  }
}

void handleData()
{
  server.send(200, "text/html", gpsDataInnerHtml());
}

void handleStartTrip()
{
  addLog("[DEBUG] handleStartTrip() called");
  if (!tripActive)
  {
    tripActive = true;
    tripStartTime = millis();
    time_t now = time(nullptr);
    struct tm *tm_info = localtime(&now);
    char buf[32];
    strftime(buf, sizeof(buf), "/trip_%Y%m%d_%H%M%S.csv", tm_info);
    tripFileName = String(buf);
    File f = LittleFS.open(tripFileName, "w");
    if (f)
    {
      // 写入标准码表CSV表头
      f.println("timestamp,latitude,longitude,altitude,speed_kmph");
      f.close();
      addLog("[TRIP] Trip started: " + tripFileName);
    }
    else
    {
      addLog("[ERROR] Failed to create trip file");
    }
    server.send(200, "text/plain", "Trip started");
  }
  else
  {
    addLog("[DEBUG] handleStartTrip() called but tripActive already true");
    server.send(200, "text/plain", "Trip already started");
  }
}

void handleStopTrip()
{
  if (tripActive)
  {
    tripActive = false;
    tripEndTime = millis();
    addLog("[TRIP] Trip ended: " + tripFileName);
    server.send(200, "text/plain", "Trip stopped");
  }
  else
  {
    server.send(200, "text/plain", "Trip not active");
  }
}

void writePositionToFS(double lat, double lng, double alt, double speed)
{
  File f = LittleFS.open("/gpslog.txt", "a");
  if (f)
  {
    f.printf("%f,%f,%f,%f,%lu\n", lat, lng, alt, speed, millis());
    f.close();
  }
  else
  {
    addLog("[ERROR] Failed to open gpslog.txt for append");
  }
}

void writeTripData(double lat, double lng, double alt, double speed)
{
  if (tripActive && tripFileName.length() > 0)
  {
    File f = LittleFS.open(tripFileName, "a");
    if (f)
    {
      if (gps.location.isValid())
      {
        f.printf("%lu,%.6f,%.6f,%.2f,%.2f\n", millis(), lat, lng, alt, speed);
        addLog("[TRIP] Data written to " + tripFileName + ": " +
               String(lat, 6) + ", " + String(lng, 6) + ", " +
               String(alt, 2) + " m, " + String(speed, 2) + " km/h");
      }
      else
      {
        f.printf("%lu,,,,\n", millis()); // 只写时间，其他为空
        addLog("[TRIP] Invalid GPS data, only timestamp written to " + tripFileName);
      }
      f.close();
    }
  }
}

void handleWifiConfig()
{
  String html = "<html><head><meta charset='utf-8'><title>WiFi配置</title>";
  html += R"(
  <style>
    body { font-family: 'Segoe UI', Arial, sans-serif; background: #f4f6fa; }
    .container { max-width: 400px; margin: 40px auto; background: #fff; border-radius: 10px; box-shadow: 0 2px 8px #0001; padding: 24px; }
    h2 { color: #2a5d9f; }
    label { display:block; margin: 12px 0 4px 0; }
    input { width: 100%; padding: 8px; border-radius: 5px; border: 1px solid #bbb; margin-bottom: 12px; }
    button { background: #2a5d9f; color: #fff; border: none; border-radius: 5px; padding: 10px 22px; font-size: 1em; cursor: pointer; width: 100%; }
    button:hover { background: #17406b; }
    .msg { color: #c00; margin: 10px 0; }
  </style>
  )";
  html += "</head><body><div class='container'>";
  html += "<h2>WiFi 配置</h2>";
  html += "<form method='POST' action='/wifi_save'>";
  html += "<label>WiFi名称(SSID)</label><input name='ssid' required>";
  html += "<label>WiFi密码</label><input name='pass' type='password'>";
  html += "<button type='submit'>保存并连接</button>";
  html += "</form>";
  html += "</div></body></html>";
  server.send(200, "text/html", html);
}

void handleWifiSave()
{
#ifdef wifi_ssid
  // 宏模式下不允许网页配置WiFi
  server.send(403, "text/plain", "WiFi is hardcoded in firmware");
#else
  if (server.hasArg("ssid"))
    wifiSsid = server.arg("ssid");
  if (server.hasArg("pass"))
    wifiPass = server.arg("pass");
  wifiConfigured = true;
  saveWifiConfig(); // 保存WiFi配置到LittleFS
  String html = "<html><meta charset='utf-8'><body><h2>WiFi配置已保存，正在重启...</h2></body></html>";
  server.send(200, "text/html", html);
  delay(1000);
  ESP.restart();
#endif
}

void tryLoadWifiConfig()
{
#ifndef wifi_ssid
  File f = LittleFS.open("/wifi.txt", "r");
  if (f)
  {
    wifiSsid = f.readStringUntil('\n');
    wifiSsid.trim();
    wifiPass = f.readStringUntil('\n');
    wifiPass.trim();
    wifiConfigured = wifiSsid.length() > 0;
    f.close();
  }
#endif
}


#ifdef USE_OLED_SCREEN
void updateOled()
{
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);
  display.setCursor(0, 0); // 显示WiFi状态
  if (apModeActive)
  {
    if (wifiRetrying)
    {
      display.println("AP: Reconnecting...");
    }
    else
    {
      display.println("AP: GPS-AP-Data");
    }
    display.println("Visit: 192.168.4.1");
  }
  else if (configModeActive)
  {
    display.println("Config Mode");
    display.println("Visit: 192.168.4.1");
  }
  else if (WiFi.status() == WL_CONNECTED)
  {
    display.printf("WiFi: %s\n", WiFi.localIP().toString().c_str());
    display.println("Visit: esp32gps.local");
  }
  else
  {
    if (wifiLostTime > 0)
    {
      unsigned long disconnectTime = (millis() - wifiLostTime) / 1000;
      display.printf("WiFi lost: %lus\n", disconnectTime);
    }
    else
    {
      display.println("WiFi: Connecting...");
    }
  }

  // 检查GPS数据是否超时
  unsigned long currentTime = millis();
  bool gpsTimeout = (lastGpsUpdateTime > 0) && (currentTime - lastGpsUpdateTime > GPS_TIMEOUT_MS);
  if (gps.location.isValid() && !gpsTimeout)
  {
    // 第一行：简化的经纬度显示
    display.setTextSize(1);
    display.printf("%.4f,%.4f\n", gps.location.lat(), gps.location.lng());

    // 第二行：高度
    display.printf("Alt: %.0fm\n", gps.altitude.meters());

    // 第三、四行：大字体显示速度
    display.setTextSize(2);
    display.setCursor(0, 32);
    display.printf("%.1f", gps.speed.kmph());

    // 在速度数字右侧显示单位（小字体）
    display.setTextSize(1);
    display.setCursor(80, 40);
    display.println("km/h");
  }
  else if (gpsTimeout)
  {
    unsigned long timeSinceUpdate = currentTime - lastGpsUpdateTime;
    display.printf("GPS TIMEOUT!\n");
    display.printf("No update: %lus\n", timeSinceUpdate / 1000);
    display.println("Check connection");
  }
  else
  {
    display.println("wait for signal");
  }

  if (tripActive)
  {
    display.setTextSize(1);
    display.setCursor(0, 56);
    display.print("Trip: ON");
  }
  else
  {
    display.setTextSize(1);
    display.setCursor(0, 56);
    display.print("Trip: OFF");
  }
  display.display();
}
#endif
#ifdef USE_ST7735_SCREEN
void updateSt7735()
{
  tft.fillScreen(ST77XX_BLACK);
  tft.setTextColor(ST77XX_WHITE);
  tft.setTextSize(1);
  tft.setCursor(0, 0);

  // 显示WiFi状态
  if (apModeActive)
  {
    tft.setTextColor(ST77XX_YELLOW);
    tft.println("AP: GPS-AP-Data");
    tft.setTextColor(ST77XX_WHITE);
  }
  else if (WiFi.status() == WL_CONNECTED)
  {
    tft.setTextColor(ST77XX_GREEN);
    tft.printf("WiFi: %s\n", WiFi.localIP().toString().c_str());
    tft.setTextColor(ST77XX_WHITE);
  }
  else
  {
    tft.setTextColor(ST77XX_RED);
    if (wifiLostTime > 0)
    {
      unsigned long disconnectTime = (millis() - wifiLostTime) / 1000;
      tft.printf("WiFi 断开: %lus\n", disconnectTime);
    }
    else
    {
      tft.println("WiFi: 连接中...");
    }
    tft.setTextColor(ST77XX_WHITE);
  }

  // 检查GPS数据是否超时
  unsigned long currentTime = millis();
  bool gpsTimeout = (lastGpsUpdateTime > 0) && (currentTime - lastGpsUpdateTime > GPS_TIMEOUT_MS);

  if (gps.location.isValid() && !gpsTimeout)
  {
    tft.printf("Lat: %.6f\n", gps.location.lat());
    tft.printf("Lng: %.6f\n", gps.location.lng());
    tft.printf("Alt: %.1f m\n", gps.altitude.meters());
    tft.printf("Spd: %.1f km/h\n", gps.speed.kmph());
    // 显示最后更新时间
    unsigned long timeSinceUpdate = currentTime - lastGpsUpdateTime;
    tft.printf("更新: %lu秒前\n", timeSinceUpdate / 1000);
  }
  else if (gpsTimeout)
  {
    unsigned long timeSinceUpdate = currentTime - lastGpsUpdateTime;
    tft.setTextColor(ST77XX_RED);
    tft.printf("GPS超时!\n");
    tft.printf("未更新: %lu秒\n", timeSinceUpdate / 1000);
    tft.setTextColor(ST77XX_WHITE);
    tft.println("检查连接...");
  }
  else
  {
    tft.println("等待定位...");
  }

  if (tripActive)
  {
    tft.setCursor(0, 48);
    tft.print("码表: 进行中");
  }
  else
  {
    tft.setCursor(0, 48);
    tft.print("码表: 未开始");
  }
}
#endif

void handleDownloads()
{
  String html = "<div class='download-list'>";
  std::vector<String> files;
  File root = LittleFS.open("/", "r");
  if (root)
  {
    File file = root.openNextFile();
    while (file)
    {
      String name = file.name();
      if (!file.isDirectory())
      {
        files.push_back(name);
      }
      file = root.openNextFile();
    }
    root.close();
  }
  // 按文件名倒序排列（最新在前）
  std::sort(files.begin(), files.end(), std::greater<String>());
  for (const auto &name : files)
  {
    html += "<a href=\"/download?file=" + name + "\" download>" + name.substring(1) + "</a><br>";
  }
  if (files.empty())
  {
    html += "<div class='no-data'>暂无码表数据</div>";
  }
  html += "</div>";
  server.send(200, "text/html", html);
}

void handleDownloadFile()
{
  if (!server.hasArg("file"))
  {
    server.send(400, "text/plain", "Missing file param");
    return;
  }
  String fn = server.arg("file");
  // 兼容前端传递的文件名没有前导斜杠的情况
  if (!fn.startsWith("/"))
  {
    fn = "/" + fn;
  }
  File f = LittleFS.open(fn, "r");
  if (!f)
  {
    server.send(404, "text/plain", "File not found");
    return;
  }
  server.streamFile(f, "text/csv");
  f.close();
}

void listLittleFSFiles()
{
  Serial.println("LittleFS 文件列表:");
  File root = LittleFS.open("/", "r");
  if (root)
  {
    File file = root.openNextFile();
    while (file)
    {
      Serial.printf("  %s (%d bytes)\n", file.name(), file.size());
      file = root.openNextFile();
    }
    root.close();
  }
  else
  {
    Serial.println("无法打开 LittleFS 根目录");
  }
}

// --- 函数声明，解决 undefined 报错 ---
void tryLoadWifiConfig();
void handleWifiConfig();
void handleWifiSave();
void writePositionToFS(double lat, double lng, double alt, double speed);
void writeTripData(double lat, double lng, double alt, double speed);
void handleStartTrip();
void handleStopTrip();
void handleDownloads();
void handleDownloadFile();
void enterApMode();

void setup() {
  Serial.begin(115200);
  Serial.println("Booting...");
  gpsSerial.begin(9600, SERIAL_8N1, RX_PIN, TX_PIN);
  Serial.printf("[INFO] GPS UART1 started: RX=%d, TX=%d, baud=9600\n", RX_PIN, TX_PIN);
  if (!LittleFS.begin())
  {
    Serial.println("LittleFS mount failed");
    addLog("[ERROR] LittleFS mount failed");
  }
  tryLoadWifiConfig();

#ifdef USE_OLED_SCREEN
  Wire.begin(OLED_SDA, OLED_SCL); // 指定SDA和SCL引脚
  display.begin(SSD1306_SWITCHCAPVCC, 0x3C);
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.println("Booting...");
  display.display();
  delay(1000); // 显示启动信息1秒
#endif
#ifdef USE_ST7735_SCREEN
  tft.initR(INITR_BLACKTAB);
  tft.setRotation(1);
  tft.fillScreen(ST77XX_BLACK);
  tft.setTextColor(ST77XX_WHITE);
  tft.setTextSize(1);
  tft.setCursor(0, 0);
  tft.println("Booting...");
  delay(1000);
#endif

  // 智能WiFi连接逻辑：预配置模式也支持超时进入AP
  bool wifiConnected = false;
  unsigned long wifiConnectStart = millis();
  const unsigned long WIFI_CONNECT_TIMEOUT = 30000; // 30秒超时

#ifdef wifi_ssid
  WiFi.begin(wifiSsid, wifiPass);
  Serial.printf("[INFO] Trying to connect to predefined WiFi: %s\n", wifiSsid);
#else
  if (!wifiConfigured)
  {
    // 无配置，直接进入AP配置模式，跳过WiFi连接
    Serial.println("[INFO] No WiFi configuration, entering config mode");
    enterConfigMode();
    return; // 退出setup函数，不继续WiFi连接流程
  }
  WiFi.begin(wifiSsid.c_str(), wifiPass.c_str());
  Serial.printf("[INFO] Trying to connect to saved WiFi: %s\n", wifiSsid.c_str());
#endif
  // 等待WiFi连接，但有超时限制
  while (WiFi.status() != WL_CONNECTED && (millis() - wifiConnectStart < WIFI_CONNECT_TIMEOUT))
  {
    // 在等待WiFi连接时处理GPS数据
    while (gpsSerial.available() > 0)
    {
      if (gps.encode(gpsSerial.read()))
      {
        lastGpsUpdateTime = millis();
      }
    }

    delay(500); // 减少延迟，提高响应速度
    Serial.println("Connecting to WiFi...");

    // 在连接过程中显示GPS数据和连接状态
#ifdef USE_OLED_SCREEN
    display.clearDisplay();
    display.setTextColor(SSD1306_WHITE);
    display.setTextSize(1);
    display.setCursor(0, 0);
    display.println("Connecting WiFi...");
    display.printf("Time: %lu s\n", (millis() - wifiConnectStart) / 1000);
#ifdef wifi_ssid
    display.printf("SSID: %s\n", wifiSsid);
#else
    display.printf("SSID: %s\n", wifiSsid.c_str());
#endif

    // 显示GPS状态
    if (gps.location.isValid())
    {
      display.printf("GPS: %.6f,%.6f\n", gps.location.lat(), gps.location.lng());
    }
    else
    {
      display.println("GPS: Searching...");
    }

    if (gps.speed.isValid())
    {
      display.printf("Speed: %.1f km/h\n", gps.speed.kmph());
    }

    display.display();
#endif
  }

  if (WiFi.status() == WL_CONNECTED)
  {
    wifiConnected = true;
    Serial.println("[INFO] Connected to WiFi successfully");
    Serial.printf("[INFO] IP address: %s\n", WiFi.localIP().toString().c_str());
    addLog("[INFO] WiFi connected: " + WiFi.localIP().toString());

    // --- mDNS ---
    if (MDNS.begin("esp32gps"))
    {
      Serial.println("mDNS responder started: http://esp32gps.local/");
      addLog("[INFO] mDNS started: http://esp32gps.local/");
    }
    else
    {
      Serial.println("Error setting up mDNS responder!");
      addLog("[ERROR] mDNS failed");
    }
  }
  else
  {
    Serial.println("[WARN] WiFi connection timeout, entering AP mode");
    wifiConnected = false;
#ifdef wifi_ssid
    // 预配置模式下，WiFi连接失败后进入数据AP模式
    enterApMode();
#else
    // 无配置时，进入配置AP模式
    enterConfigMode();
#endif
  }
  // 只有在WiFi连接成功后才尝试时间同步
  if (wifiConnected)
  {
    configTime(8 * 3600, 0, "ntp.aliyun.com", "ntp1.aliyun.com", "pool.ntp.org");
  }

  server.on("/", HTTP_GET, handleRoot);
  server.on("/index.html", HTTP_GET, handleRoot);
  server.on("/style.css", HTTP_GET, handleStyle);
  server.on("/script.js", HTTP_GET, handleScript);
  server.on("/data", handleData);
  server.on("/start", HTTP_POST, handleStartTrip);
  server.on("/start", HTTP_GET, handleStartTrip); // 新增这一行
  server.on("/stop", HTTP_POST, handleStopTrip);
  server.on("/stop", HTTP_GET, handleStopTrip); // 新增这一行
  server.on("/downloads", handleDownloads);
  server.on("/download", handleDownloadFile);
  server.begin();
  Serial.println("HTTP server started");
  listLittleFSFiles(); // 启动后串口输出所有文件列表
}

void enterConfigMode()
{
  configModeActive = true;
  Serial.println("[CONFIG MODE] Starting AP for WiFi configuration");
  addLog("[CONFIG MODE] Starting AP for WiFi configuration");

#ifdef USE_OLED_SCREEN
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.println("Config Mode");
  display.println("Connect to:");
  display.println("ESP32-GPS-Config");
  display.println("192.168.4.1");
  display.display();
#endif
  WiFi.mode(WIFI_AP);
  WiFi.softAP("ESP32-GPS-Config");
  IPAddress apIP(192, 168, 4, 1);
  WiFi.softAPConfig(apIP, apIP, IPAddress(255, 255, 255, 0));

  // 启动DNS服务器，用于Captive Portal
  dnsServer.start(53, "*", apIP);

  // 设置路由
  server.on("/", HTTP_GET, handleWifiConfig); // 根路径直接显示WiFi配置页面
  server.on("/index.html", HTTP_GET, handleWifiConfig);
  server.on("/wifi_config", HTTP_GET, handleWifiConfig);
  server.on("/wifi_save", HTTP_POST, handleWifiSave);

  // Captive Portal支持 - 返回204状态码用于网络连接检测
  server.on("/generate_204", HTTP_GET, []()
            { server.send(204, "text/plain", ""); });
  server.on("/hotspot-detect.html", HTTP_GET, []()
            {
    server.sendHeader("Location", "http://192.168.4.1/wifi_config", true);
    server.send(302, "text/plain", ""); });
  server.on("/canonical.html", HTTP_GET, []()
            {
    server.sendHeader("Location", "http://192.168.4.1/wifi_config", true);
    server.send(302, "text/plain", ""); });

  // 通配符路由 - 捕获所有其他请求并重定向到配置页面
  server.onNotFound([]()
                    {
    server.sendHeader("Location", "http://192.168.4.1/wifi_config", true);
    server.send(302, "text/plain", ""); });

  server.begin();

  Serial.println("[CONFIG MODE] AP started: SSID=ESP32-GPS-Config, IP=192.168.4.1");
  Serial.println("[CONFIG MODE] Captive Portal active - all requests redirect to WiFi config");
  addLog("[CONFIG MODE] AP started: SSID=ESP32-GPS-Config, IP=192.168.4.1");
  addLog("[CONFIG MODE] Captive Portal active");
}

void enterApMode()
{
  if (apModeActive)
    return;
  apModeActive = true;
  WiFi.disconnect();
  WiFi.mode(WIFI_AP);
  WiFi.softAP("GPS-AP-Data");
  IPAddress apIP(192, 168, 4, 1);
  WiFi.softAPConfig(apIP, apIP, IPAddress(255, 255, 255, 0));
  server.close();
  server.stop();
  delay(100);
  // 重新设置路由并启动server
  server.on("/", HTTP_GET, handleRoot);
  server.on("/index.html", HTTP_GET, handleRoot);
  server.on("/style.css", HTTP_GET, handleStyle);
  server.on("/script.js", HTTP_GET, handleScript);
  server.on("/data", handleData);
  server.on("/downloads", handleDownloads);
  server.on("/download", handleDownloadFile);
  server.begin();
  Serial.println("[AP MODE] Started AP for data access: SSID=GPS-AP-Data");
  addLog("[AP MODE] Started AP for data access: SSID=GPS-AP-Data");
}

void loop()
{ // 每10秒输出一次调试日志，减少日志频率
  static unsigned long lastDebug = 0;
  if (millis() - lastDebug > 10000)
  {
    addLog("[DEBUG] loop running, waiting for GPS data...");
    lastDebug = millis();
  }

  // 检查是否有来自 GPS 模块的数据
  if (gpsSerial.available() > 0) {
    char c = gpsSerial.read();
    Serial.write(c); // 打印所有GPS原始数据，便于调试
    gps.encode(c);  // 解码接收到的 GPS 数据

    if (c == '\n')
    {
      addLog(lineBuffer);
      lineBuffer = "";
    }
    else if (c != '\r')
    {
      lineBuffer += c;
    } // 如果 GPS 数据更新了，打印相关信息
    if (gps.location.isUpdated()) {
      lastGpsUpdateTime = millis(); // 记录GPS数据更新时间
      String logMsg = "Latitude= " + String(gps.location.lat(), 6) +
                      " Longitude= " + String(gps.location.lng(), 6) +
                      " Altitude= " + String(gps.altitude.meters()) +
                      " Speed= " + String(gps.speed.kmph());
      Serial.println(logMsg);
      addLog(logMsg);
      // 写入LittleFS
      writePositionToFS(gps.location.lat(), gps.location.lng(), gps.altitude.meters(), gps.speed.kmph());
      writeTripData(gps.location.lat(), gps.location.lng(), gps.altitude.meters(), gps.speed.kmph());
    }
  }

  static unsigned long lastTripWrite = 0;
  if (tripActive)
  {
    if (millis() - lastTripWrite > 1000)
    {
      lastTripWrite = millis();
      double lat = gps.location.isValid() ? gps.location.lat() : 0.0;
      double lng = gps.location.isValid() ? gps.location.lng() : 0.0;
      double alt = gps.location.isValid() ? gps.altitude.meters() : 0.0;
      double spd = gps.location.isValid() ? gps.speed.kmph() : 0.0;
      writeTripData(lat, lng, alt, spd);
    }
  }
  // WiFi掉线检测与AP切换
  if (!apModeActive)
  {
    if (WiFi.status() != WL_CONNECTED)
    {
      if (wifiLostTime == 0)
      {
        wifiLostTime = millis();
        Serial.println("[WARN] WiFi connection lost, starting timer...");
        addLog("[WARN] WiFi connection lost");
      }
      else if (millis() - wifiLostTime > 30000)
      { // 30秒无网
        Serial.println("[WARN] WiFi disconnected for 30s, entering AP mode");
        enterApMode();
      }
    }
    else
    {
      // WiFi重新连接成功
      if (wifiLostTime > 0)
      {
        Serial.println("[INFO] WiFi reconnected successfully");
        addLog("[INFO] WiFi reconnected: " + WiFi.localIP().toString());
      }
      wifiLostTime = 0;
    }
  }
  else
  {
    // 在AP模式下，定期尝试重连原WiFi网络
    if (millis() - lastWifiRetryTime > WIFI_RETRY_INTERVAL)
    {
      lastWifiRetryTime = millis();
      wifiRetrying = true; // 设置重连状态标志
      Serial.println("[INFO] Attempting to reconnect to WiFi from AP mode...");
      addLog("[INFO] Attempting WiFi reconnection...");
      // 临时切换到Station+AP模式尝试连接
      WiFi.mode(WIFI_AP_STA);
      WiFi.begin(wifi_ssid, wifi_password);

      // 等待连接最多15秒
      unsigned long connectStart = millis();
      while (WiFi.status() != WL_CONNECTED && millis() - connectStart < 15000)
      {
        delay(500);
        Serial.print(".");

        // 在等待期间继续处理GPS数据
        if (gpsSerial.available() > 0)
        {
          char c = gpsSerial.read();
          gps.encode(c);
        }
      }

      wifiRetrying = false; // 清除重连状态标志

      if (WiFi.status() == WL_CONNECTED)
      {
        Serial.println();
        Serial.println("[INFO] WiFi reconnected successfully, exiting AP mode");
        addLog("[INFO] WiFi recovered: " + WiFi.localIP().toString());

        // 成功连接，退出AP模式
        apModeActive = false;
        wifiLostTime = 0;
        WiFi.mode(WIFI_STA); // 切换回纯Station模式

        // 重启server和mDNS服务
        server.close();
        server.stop();
        delay(100);

        // 重新设置路由
        server.on("/", HTTP_GET, handleRoot);
        server.on("/index.html", HTTP_GET, handleRoot);
        server.on("/style.css", HTTP_GET, handleStyle);
        server.on("/script.js", HTTP_GET, handleScript);
        server.on("/data", handleData);
        server.on("/start", HTTP_POST, handleStartTrip);
        server.on("/start", HTTP_GET, handleStartTrip);
        server.on("/stop", HTTP_POST, handleStopTrip);
        server.on("/stop", HTTP_GET, handleStopTrip);
        server.on("/downloads", handleDownloads);
        server.on("/download", handleDownloadFile);
        server.begin();

        // 重启mDNS服务
        MDNS.end();
        if (MDNS.begin("esp32gps"))
        {
          Serial.println("[INFO] mDNS restarted after WiFi recovery");
          addLog("[INFO] mDNS restarted");
        }
      }
      else
      {
        Serial.println();
        Serial.println("[INFO] WiFi reconnection failed, staying in AP mode");
        addLog("[INFO] WiFi reconnection failed");

        // 连接失败，切换回纯AP模式
        WiFi.mode(WIFI_AP);
      }
    }
  }

  // 屏幕更新限制频率，避免过于频繁
  static unsigned long lastScreenUpdate = 0;
  if (millis() - lastScreenUpdate > 100) // 每100ms更新一次屏幕
  {
    lastScreenUpdate = millis();
#ifdef USE_OLED_SCREEN
    updateOled();
#endif
#ifdef USE_ST7735_SCREEN
    updateSt7735();
#endif
  }
  server.handleClient();
  if (configModeActive)
  {
    dnsServer.processNextRequest(); // 处理DNS请求，用于Captive Portal
  }
  delay(1); // 减少延迟，提高响应速度
}
