// Ice Cream Machine Controller with WiFi Manager
#include <Arduino.h>
#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <SPIFFS.h>
#include <Preferences.h>

#define RELAY_MASTER 26
#define RELAY_COMPRESSOR 27
#define NTC_PIN 34

// Create AsyncWebServer object on port 80
AsyncWebServer server(80);

// Search for parameter in HTTP POST request
const char* PARAM_INPUT_1 = "ssid";
const char* PARAM_INPUT_2 = "pass";
const char* PARAM_INPUT_3 = "ip";
const char* PARAM_INPUT_4 = "gateway";

// Variables to save values from HTML form
String ssid;
String pass;
String ip;
String gateway;

// File paths to save input values permanently
const char* ssidPath = "/ssid.txt";
const char* passPath = "/pass.txt";
const char* ipPath = "/ip.txt";
const char* gatewayPath = "/gateway.txt";

IPAddress localIP;
IPAddress localGateway;
IPAddress subnet(255, 255, 255, 0);

// Timer variables
unsigned long previousMillis = 0;
const long interval = 10000;  // interval to wait for Wi-Fi connection (milliseconds)

// Ice cream controller variables
Preferences prefs;
bool use_timer = false;
unsigned long timer_duration_minutes = 0;
unsigned long compressor_start_time = 0;
bool compressor_active = false;

float readTemperatureC() {
  int adc = analogRead(NTC_PIN);
  float voltage = adc * 3.3 / 4095.0;
  float resistance = 10000.0 * (3.3 / voltage - 1); // 10k series resistor
  float B = 3435.0;
  float T0 = 298.15;
  float R0 = 10000.0;
  float tempK = 1.0 / (1.0 / T0 + log(resistance / R0) / B);
  return tempK - 273.15;
}

void startCompressor() {
  digitalWrite(RELAY_COMPRESSOR, HIGH);
  compressor_start_time = millis();
  compressor_active = true;
}

void stopCompressor() {
  digitalWrite(RELAY_COMPRESSOR, LOW);
  compressor_active = false;
}

void saveSettings() {
  prefs.putBool("use_timer", use_timer);
  prefs.putUInt("timer_minutes", timer_duration_minutes);
}

void loadSettings() {
  use_timer = prefs.getBool("use_timer", false);
  timer_duration_minutes = prefs.getUInt("timer_minutes", 0);
}

// Initialize SPIFFS
void initSPIFFS() {
  if (!SPIFFS.begin(true)) {
    Serial.println("An error has occurred while mounting SPIFFS");
  }
  Serial.println("SPIFFS mounted successfully");
}

// Read File from SPIFFS
String readFile(fs::FS &fs, const char * path) {
  Serial.printf("Reading file: %s\r\n", path);
  File file = fs.open(path);
  if(!file || file.isDirectory()) {
    Serial.println("- failed to open file for reading");
    return String();
  }
  
  String fileContent;
  while(file.available()) {
    fileContent = file.readStringUntil('\n');
    break;     
  }
  return fileContent;
}

// Write file to SPIFFS
void writeFile(fs::FS &fs, const char * path, const char * message) {
  Serial.printf("Writing file: %s\r\n", path);
  File file = fs.open(path, FILE_WRITE);
  if(!file) {
    Serial.println("- failed to open file for writing");
    return;
  }
  if(file.print(message)) {
    Serial.println("- file written");
  } else {
    Serial.println("- write failed");
  }
}

// Initialize WiFi
bool initWiFi() {
  if(ssid=="" || ip=="") {
    Serial.println("Undefined SSID or IP address.");
    return false;
  }

  WiFi.mode(WIFI_STA);
  localIP.fromString(ip.c_str());
  localGateway.fromString(gateway.c_str());

  if (!WiFi.config(localIP, localGateway, subnet)) {
    Serial.println("STA Failed to configure");
    return false;
  }
  WiFi.begin(ssid.c_str(), pass.c_str());
  Serial.println("Connecting to WiFi...");

  unsigned long currentMillis = millis();
  previousMillis = currentMillis;

  while(WiFi.status() != WL_CONNECTED) {
    currentMillis = millis();
    if (currentMillis - previousMillis >= interval) {
      Serial.println("Failed to connect.");
      return false;
    }
  }

  Serial.println(WiFi.localIP());
  return true;
}

// Replaces placeholder with compressor state value
String processor(const String& var) {
  if(var == "TEMP") {
    return String(readTemperatureC(), 1);
  }
  if(var == "COMPRESSOR_STATE") {
    return compressor_active ? "ON" : "OFF";
  }
  if(var == "USE_TIMER") {
    return use_timer ? "checked" : "";
  }
  if(var == "TIMER_MINUTES") {
    return String(timer_duration_minutes);
  }
  return String();
}

