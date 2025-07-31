// Ice Cream Machine Controller with Captive Portal
#include <Arduino.h>
#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <SPIFFS.h>
#include <Preferences.h>
#include <DNSServer.h>
#include <ArduinoOTA.h>

#define RELAY_MASTER 16
#define RELAY_COMPRESSOR 17
#define NTC_PIN 34

// Create AsyncWebServer object on port 80
AsyncWebServer server(80);

// DNS server for captive portal
DNSServer dnsServer;
const byte DNS_PORT = 53;

// Ice cream controller variables
Preferences prefs;
bool use_timer = false;
unsigned long timer_duration_minutes = 0;
unsigned long compressor_start_time = 0;
bool compressor_active = false;

// Temperature tracking variables
float target_temperature = -5.0; // Default target temperature in Celsius
struct TempReading {
  unsigned long timestamp;
  float temperature;
};
const int MAX_TEMP_READINGS = 100; // Store last 100 readings
TempReading tempHistory[MAX_TEMP_READINGS];
int tempHistoryIndex = 0;
bool tempHistoryFull = false;
unsigned long lastTempReading = 0;
const unsigned long TEMP_READING_INTERVAL = 30000; // Read every 30 seconds

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

void addTemperatureReading(float temp) {
  unsigned long now = millis();
  tempHistory[tempHistoryIndex].timestamp = now;
  tempHistory[tempHistoryIndex].temperature = temp;
  
  tempHistoryIndex = (tempHistoryIndex + 1) % MAX_TEMP_READINGS;
  if (tempHistoryIndex == 0) {
    tempHistoryFull = true;
  }
}

int getTemperatureHistoryCount() {
  return tempHistoryFull ? MAX_TEMP_READINGS : tempHistoryIndex;
}

TempReading getTemperatureReading(int index) {
  if (tempHistoryFull) {
    int actualIndex = (tempHistoryIndex + index) % MAX_TEMP_READINGS;
    return tempHistory[actualIndex];
  } else {
    return tempHistory[index];
  }
}

// Calculate estimated time to reach target temperature (in minutes)
int estimateTimeToTarget() {
  int count = getTemperatureHistoryCount();
  if (count < 3) return -1; // Need at least 3 readings for trend
  
  // Get recent readings for trend calculation
  int recentCount = min(10, count); // Use last 10 readings or all available
  float sumX = 0, sumY = 0, sumXY = 0, sumX2 = 0;
  
  for (int i = count - recentCount; i < count; i++) {
    TempReading reading = getTemperatureReading(i);
    float x = (reading.timestamp / 1000.0) / 60.0; // Convert to minutes
    float y = reading.temperature;
    
    sumX += x;
    sumY += y;
    sumXY += x * y;
    sumX2 += x * x;
  }
  
  // Linear regression to find temperature change rate
  float slope = (recentCount * sumXY - sumX * sumY) / (recentCount * sumX2 - sumX * sumX);
  float intercept = (sumY - slope * sumX) / recentCount;
  
  // If slope is near zero or positive (heating up), can't reach target
  if (slope >= -0.01) return -1;
  
  // Calculate current time and temperature
  TempReading latest = getTemperatureReading(count - 1);
  float currentTime = (latest.timestamp / 1000.0) / 60.0;
  float currentTemp = latest.temperature;
  
  // Solve for time when temperature reaches target
  // target = slope * time + intercept
  float targetTime = (target_temperature - intercept) / slope;
  int minutesToTarget = (int)(targetTime - currentTime);
  
  return minutesToTarget > 0 ? minutesToTarget : -1;
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
  prefs.putFloat("target_temp", target_temperature);
}

void loadSettings() {
  use_timer = prefs.getBool("use_timer", false);
  timer_duration_minutes = prefs.getUInt("timer_minutes", 0);
  target_temperature = prefs.getFloat("target_temp", -5.0);
}

// Initialize SPIFFS
void initSPIFFS() {
  if (!SPIFFS.begin(true)) {
    Serial.println("An error has occurred while mounting SPIFFS");
  }
  Serial.println("SPIFFS mounted successfully");
}

