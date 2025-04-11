#define TINY_GSM_MODEM_SIM7600
#include <SPI.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <PubSubClient.h>
#include <TinyGsmClient.h>

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define DEBUG false

#define OLED_MOSI   9
#define OLED_CLK   10
#define OLED_DC    11
#define OLED_CS    12
#define OLED_RESET 13

#define LTE_PWRKEY_PIN 5
#define LTE_RESET_PIN 6
#define LTE_FLIGHT_PIN 7
#define BATTERY_PIN A1

const int switch1Pin = 8;
#define stopwatchButtonPin 2
const int switch2Pin = 3;

// Battery monitoring
const float MAX_VOLTAGE = 4.2;
const float MIN_VOLTAGE = 3.0;
const int NUM_READINGS = 10;

#define PHONE_NUMBER "9944546840"

// MQTT Configuration
const char apn[] = "airtelgprs.com";
const char broker[] = "35.200.163.26";
const int brokerPort = 1883;
const char topicTracking[] = "sim7600/nmea";
const char topicSOS[] = "sim7600/sos";
const char topicOK[] = "sim7600/ok";

// Display and state variables
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, OLED_MOSI, OLED_CLK, OLED_DC, OLED_RESET, OLED_CS);
bool blinkState = false;
unsigned long lastBlinkTime = 0;
const unsigned long BLINK_INTERVAL = 500;
bool validNMEA = false;

// Stopwatch variables
volatile bool stopwatchPressed = false;
enum StopwatchState { RESET, RUNNING, PAUSED };
StopwatchState stopwatchState = RESET;
unsigned long stopwatchStartTime = 0;
unsigned long stopwatchElapsedTime = 0;

// Status variables
String displayStatus = "Ready";
volatile bool sosPressed = false;
volatile bool okPressed = false;
volatile bool sendingMessage = false;

// MQTT clients
TinyGsm modem(Serial1);
TinyGsmClient gsmClient(modem);
PubSubClient mqttClient(gsmClient);

unsigned long lastTrackSendTime = 0;
const unsigned long trackSendInterval = 1500;


String sendData(String command, const int timeout, boolean debug = false) {
    String response = "";
    Serial1.println(command);

    long int startTime = millis();
    while (millis() - startTime < timeout) {
        while (Serial1.available()) {
            char c = Serial1.read();
            response += c;
        }
    }

    if (debug) {
        SerialUSB.print(command);
        SerialUSB.print(" Response: ");
        SerialUSB.println(response);
    }

    return response;
}

String extractNMEA(String response) {
    int start = response.indexOf("+CGPSINFO: ");
    if (start != -1) {
        start += 11;
        int end = response.indexOf("OK", start);
        if (end != -1 && end > start) {
            String nmea = response.substring(start, end);
            nmea.trim();
            return nmea;
        }
    }
    return "";
}

void makeVoiceCall(String phoneNumber) {
    SerialUSB.println("Dialing number: " + phoneNumber);
    String response = sendData("ATD" + phoneNumber + ";", 20000, DEBUG);
    if (response.indexOf("OK") != -1) {
        displayStatus = "Call initiated";
    } else {
        displayStatus = "Failed to call";
    }
    updateDisplay();
}

void handleStopwatch() {
    if (stopwatchPressed) {
        stopwatchPressed = false;
        switch (stopwatchState) {
            case RESET:
                stopwatchState = RUNNING;
                stopwatchStartTime = millis();
                break;
            case RUNNING:
                stopwatchState = PAUSED;
                stopwatchElapsedTime += millis() - stopwatchStartTime;
                break;
            case PAUSED:
                stopwatchState = RESET;
                stopwatchElapsedTime = 0;
                break;
        }
    }

    if (stopwatchState == RUNNING) {
        stopwatchElapsedTime = millis() - stopwatchStartTime;
    }
}

