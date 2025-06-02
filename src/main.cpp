#include <Arduino.h>
#include <secrets.h>
#include <SoftwareSerial.h>
#include <TinyGPSPlus.h>
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>

// 定义 GPS 模块的 RX 和 TX 引脚
#define RX_PIN 4   // D2 (GPIO4)
#define TX_PIN 5   // D1 (GPIO5)

// 创建 SoftwareSerial 和 TinyGPSPlus 对象
SoftwareSerial gpsSerial(RX_PIN, TX_PIN);
TinyGPSPlus gps;
ESP8266WebServer server(80);


String logBuffer = "";

void addLog(const String &msg) {
  logBuffer += msg + "<br>";
  // 限制日志长度，避免内存溢出
  if (logBuffer.length() > 3000) {
    logBuffer = logBuffer.substring(logBuffer.length() - 3000);
  }
}

String gpsDataHtml() {
  String html = "<html><head><meta charset='utf-8'><title>GPS Data</title></head><body>";
  html += "<h2>GPS 实时数据</h2>";
  if (gps.location.isValid()) {
    html += "<p>纬度: " + String(gps.location.lat(), 6) + "</p>";
    html += "<p>经度: " + String(gps.location.lng(), 6) + "</p>";
    html += "<p>海拔: " + String(gps.altitude.meters()) + " m</p>";
    html += "<p>速度: " + String(gps.speed.kmph()) + " km/h</p>";
  } else {
    html += "<p>等待 GPS 定位数据...</p>";
  }
  html += "<h3>串口日志</h3><div style='font-family:monospace;white-space:pre-wrap;border:1px solid #ccc;padding:8px;height:200px;overflow:auto;background:#f9f9f9;'>" + logBuffer + "</div>";
  html += "<script>setTimeout(()=>location.reload(),2000);</script>";
  html += "</body></html>";
  return html;
}

void handleRoot() {
  server.send(200, "text/html", gpsDataHtml());
}

void setup() {
  Serial.begin(115200);
  Serial.println("Booting...");
  gpsSerial.begin(9600);         // 初始化 GPS 模块的串口通信
  WiFi.begin(ssid, password);    // 连接到 Wi-Fi 网络

  // 等待 Wi-Fi 连接成功
  while (WiFi.status() != WL_CONNECTED) {
    delay(1000);
    Serial.println("Connecting to WiFi...");
  }
  Serial.println("Connected to WiFi");

  server.on("/", handleRoot);
  server.begin();
  Serial.println("HTTP server started");
}

void loop() {
  // 检查是否有来自 GPS 模块的数据
  if (gpsSerial.available() > 0) {
    char c = gpsSerial.read();
    Serial.write(c); // 打印所有GPS原始数据，便于调试
    gps.encode(c);  // 解码接收到的 GPS 数据
    addLog(String(c)); // 记录原始GPS数据到网页日志

    // 如果 GPS 数据更新了，打印相关信息
    if (gps.location.isUpdated()) {
      String logMsg = "Latitude= " + String(gps.location.lat(), 6) +
                      " Longitude= " + String(gps.location.lng(), 6) +
                      " Altitude= " + String(gps.altitude.meters()) +
                      " Speed= " + String(gps.speed.kmph());
      Serial.println(logMsg);
      addLog(logMsg);
    }
  }

  server.handleClient();
  delay(10); // 避免看门狗复位
}