// Setup Access Point with Captive Portal
void setupAccessPoint() {
  Serial.println("Setting up Access Point with Captive Portal");
  
  // More careful WiFi initialization to prevent crashes
  Serial.println("Stopping WiFi...");
  WiFi.mode(WIFI_OFF);
  delay(500);
  
  Serial.println("Setting AP mode...");
  WiFi.mode(WIFI_AP);
  delay(1000);  // Longer delay for mode change
  
  Serial.println("Creating Access Point...");
  
  // Try with simpler parameters first
  bool result = WiFi.softAP("IceCreamController", NULL, 1, 0, 4);
  if (!result) {
    Serial.println("Failed to create Access Point with full params, trying simple...");
    result = WiFi.softAP("IceCreamController");
    if (!result) {
      Serial.println("Failed to create Access Point completely!");
      return;
    }
  }
  
  Serial.println("Access Point created successfully");
  delay(500);
  
  // Configure AP settings with error checking
  IPAddress apIP(192, 168, 4, 1);
  IPAddress subnet(255, 255, 255, 0);
  
  Serial.println("Configuring AP settings...");
  bool configResult = WiFi.softAPConfig(apIP, apIP, subnet);
  if (!configResult) {
    Serial.println("Failed to configure Access Point!");
    // Continue anyway, use default settings
  }
  
  Serial.print("AP IP address: ");
  Serial.println(WiFi.softAPIP());
  
  // Start DNS server for captive portal
  Serial.println("Starting DNS server...");
  bool dnsResult = dnsServer.start(DNS_PORT, "*", apIP);
  if (!dnsResult) {
    Serial.println("Failed to start DNS server!");
    return;
  }
  
  Serial.println("DNS server started for captive portal");
  Serial.println("Access Point setup complete!");
}