void updateDisplay() {
    display.clearDisplay();
    for (int i = 0; i < SCREEN_HEIGHT; i++) {
          display.drawPixel(0, i, SSD1306_WHITE);  // Left vertical line
          display.drawPixel(SCREEN_WIDTH - 1, i, SSD1306_WHITE);  // Right vertical line
      }
    // Draw CarID in top left corner
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(3, 4);
    display.print("CarID: 1");

    // Draw the circle on top
    drawCircleWithToggle(SCREEN_WIDTH / 2 + 18, 8, blinkState);

    // Draw battery status
    int batteryPercentage = getBatteryPercentage();
    drawBatteryStatus(batteryPercentage);

    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);

    // Display stopwatch time
    char timeString[20];
    unsigned long totalSeconds = stopwatchElapsedTime / 1000;
    unsigned long hours = totalSeconds / 3600;
    unsigned long minutes = (totalSeconds % 3600) / 60;
    unsigned long seconds = totalSeconds % 60;
    unsigned long milliseconds = stopwatchElapsedTime % 1000;

    if (hours > 0) {
        snprintf(timeString, sizeof(timeString), "%02lu:%02lu:%02lu.%03lu", hours, minutes, seconds, milliseconds);
    } else {
        snprintf(timeString, sizeof(timeString), "   %02lu:%02lu.%03lu", minutes, seconds, milliseconds);
    }

    drawLine(0, 0, SCREEN_WIDTH, 20);
    drawLine(0, 17, SCREEN_WIDTH, 20);
    drawLine(0, 63, SCREEN_WIDTH, 20);

    display.setCursor(10, 30);
    display.print(timeString);

    drawLine(0, 45, SCREEN_WIDTH, 45);

    // Display status
    display.setCursor(10, 50);
    display.print(displayStatus);

    display.display();
}

void drawBatteryStatus(int percentage) {
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);

    String displayText;
    if (percentage < 25 && blinkState) {
        displayText = "LOW";
    } else {
        displayText = String(percentage) + "%";
    }

    int boxPadding = 2;
    int textWidth = displayText.length() * 6;
    int textHeight = 8;
    int boxWidth = textWidth + 2 * boxPadding;
    int boxHeight = textHeight + 2 * boxPadding;

    int boxX = SCREEN_WIDTH - boxWidth - 5;
    int boxY = 3;
    int textX = boxX + boxPadding;
    int textY = boxY + boxPadding;

    display.drawRect(boxX, boxY, boxWidth, boxHeight, SSD1306_WHITE);
    display.setCursor(textX, textY);
    display.print(displayText);
}

int getBatteryPercentage() {
    int totalAnalogValue = 0;

    for (int i = 0; i < NUM_READINGS; i++) {
        totalAnalogValue += analogRead(BATTERY_PIN);
        delay(5);
    }

    int averageAnalogValue = totalAnalogValue / NUM_READINGS;
    float voltage = (averageAnalogValue * 3.3) / 1023.0;
    float batteryVoltage = voltage * 2;

    int percentage = map(batteryVoltage * 100, MIN_VOLTAGE * 100, MAX_VOLTAGE * 100, 0, 100);
    return constrain(percentage, 0, 100);
}

void drawStartupPage() {
    const unsigned long SPLASH_SCREEN_DURATION = 3000;  // Total duration of the splash screen in ms
    const unsigned long BLINK_INTERVAL = 500;          // Blink interval in ms
    unsigned long splashScreenStartTime = millis();
    unsigned long lastBlinkTime = 0;
    bool blinkState = false;

    while (millis() - splashScreenStartTime < SPLASH_SCREEN_DURATION) {
        unsigned long currentTime = millis();

        // Handle the blink effect
        if (currentTime - lastBlinkTime >= BLINK_INTERVAL) {
            blinkState = !blinkState;  // Toggle blink state
            lastBlinkTime = currentTime;
        }

        // Display the splash screen
        display.clearDisplay();
        if (blinkState) {
            // Display text only in "on" state
            display.setTextSize(2);
            display.setTextColor(SSD1306_WHITE);
            display.setCursor(SCREEN_WIDTH / 6, SCREEN_HEIGHT / 2 - 10);
            display.println("BLUEBAND  SPORTS");
        }
        display.display();
    }

    // Ensure screen is cleared after the splash screen
    display.clearDisplay();
    display.display();
}


void drawCircleWithToggle(int x, int y, bool isFilled) {
    if (isFilled) {
        display.fillCircle(x, y, 4, SSD1306_WHITE);
    } else {
        display.drawCircle(x, y, 4, SSD1306_WHITE);
    }
}

void drawLine(int x0, int y0, int x1, int y1) {

    for (int x = x0; x < x1; x++) {
        display.drawPixel(x, y0, SSD1306_WHITE);
        display.drawPixel(x + 1, y0, SSD1306_WHITE);
    }
    
}

// Button interrupt handlers
void handleStopwatchPress() {
    stopwatchPressed = true;
}

void handleSosPress() {
    delay(50);
    if (digitalRead(switch1Pin) == LOW) {
        sosPressed = true;
    }
}

void handleOkPress() {
    delay(50);
    if (digitalRead(switch2Pin) == LOW) {
        okPressed = true;
    }
}

