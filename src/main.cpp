#include <Arduino.h>
#include <secrets.h>
#include <SoftwareSerial.h>
#include <TinyGPS++.h> //TinyGPSPlus.h不行，而TinyGPS++.h可以
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <LittleFS.h>

// 定义 GPS 模块的 RX 和 TX 引脚
#define RX_PIN 4   // D2 (GPIO4)
#define TX_PIN 5   // D1 (GPIO5)

// 创建 SoftwareSerial 和 TinyGPSPlus 对象
SoftwareSerial gpsSerial(RX_PIN, TX_PIN);
TinyGPSPlus gps;
ESP8266WebServer server(80);


String logBuffer = "";
String lineBuffer = "";

// 码表相关变量
bool tripActive = false;
unsigned long tripStartTime = 0;
unsigned long tripEndTime = 0;
String tripFileName = "";

void addLog(const String &msg) {
  logBuffer += msg + "<br>";
  // 限制日志长度，避免内存溢出
  if (logBuffer.length() > 3000) {
    logBuffer = logBuffer.substring(logBuffer.length() - 3000);
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

void setup() {
  Serial.begin(115200);
  Serial.println("Booting...");
  if (!LittleFS.begin())
  {
    Serial.println("LittleFS mount failed");
    addLog("[ERROR] LittleFS mount failed");
  }
  gpsSerial.begin(9600);         // 初始化 GPS 模块的串口通信
  WiFi.begin(ssid, password);    // 连接到 Wi-Fi 网络

  // 等待 Wi-Fi 连接成功
  while (WiFi.status() != WL_CONNECTED) {
    delay(1000);
    Serial.println("Connecting to WiFi...");
  }
  Serial.println("Connected to WiFi");

  server.on("/", handleRoot);
  server.on("/data", handleData);
  server.on("/start", HTTP_POST, handleStartTrip);
  server.on("/stop", HTTP_POST, handleStopTrip);
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

  server.handleClient();
  delay(10); // 避免看门狗复位
}
