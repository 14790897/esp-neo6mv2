#include <Arduino.h>
#include <secrets.h>
#include <SoftwareSerial.h>
#include <TinyGPS++.h> //TinyGPSPlus.h不行，而TinyGPS++.h可以
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <LittleFS.h>
#include <DNSServer.h>
#ifdef USE_OLED_SCREEN
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
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

// 定义 GPS 模块的 RX 和 TX 引脚
#define RX_PIN 4   // D2 (GPIO4)
#define TX_PIN 5   // D1 (GPIO5)

// 创建 SoftwareSerial 和 TinyGPSPlus 对象
SoftwareSerial gpsSerial(RX_PIN, TX_PIN);
TinyGPSPlus gps;
ESP8266WebServer server(80);

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

void addLog(const String &msg) {
  logBuffer += msg + "<br>";
  // 限制日志长度，避免内存溢出
  if (logBuffer.length() > 3000) {
    logBuffer = logBuffer.substring(logBuffer.length() - 3000);
  }
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


String gpsDataHtml() {
  String html = "<html><head><meta charset='utf-8'><title>GPS Data</title>";
  html += R"(
  <style>
    body { font-family: 'Segoe UI', Arial, sans-serif; background: #f4f6fa; margin: 0; padding: 0; }
    .container { max-width: 600px; margin: 30px auto; background: #fff; border-radius: 10px; box-shadow: 0 2px 8px #0001; padding: 24px; }
    h2 { color: #2a5d9f; margin-top: 0; }
    .gps-data p { margin: 6px 0; font-size: 1.1em; }
    .log-title { margin: 18px 0 6px 0; color: #444; font-size: 1.1em; }
    .log-box { font-family:monospace; white-space:pre-wrap; border:1px solid #ccc; padding:8px; height:200px; overflow:auto; background:#f9f9f9; border-radius: 6px; }
    .btn-row { display: flex; gap: 12px; margin: 18px 0 0 0; }
    button { background: #2a5d9f; color: #fff; border: none; border-radius: 5px; padding: 10px 22px; font-size: 1em; cursor: pointer; transition: background 0.2s; }
    button:hover { background: #17406b; }
    .status { margin: 10px 0 0 0; color: #888; font-size: 0.98em; }
  </style>
  <script>
    function fetchData() {
      fetch('/data').then(r=>r.text()).then(html=>{
        document.getElementById('main').innerHTML = html;
      });
    }
    setInterval(fetchData, 2000);
    window.onload = fetchData;
  </script>
  )";
  html += "</head><body><div class='container'>";
  html += "<div id='main'>加载中...</div>";
  html += "<div class='btn-row'>";
  html += "<form method='POST' action='/start' style='display:inline;'><button type='submit'>开始码表</button></form>";
  html += "<form method='POST' action='/stop' style='display:inline;'><button type='submit'>结束码表</button></form>";
  html += "</div>";
  html += "<div class='status'>页面每2秒自动刷新</div>";
  html += "</div></body></html>";
  return html;
}

String gpsDataInnerHtml()
{
  String html = "<div class='gps-data'>";
  html += "<h2>GPS 实时数据</h2>";
  if (gps.location.isValid()) {
    html += "<p>纬度: <b>" + String(gps.location.lat(), 6) + "</b></p>";
    html += "<p>经度: <b>" + String(gps.location.lng(), 6) + "</b></p>";
    html += "<p>海拔: <b>" + String(gps.altitude.meters()) + " m</b></p>";
    html += "<p>速度: <b>" + String(gps.speed.kmph()) + " km/h</b></p>";
  } else {
    html += "<p style='color:#c00;'>等待 GPS 定位数据...</p>";
  }
  html += "</div>";
  html += "<div class='log-title'>串口日志</div>";
  html += "<div class='log-box'>" + logBuffer + "</div>";
  return html;
}

void handleRoot() {
  server.send(200, "text/html", gpsDataHtml());
}

void handleData()
{
  server.send(200, "text/html", gpsDataInnerHtml());
}

void handleStartTrip()
{
  if (!tripActive)
  {
    tripActive = true;
    tripStartTime = millis();
    tripFileName = "/trip_" + String(tripStartTime) + ".csv";
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
  }
  // 直接返回主页面，不跳转
  handleRoot();
}

void handleStopTrip()
{
  if (tripActive)
  {
    tripActive = false;
    tripEndTime = millis();
    addLog("[TRIP] Trip ended: " + tripFileName);
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
      f.printf("%lu,%.6f,%.6f,%.2f,%.2f\n", millis(), lat, lng, alt, speed);
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
}

void tryLoadWifiConfig()
{
#ifndef ssid
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
  display.setCursor(0, 0);
  display.println("GPS 码表");
  display.setTextSize(1);
  if (gps.location.isValid())
  {
    display.printf("Lat: %.6f\n", gps.location.lat());
    display.printf("Lng: %.6f\n", gps.location.lng());
    display.printf("Alt: %.1f m\n", gps.altitude.meters());
    display.printf("Spd: %.1f km/h\n", gps.speed.kmph());
  }
  else
  {
    display.println("等待定位...");
  }
  if (tripActive)
  {
    display.setTextSize(1);
    display.setCursor(0, 48);
    display.print("码表: 进行中");
  }
  else
  {
    display.setTextSize(1);
    display.setCursor(0, 48);
    display.print("码表: 未开始");
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
  tft.println("GPS 码表");
  if (gps.location.isValid())
  {
    tft.printf("Lat: %.6f\n", gps.location.lat());
    tft.printf("Lng: %.6f\n", gps.location.lng());
    tft.printf("Alt: %.1f m\n", gps.altitude.meters());
    tft.printf("Spd: %.1f km/h\n", gps.speed.kmph());
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
  // 直接返回主页面，嵌入下载列表
  String html = gpsDataHtml();
  // 插入下载列表到主页面底部
  String list = "<div class='container'><h2>码表数据下载</h2><ul>";
  Dir dir = LittleFS.openDir("/");
  bool found = false;
  while (dir.next())
  {
    String fn = dir.fileName();
    if (fn.startsWith("/trip_") && fn.endsWith(".csv"))
    {
      list += "<li><a href='/download?file=" + fn + "'>" + fn + "</a></li>";
      found = true;
    }
  }
  if (!found)
    list += "<li>暂无码表数据</li>";
  list += "</ul></div></body></html>";
  // 替换主页面结尾
  html.replace("</body></html>", list);
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
  if (!fn.startsWith("/trip_") || !fn.endsWith(".csv"))
  {
    server.send(403, "text/plain", "Forbidden");
    return;
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

void setup() {
  Serial.begin(115200);
  Serial.println("Booting...");
  if (!LittleFS.begin())
  {
    Serial.println("LittleFS mount failed");
    addLog("[ERROR] LittleFS mount failed");
  }
  tryLoadWifiConfig();
#ifdef ssid
  // secrets.h已定义，直接连接
  gpsSerial.begin(9600);
  WiFi.begin(wifiSsid, wifiPass);
#else
  DNSServer dnsServer;
  if (!wifiConfigured)
  {
    WiFi.mode(WIFI_AP);
    WiFi.softAP("GPS-Config"); // 无密码
    WiFi.softAPConfig(IPAddress(192, 168, 4, 1), IPAddress(192, 168, 4, 1), IPAddress(255, 255, 255, 0));
    dnsServer.start(53, "*", IPAddress(192, 168, 4, 1));
    server.on("/", handleWifiConfig);
    server.on("/wifi_save", HTTP_POST, handleWifiSave);
    // Captive Portal兼容：重定向常见探测路径
    server.on("/generate_204", handleWifiConfig);        // Android
    server.on("/fwlink", handleWifiConfig);              // Windows
    server.on("/ncsi.txt", handleWifiConfig);            // Windows
    server.on("/hotspot-detect.html", handleWifiConfig); // iOS/macOS
    server.onNotFound(handleWifiConfig);                 // 其它所有路径
    server.begin();
    Serial.println("WiFi config AP started");
    Serial.println("请在浏览器访问 http://192.168.4.1 进行WiFi配置");
    while (true)
    {
      dnsServer.processNextRequest();
      server.handleClient();
      delay(10);
    }
  }
  gpsSerial.begin(9600);
  WiFi.begin(wifiSsid.c_str(), wifiPass.c_str());
#endif
  // 等待 Wi-Fi 连接成功
  while (WiFi.status() != WL_CONNECTED) {
    delay(1000);
    Serial.println("Connecting to WiFi...");
  }
  Serial.println("Connected to WiFi");

#ifdef USE_OLED_SCREEN
  display.begin(SSD1306_SWITCHCAPVCC, 0x3C);
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.println("Booting...");
  display.display();
#endif
#ifdef USE_ST7735_SCREEN
  tft.initR(INITR_BLACKTAB);
  tft.setRotation(1);
  tft.fillScreen(ST77XX_BLACK);
  tft.setTextColor(ST77XX_WHITE);
  tft.setTextSize(1);
  tft.setCursor(0, 0);
  tft.println("Booting...");
#endif

  server.on("/", HTTP_GET, handleRoot);
  server.on("/data", handleData);
  server.on("/start", HTTP_POST, handleStartTrip);
  server.on("/stop", HTTP_POST, handleStopTrip);
  server.on("/downloads", handleDownloads);
  server.on("/download", handleDownloadFile);
  server.begin();
  Serial.println("HTTP server started");
}

void loop() {
  // 每2秒输出一次调试日志，表明主循环正常运行
  static unsigned long lastDebug = 0;
  if (millis() - lastDebug > 2000)
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
    }

    // 如果 GPS 数据更新了，打印相关信息
    if (gps.location.isUpdated()) {
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

#ifdef USE_OLED_SCREEN
  updateOled();
#endif
#ifdef USE_ST7735_SCREEN
  updateSt7735();
#endif
  server.handleClient();
  delay(10); // 避免看门狗复位
}
