# ESP32 Ice Cream Machine Controller

![Melissa Ice Cream Machine](melissa.avif)

A smart WiFi-enabled replacement control system for the **Melissa 2L Ice Cream Machine** (180W), built with a ESP32 2-channel relay module and featuring a modern web-based interface with intelligent WiFi management.

## 🍨 Background

This project was created to replace the original control system of the [Melissa Ice Cream Machine](https://www.imerco.dk/melissa-ismaskine-2-liter-180-watt-rustfrit-staal?id=100409440) after the original control board failed. Instead of buying a new machine, this ESP32 2-channel relay module solution provides:

- **Modern WiFi Control**: Remote monitoring and control via web interface
- **Enhanced Features**: Timer control, temperature monitoring, and safety features not available in the original
- **Cost-Effective**: Significant savings compared to buying a replacement machine
- **Reliability**: More robust than the original control system

## 🍦 Features

- **Smart Temperature Monitoring**: Real-time temperature sensing using NTC thermistor embedded in cooling bowl
- **Relay Control**: Master and compressor relay control using built-in 2-channel relay module
- **Web Interface**: Beautiful, responsive web interface for remote control
- **WiFi Manager**: Easy WiFi setup with captive portal configuration
- **Timer Control**: Optional timer-based operation with customizable duration
- **SPIFFS Storage**: Persistent storage for WiFi credentials and settings
- **Auto-Recovery**: Automatic WiFi reconnection and fallback to AP mode

## 📋 Hardware Requirements

### Components
- **ESP32 2-Channel Relay Module** ([AliExpress Link](https://www.aliexpress.com/item/1005007027676026.html)) - ESP32-WROOM with built-in 2-channel relays
- **NTC Thermistor** (10kΩ at 25°C, B-value: 3435K) - *embedded in cooling bowl*
- **10kΩ Resistor** (for NTC voltage divider)
- **Power Supply** (DC5-30V for the relay module)

### Pin Configuration
```
GPIO 16 - Master Relay Control (Relay 1 on module)
GPIO 17 - Compressor Relay Control (Relay 2 on module)
GPIO 34 - NTC Temperature Sensor (Analog Input - from cooling bowl)
```

### Wiring Diagram
```
ESP32 2-Channel Relay Module    Melissa Ice Cream Machine
├── Relay 1 (GPIO 16) ────────── Master Power Control
├── Relay 2 (GPIO 17) ────────── Compressor Control
├── GPIO 34 ──────────────────── NTC Thermistor (embedded in cooling bowl via 10kΩ divider)
├── 3.3V ─────────────────────── NTC Voltage Divider
├── GND ──────────────────────── Common Ground
└── DC 5-30V ─────────────────── Power Input for module
```

## 🚀 Getting Started

### Prerequisites
- [PlatformIO IDE](https://platformio.org/platformio-ide) or [PlatformIO Core](https://platformio.org/install/cli)
- ESP32 drivers installed on your system

Use VS code and the platform.IO extension

### WiFi Setup

The device operates as a WiFi Access Point with automatic captive portal:

1. **Access Point Mode**: The device creates a WiFi hotspot named `IceCreamController`
2. **Connect to AP**: Join the network (no password required)
3. **Automatic Redirect**: Your device should automatically open a browser and redirect to the ice cream controller interface
4. **Manual Access**: If automatic redirect doesn't work, manually navigate to `192.168.4.1` in your browser

**How Captive Portal Works:**

- When you connect to the WiFi network, your device will try to detect internet connectivity
- The ESP32 intercepts these detection requests and redirects them to the controller interface
- This works with most modern devices including smartphones, tablets, and laptops
- Supports detection methods used by Android, iOS, Windows, and other operating systems

## 🖥️ Web Interface

### Main Controller (`/`)
- Real-time temperature display
- Start/Stop compressor controls
- Timer configuration
- System status indicators
- WiFi reset functionality

### API Endpoints

| Endpoint | Method | Description |
|----------|--------|-------------|
| `/` | GET | Main controller interface |
| `/status` | GET | JSON status (temp, compressor state, timer) |
| `/start` | POST | Start compressor with optional timer |
| `/stop` | POST | Stop compressor |
| `/reset-wifi` | POST | Reset WiFi credentials and restart |

### Example API Response (`/status`)
```json
{
  "temp": 25.4,
  "compressor": false,
  "use_timer": true,
  "timer_minutes": 30
}
```

## 📊 Temperature Calculation

The NTC thermistor is embedded directly in the cooling bowl of the Melissa ice cream machine, providing accurate real-time temperature monitoring of the ice cream mixture during the freezing process. The thermistor uses the Steinhart-Hart equation for precise temperature readings:

```cpp
float readTemperatureC() {
  int adc = analogRead(NTC_PIN);
  float voltage = adc * 3.3 / 4095.0;
  float resistance = 10000.0 * (3.3 / voltage - 1);
  float B = 3435.0;  // B-value of thermistor
  float T0 = 298.15; // 25°C in Kelvin
  float R0 = 10000.0; // Resistance at 25°C
  float tempK = 1.0 / (1.0 / T0 + log(resistance / R0) / B);
  return tempK - 273.15; // Convert to Celsius
}
```

## 🔒 Safety Features

- **Master Relay**: Primary safety cutoff for entire system (replaces original safety mechanisms)
- **Controlled Startup**: Systematic relay activation sequence
- **Timer Protection**: Automatic shutdown after configured duration
- **Temperature Monitoring**: Real-time temperature feedback (enhanced from original LCD display)
- **Web-based Control**: Remote monitoring and control capability
- **Original Hardware Compatibility**: Designed to work with existing Melissa machine components

## 🛠️ Dependencies

```ini
lib_deps = 
    knolleary/PubSubClient
    adafruit/Adafruit Unified Sensor@^1.1.15
    esp32async/ESPAsyncWebServer@^3.7.10
    esp32async/AsyncTCP@^3.4.5
```

## 📝 Usage

1. **Power On**: Connect power to ESP32 and relay modules
2. **WiFi Setup**: Configure network settings via captive portal (first time only)
3. **Access Interface**: Navigate to the configured IP address
4. **Monitor Temperature**: View real-time temperature readings
5. **Control Operation**: Start/stop compressor with optional timer
6. **Safety**: Use master relay for emergency shutdown

## � Over-The-Air (OTA) Updates

The controller supports OTA (Over-The-Air) firmware updates, allowing you to update the code wirelessly without physical access to the device.

### How OTA Works

1. **Initial Setup**: After first WiFi connection, OTA server starts automatically
2. **Network Discovery**: Device appears as "IceCreamController" on your network
3. **Secure Updates**: Optional password protection for update security
4. **Progress Monitoring**: Real-time update progress via serial monitor

### Using OTA Updates

#### Method 1: PlatformIO IDE
1. **Configure Upload**: In `platformio.ini`, uncomment and set:
   ```ini
   upload_protocol = espota
   upload_port = 192.168.1.100  ; Your ESP32's IP address
   upload_flags = --auth=your_ota_password  ; If password enabled
   ```

2. **Upload Firmware**: Use normal upload button - PlatformIO will upload wirelessly

#### Method 2: Arduino IDE
1. **Tools > Port**: Select your ESP32's network port (e.g., "IceCreamController at 192.168.1.100")
2. **Upload**: Use normal upload process

#### Method 3: Command Line
```bash
# Using platformio
pio run --target upload --upload-port 192.168.1.100

# Using Arduino IDE tools
arduino-cli upload --fqbn esp32:esp32:esp32 --port 192.168.1.100
```

### OTA API Endpoint

- **GET `/ota-info`**: Returns OTA status information
  ```json
  {
    "hostname": "IceCreamController",
    "ip": "192.168.1.100",
    "free_heap": 234560,
    "uptime": 3600
  }
  ```

### OTA Safety Features

- **Progress Monitoring**: Visual progress indication during updates
- **Error Handling**: Automatic rollback on failed updates
- **Service Continuity**: Ice cream controller remains operational until update starts
- **Auto-Recovery**: Device restarts automatically after successful update

### Troubleshooting OTA

- **Device Not Found**: Check IP address and ensure device is connected to WiFi
- **Authentication Failed**: Verify OTA password if enabled
- **Upload Timeout**: Ensure stable WiFi connection and device isn't overloaded
- **Permission Errors**: Check firewall settings on development machine

## �🔧 Troubleshooting

### Melissa Machine Integration
- **ESP32 relay module compatibility**: Connect the 2-channel relay outputs directly to the original compressor and power connections
- **Temperature sensor placement**: NTC thermistor is embedded in the cooling bowl for direct mixture temperature monitoring
- **Power requirements**: ESP32 relay module handles 180W compressor power (supports AC90-250V/DC5-30V)
- **Mechanical fit**: Compact ESP32 relay module fits in original control panel space

### WiFi Issues
- **Can't connect to AP**: Ensure you're within range, try restarting the device
- **Can't access web interface**: Check IP address configuration
- **WiFi credentials lost**: Use "Reset WiFi" button to reconfigure

### Hardware Issues
- **Temperature readings incorrect**: Check NTC wiring and resistance values
- **Relays not activating**: Verify GPIO connections and relay power supply
- **ESP32 not responding**: Check power supply and USB connection

### Build Issues
- **Library conflicts**: Clean build directory: `platformio run --target clean`
- **Upload failures**: Ensure proper USB drivers and port selection

## 🤝 Contributing

1. Fork the repository
2. Create a feature branch (`git checkout -b feature/amazing-feature`)
3. Commit your changes (`git commit -m 'Add amazing feature'`)
4. Push to the branch (`git push origin feature/amazing-feature`)
5. Open a Pull Request

## 📄 License

None. Go nuts


## 🙏 Acknowledgments

- [Random Nerd Tutorials](https://randomnerdtutorials.com/) for WiFi Manager implementation guidance

---

**⚠️ Safety Notice**: This controller manages electrical relays and should only be used by individuals familiar with electrical safety. Always follow proper electrical safety procedures and local codes.
Don't kill yourself