// MQTT functions
void reconnectMQTT() {
    while (!mqttClient.connected()) {
        displayStatus = "Connecting MQTT...";
        updateDisplay();
        if (mqttClient.connect("TrackingClient")) {
            displayStatus = "MQTT Connected";
        } else {
            delay(5000);
        }
        updateDisplay();
    }
}

void publishMessage(const char* topic, const String& payload) {
    if (mqttClient.publish(topic, payload.c_str())) {
        SerialUSB.println(String("Published to ") + topic + ": " + payload);
    }
}

void handleSosRequest() {
    sendingMessage = true;
    sosPressed = false;

    displayStatus = "Sending SOS...";
    updateDisplay();
    
    String payload = "{\"carId\":1, \"message\": \"SOS\"}";
    for (int attempt = 0; attempt < 25; attempt++) {
        publishMessage(topicSOS, payload);
        break;
    }

    displayStatus = "SOS: Calling...";
    updateDisplay();
    makeVoiceCall(PHONE_NUMBER);

    sendingMessage = false;
    displayStatus = "SOS completed";
    updateDisplay();
}

void handleOkRequest() {
    sendingMessage = true;
    displayStatus = "Sending OK...";
    updateDisplay();

    String payload = "{\"carId\":1, \"message\": \"OK\"}";
    for (int attempt = 0; attempt < 25; attempt++) {
        publishMessage(topicOK, payload);
        break;
    }

    sendingMessage = false;
    displayStatus = "OK sent";
    updateDisplay();
}

void initModem() {
    modem.restart();
    if (!modem.gprsConnect(apn, "", "")) {
        while (!modem.gprsConnect(apn, "", "")) {
            delay(5000);
        }
    }
    displayStatus = "GPRS Connected";
    updateDisplay();
}

void setup() {
    SerialUSB.begin(115200);
    Serial1.begin(115200);

    if (!display.begin(SSD1306_SWITCHCAPVCC)) {
        SerialUSB.println(F("SSD1306 allocation failed"));
        for(;;);
    }
    display.clearDisplay();
    drawStartupPage();
    display.display();

    pinMode(LTE_RESET_PIN, OUTPUT);
    pinMode(LTE_PWRKEY_PIN, OUTPUT);
    pinMode(LTE_FLIGHT_PIN, OUTPUT);
    pinMode(switch1Pin, INPUT_PULLUP);
    pinMode(stopwatchButtonPin, INPUT_PULLUP);
    pinMode(switch2Pin, INPUT_PULLUP);
    pinMode(BATTERY_PIN, INPUT);

    digitalWrite(LTE_RESET_PIN, LOW);
    digitalWrite(LTE_PWRKEY_PIN, LOW);
    delay(100);
    digitalWrite(LTE_PWRKEY_PIN, HIGH);
    delay(2000);
    digitalWrite(LTE_PWRKEY_PIN, LOW);
    digitalWrite(LTE_FLIGHT_PIN, LOW);

    attachInterrupt(digitalPinToInterrupt(stopwatchButtonPin), handleStopwatchPress, FALLING);
    attachInterrupt(digitalPinToInterrupt(switch1Pin), handleSosPress, FALLING);
    attachInterrupt(digitalPinToInterrupt(switch2Pin), handleOkPress, FALLING);

    displayStatus = "Initializing...";
    updateDisplay();
    
    initModem();
    mqttClient.setServer(broker, brokerPort);
    
    String response = sendData("AT+CGPS=0", 3000, DEBUG);
    response = sendData("AT+CGPS=1", 3000, DEBUG);
    
    displayStatus = "Ready";
    updateDisplay();
}

void loop() {
    unsigned long currentTime = millis();

    if (!mqttClient.connected()) {
        reconnectMQTT();
    }
    mqttClient.loop();

    if (sosPressed) {
        handleSosRequest();
    }

    if (okPressed) {
        handleOkRequest();
    }

    if (!sendingMessage && (currentTime - lastTrackSendTime >= trackSendInterval)) {
        sendTrackData();
        lastTrackSendTime = currentTime;
    }

    if (currentTime - lastBlinkTime >= BLINK_INTERVAL) {
        blinkState = validNMEA ? !blinkState : false;
        lastBlinkTime = currentTime;
    }

    handleStopwatch();
    updateDisplay();
    delay(100);
}

void sendTrackData() {
    String gpsInfo = sendData("AT+CGPSINFO", 1300, DEBUG);
    String nmeaSentence = extractNMEA(gpsInfo);

    if (nmeaSentence.length() > 8) {
        validNMEA = true;
        String payload = "{\"nmea\": \"" + nmeaSentence + "\",\"carId\":1}";
        publishMessage(topicTracking, payload);
    } else {
        validNMEA = false;
    }
}