void setup() {
  // Serial port for debugging purposes
  Serial.begin(115200);

  initSPIFFS();

  // Set GPIO pins
  pinMode(RELAY_MASTER, OUTPUT);
  pinMode(RELAY_COMPRESSOR, OUTPUT);
  digitalWrite(RELAY_MASTER, LOW);
  digitalWrite(RELAY_COMPRESSOR, LOW);
  analogReadResolution(12);
  
  // Load values saved in SPIFFS
  ssid = readFile(SPIFFS, ssidPath);
  pass = readFile(SPIFFS, passPath);
  ip = readFile(SPIFFS, ipPath);
  gateway = readFile(SPIFFS, gatewayPath);
  Serial.println(ssid);
  Serial.println(pass);
  Serial.println(ip);
  Serial.println(gateway);

  if(initWiFi()) {
    // Initialize preferences
    prefs.begin("icecream", false);
    loadSettings();

    // Route for root / web page
    server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
      request->send(SPIFFS, "/index.html", "text/html", false, processor);
    });
    server.serveStatic("/", SPIFFS, "/");
    
    // Ice cream controller routes
    server.on("/status", HTTP_GET, [](AsyncWebServerRequest *request) {
      float t = readTemperatureC();
      String json = "{";
      json += "\"temp\":" + String(t) + ",";
      json += "\"compressor\":" + String(compressor_active ? "true" : "false") + ",";
      json += "\"use_timer\":" + String(use_timer ? "true" : "false") + ",";
      json += "\"timer_minutes\":" + String(timer_duration_minutes);
      json += "}";
      request->send(200, "application/json", json);
    });

    server.on("/start", HTTP_POST, [](AsyncWebServerRequest *request) {
      if (request->hasParam("use_timer", true)) {
        use_timer = request->getParam("use_timer", true)->value() == "true";
      }
      if (request->hasParam("timer_minutes", true)) {
        timer_duration_minutes = request->getParam("timer_minutes", true)->value().toInt();
      }
      saveSettings();
      digitalWrite(RELAY_MASTER, HIGH);
      startCompressor();
      request->send(200, "text/plain", "Compressor started");
    });

    server.on("/stop", HTTP_POST, [](AsyncWebServerRequest *request) {
      digitalWrite(RELAY_MASTER, LOW);
      stopCompressor();
      request->send(200, "text/plain", "Compressor stopped");
    });

    server.on("/reset-wifi", HTTP_POST, [](AsyncWebServerRequest *request) {
      // Delete WiFi credentials files
      SPIFFS.remove(ssidPath);
      SPIFFS.remove(passPath);
      SPIFFS.remove(ipPath);
      SPIFFS.remove(gatewayPath);
      request->send(200, "text/plain", "WiFi settings reset. Device will restart.");
      delay(1000);
      ESP.restart();
    });

    server.begin();
  }
  else {
    // Connect to Wi-Fi network with SSID and password
    Serial.println("Setting AP (Access Point)");
    // NULL sets an open Access Point
    WiFi.softAP("IceCreamController", NULL);

    IPAddress IP = WiFi.softAPIP();
    Serial.print("AP IP address: ");
    Serial.println(IP); 

    // Web Server Root URL
    server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
      request->send(SPIFFS, "/wifimanager.html", "text/html");
    });
    
    server.serveStatic("/", SPIFFS, "/");
    
    server.on("/", HTTP_POST, [](AsyncWebServerRequest *request) {
      int params = request->params();
      for(int i=0;i<params;i++){
        const AsyncWebParameter* p = request->getParam(i);
        if(p->isPost()){
          // HTTP POST ssid value
          if (p->name() == PARAM_INPUT_1) {
            ssid = p->value().c_str();
            Serial.print("SSID set to: ");
            Serial.println(ssid);
            // Write file to save value
            writeFile(SPIFFS, ssidPath, ssid.c_str());
          }
          // HTTP POST pass value
          if (p->name() == PARAM_INPUT_2) {
            pass = p->value().c_str();
            Serial.print("Password set to: ");
            Serial.println(pass);
            // Write file to save value
            writeFile(SPIFFS, passPath, pass.c_str());
          }
          // HTTP POST ip value
          if (p->name() == PARAM_INPUT_3) {
            ip = p->value().c_str();
            Serial.print("IP Address set to: ");
            Serial.println(ip);
            // Write file to save value
            writeFile(SPIFFS, ipPath, ip.c_str());
          }
          // HTTP POST gateway value
          if (p->name() == PARAM_INPUT_4) {
            gateway = p->value().c_str();
            Serial.print("Gateway set to: ");
            Serial.println(gateway);
            // Write file to save value
            writeFile(SPIFFS, gatewayPath, gateway.c_str());
          }
        }
      }
      request->send(200, "text/plain", "Done. ESP will restart, connect to your router and go to IP address: " + ip);
      delay(3000);
      ESP.restart();
    });
    server.begin();
  }
}

void loop() {
  if (compressor_active && use_timer) {
    unsigned long elapsed = (millis() - compressor_start_time) / 60000;
    if (elapsed >= timer_duration_minutes) {
      stopCompressor();
    }
  }
}