// Initialize OTA updates
void initOTA() {
  // Set hostname for easier identification
  ArduinoOTA.setHostname("IceCreamController");
  
  // Optional: Set password for security
  // ArduinoOTA.setPassword("your_ota_password");
  
  // What happens when OTA starts
  ArduinoOTA.onStart([]() {
    String type;
    if (ArduinoOTA.getCommand() == U_FLASH) {
      type = "sketch";
    } else { // U_SPIFFS
      type = "filesystem";
    }
    Serial.println("Start updating " + type);
    // Stop the web server during update
    server.end();
  });
  
  // What happens when OTA finishes
  ArduinoOTA.onEnd([]() {
    Serial.println("\nOTA Update completed successfully!");
  });
  
  // Show progress during update
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
  });
  
  // Handle OTA errors
  ArduinoOTA.onError([](ota_error_t error) {
    Serial.printf("Error[%u]: ", error);
    if (error == OTA_AUTH_ERROR) {
      Serial.println("Auth Failed");
    } else if (error == OTA_BEGIN_ERROR) {
      Serial.println("Begin Failed");
    } else if (error == OTA_CONNECT_ERROR) {
      Serial.println("Connect Failed");
    } else if (error == OTA_RECEIVE_ERROR) {
      Serial.println("Receive Failed");
    } else if (error == OTA_END_ERROR) {
      Serial.println("End Failed");
    }
  });
  
  ArduinoOTA.begin();
  Serial.println("OTA Ready");
  Serial.print("OTA Hostname: ");
  Serial.println(ArduinoOTA.getHostname());
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
  
  // Add delay to prevent boot loop issues
  delay(1000);
  Serial.println("Ice Cream Controller starting...");

  initSPIFFS();

  // Set GPIO pins
  pinMode(RELAY_MASTER, OUTPUT);
  pinMode(RELAY_COMPRESSOR, OUTPUT);
  digitalWrite(RELAY_MASTER, LOW);
  digitalWrite(RELAY_COMPRESSOR, LOW);
  analogReadResolution(12);
  
  // Initialize preferences first
  prefs.begin("icecream", false);
  loadSettings();
  
  // Setup Access Point with Captive Portal
  setupAccessPoint();
  
  // Initialize OTA updates
  initOTA();
  
  // Add delay after WiFi setup
  delay(500);

  // Captive portal - redirect all requests to main page
  server.onNotFound([](AsyncWebServerRequest *request) {
    String url = request->url();
    String host = request->host();
    
    Serial.println("Request: " + url + " from " + host);
    
    // If it's not our IP or it's a known captive portal detection URL, redirect
    if (host != "192.168.4.1" || 
        url == "/generate_204" || 
        url == "/fwlink" ||
        url == "/hotspot-detect.html" ||
        url == "/ncsi.txt" ||
        url == "/success.txt") {
      
      request->redirect("http://192.168.4.1/");
      return;
    }
    
    // Serve the main page for any unknown request to our IP
    if (SPIFFS.exists("/index.html")) {
      request->send(SPIFFS, "/index.html", "text/html", false, processor);
    } else {
      request->send(404, "text/plain", "File not found");
    }
  });

  // Route for root / web page
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
    Serial.println("Root page requested");
    if (SPIFFS.exists("/index.html")) {
      request->send(SPIFFS, "/index.html", "text/html", false, processor);
    } else {
      String html = "<!DOCTYPE html><html><head><title>Ice Cream Controller</title></head><body>";
      html += "<h1>Ice Cream Controller</h1>";
      html += "<p>SPIFFS files not uploaded. Connect to 192.168.4.1 to access the controller.</p>";
      html += "</body></html>";
      request->send(200, "text/html", html);
    }
  });
  
  server.serveStatic("/", SPIFFS, "/");
  
  // Ice cream controller routes
  server.on("/status", HTTP_GET, [](AsyncWebServerRequest *request) {
    float t = readTemperatureC();
    String json = "{";
    json += "\"temp\":" + String(t) + ",";
    json += "\"target_temp\":" + String(target_temperature) + ",";
    json += "\"compressor\":" + String(compressor_active ? "true" : "false") + ",";
    json += "\"use_timer\":" + String(use_timer ? "true" : "false") + ",";
    json += "\"timer_minutes\":" + String(timer_duration_minutes) + ",";
    json += "\"time_to_target\":" + String(estimateTimeToTarget()) + ",";
    
    // Calculate remaining time if timer is active
    if (compressor_active && use_timer) {
      unsigned long elapsed_ms = millis() - compressor_start_time;
      unsigned long elapsed_minutes = elapsed_ms / 60000;
      unsigned long total_seconds = timer_duration_minutes * 60;
      unsigned long elapsed_seconds = elapsed_ms / 1000;
      
      if (elapsed_seconds < total_seconds) {
        unsigned long remaining_seconds = total_seconds - elapsed_seconds;
        json += "\"remaining_seconds\":" + String(remaining_seconds);
      } else {
        json += "\"remaining_seconds\":0";
      }
    } else {
      json += "\"remaining_seconds\":null";
    }
    
    json += "}";
    request->send(200, "application/json", json);
  });

  server.on("/temp-history", HTTP_GET, [](AsyncWebServerRequest *request) {
    String json = "{\"readings\":[";
    int count = getTemperatureHistoryCount();
    
    for (int i = 0; i < count; i++) {
      if (i > 0) json += ",";
      TempReading reading = getTemperatureReading(i);
      json += "{\"time\":" + String(reading.timestamp) + ",\"temp\":" + String(reading.temperature) + "}";
    }
    
    json += "],\"target\":" + String(target_temperature) + "}";
    request->send(200, "application/json", json);
  });

  server.on("/set-target", HTTP_POST, [](AsyncWebServerRequest *request) {
    if (request->hasParam("target_temp", true)) {
      target_temperature = request->getParam("target_temp", true)->value().toFloat();
      saveSettings();
      request->send(200, "text/plain", "Target temperature set");
    } else {
      request->send(400, "text/plain", "Missing target_temp parameter");
    }
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

  // OTA info endpoint
  server.on("/ota-info", HTTP_GET, [](AsyncWebServerRequest *request) {
    String json = "{";
    json += "\"hostname\":\"" + String(ArduinoOTA.getHostname()) + "\",";
    json += "\"ip\":\"" + WiFi.softAPIP().toString() + "\",";
    json += "\"free_heap\":" + String(ESP.getFreeHeap()) + ",";
    json += "\"uptime\":" + String(millis() / 1000);
    json += "}";
    request->send(200, "application/json", json);
  });

  Serial.println("Starting web server...");
  server.begin();
  Serial.println("Ice Cream Controller started!");
  Serial.println("Connect to WiFi network 'IceCreamController' and visit http://192.168.4.1");
  Serial.println("Setup complete - entering main loop");
}

void loop() {
  // Handle OTA updates
  ArduinoOTA.handle();
  
  // Handle DNS requests for captive portal
  dnsServer.processNextRequest();
  
  // Log temperature reading every 30 seconds
  unsigned long now = millis();
  if (now - lastTempReading >= TEMP_READING_INTERVAL) {
    float temp = readTemperatureC();
    addTemperatureReading(temp);
    lastTempReading = now;
  }
  
  // Handle timer if active
  if (compressor_active && use_timer) {
    unsigned long elapsed = (millis() - compressor_start_time) / 60000;
    if (elapsed >= timer_duration_minutes) {
      stopCompressor();
    }
  }
}