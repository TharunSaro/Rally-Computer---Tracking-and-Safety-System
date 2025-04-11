#include <SPI.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64

#define OLED_MOSI   9
#define OLED_CLK   10
#define OLED_DC    11
#define OLED_CS    12
#define OLED_RESET 13
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, OLED_MOSI, OLED_CLK, OLED_DC, OLED_RESET, OLED_CS);

#define DEBUG false

#define LTE_PWRKEY_PIN 5
#define LTE_RESET_PIN 6
#define LTE_FLIGHT_PIN 7

const int switch1Pin = 8;
#define stopwatchButtonPin 2
const int switch2Pin = 3;  // OK button pin

// Battery monitoring
#define BATTERY_PIN A1
const float MAX_VOLTAGE = 4.2;
const float MIN_VOLTAGE = 3.0;
const int NUM_READINGS = 10;

#define PHONE_NUMBER "8637447158"

bool blinkState = false;
unsigned long lastBlinkTime = 0;
const unsigned long BLINK_INTERVAL = 500;

volatile bool stopwatchPressed = false;
enum StopwatchState { RESET, RUNNING, PAUSED };
StopwatchState stopwatchState = RESET;

unsigned long stopwatchStartTime = 0;
unsigned long stopwatchElapsedTime = 0;
unsigned long lastUpdate = 0;

String displayStatus = "Ready";
volatile bool sosPressed = false;
volatile bool okPressed = false;
volatile bool sendingMessage = false;

unsigned long lastTrackSendTime = 0;
const unsigned long trackSendInterval = 500; // 0.5 seconds

bool validNMEA = false;

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

void handleStopwatchPress() {
    stopwatchPressed = true;
}

void handleSosPress() {
    delay(50);  // Simple debounce
    if (digitalRead(switch1Pin) == LOW) {  // Check if the button is still pressed
        sosPressed = true;
    }
}

void handleOkPress() {
    delay(50);  // Simple debounce
    if (digitalRead(switch2Pin) == LOW) {  // Check if the button is still pressed
        okPressed = true;
    }
}

void setup() {
    SerialUSB.begin(115200);
    Serial1.begin(115200);
if (!display.begin(SSD1306_SWITCHCAPVCC)) {
        SerialUSB.println(F("SSD1306 allocation failed"));
        for (;;);
    }
    display.clearDisplay();
    drawStartupPage();
    display.display();
    
    SerialUSB.println("System initialized.");
    pinMode(LTE_RESET_PIN, OUTPUT);
    digitalWrite(LTE_RESET_PIN, LOW);

    pinMode(LTE_PWRKEY_PIN, OUTPUT);
    digitalWrite(LTE_PWRKEY_PIN, LOW);
    delay(100);
    digitalWrite(LTE_PWRKEY_PIN, HIGH);
    delay(2000);
    digitalWrite(LTE_PWRKEY_PIN, LOW);

    pinMode(LTE_FLIGHT_PIN, OUTPUT);
    digitalWrite(LTE_FLIGHT_PIN, LOW);

    pinMode(switch1Pin, INPUT_PULLUP);
    pinMode(stopwatchButtonPin, INPUT_PULLUP);
    pinMode(switch2Pin, INPUT_PULLUP);
    pinMode(BATTERY_PIN, INPUT);

    attachInterrupt(digitalPinToInterrupt(stopwatchButtonPin), handleStopwatchPress, FALLING);
    attachInterrupt(digitalPinToInterrupt(switch1Pin), handleSosPress, FALLING);
    attachInterrupt(digitalPinToInterrupt(switch2Pin), handleOkPress, FALLING);

   // String response = sendData("AT+CFUN=1,1", 5000, DEBUG);
    String response = sendData("AT+CGATT=0", 2000, DEBUG);
    response = sendData("AT+CGATT=1", 2000, DEBUG);
    response = sendData("AT+CGACT=1,1", 2000, DEBUG);
    response = sendData("AT+CGPADDR=1", 1300, DEBUG);
    if (response.indexOf("OK") != -1 && response.indexOf(".") != -1) {
        SerialUSB.println("Internet connected.");
    } else {
        SerialUSB.println("Internet not connected.");
    }
    response = sendData("AT+CGPS=0", 3000, DEBUG);
    response = sendData("AT+CGPS=1", 3000, DEBUG);
    
}

void loop() {
    unsigned long currentTime = millis();

    if (sosPressed) {
        handleSosRequest();
        sosPressed = false;  // Reset the flag after handling
    }

    if (okPressed) {
        handleOkRequest();
        okPressed = false;  // Reset the flag after handling
    }

    if (!sendingMessage) {
        sendTrackData();
    }

  // Modified blinking logic
    if (currentTime - lastBlinkTime >= BLINK_INTERVAL) {
        if (validNMEA) {
            blinkState = !blinkState;
        } else {
            blinkState = false;  // Always show empty circle if no valid NMEA
        }
        
        lastBlinkTime = currentTime;
    }
    
    handleStopwatch();
    updateDisplay();
    delay(100);
}

void handleSosRequest() {
    sendingMessage = true;
    sosPressed = false;

    // First, send HTTP request
    displayStatus = "Sending SOS...";
    updateDisplay();
    int max_sos_attempt = 0;
    while (max_sos_attempt++ < 25) {
        if (!sendHTTPRequest("https://blueband-speed-zr7gm6w4cq-el.a.run.app/sos", "{\"carId\":2, \"message\": \"SOS\"}", true)) {
            SerialUSB.println("SOS HTTP request sent");
            break;
        }
        SerialUSB.println("SOS HTTP request failed, retrying...");
    }

    // Clear SOS message from display
    displayStatus = "";
    updateDisplay();
    delay(1000);  // Brief pause to show the clear display

    // Then, make the voice call
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
    int max_ok_attempt = 0;
    while (max_ok_attempt++ < 25) {
        if (!sendHTTPRequest("https://blueband-speed-zr7gm6w4cq-el.a.run.app/ok", "{\"carId\":1, \"message\": \"OK\"}", true)) {
            SerialUSB.println("OK request sent");
            break;
        }
        SerialUSB.println("OK request failed, retrying...");
    }

    displayStatus = "OK sent";
    updateDisplay();
    delay(1000);

    sendingMessage = false;
    displayStatus = "Ready";
    updateDisplay();
}

bool sendHTTPRequest(String url, String jsonPayload, bool breath) {
    SerialUSB.println("Sending HTTP request to " + url);

    sendData("AT+HTTPINIT", 100, DEBUG);
    sendData("AT+HTTPPARA=\"CID\",1", 100, DEBUG);
    sendData("AT+HTTPPARA=\"URL\",\"" + url + "\"", 100, DEBUG);
    sendData("AT+HTTPPARA=\"CONTENT\",\"application/json\"", 100, DEBUG);
    sendData("AT+HTTPDATA=" + String(jsonPayload.length()) + ",2000", 100);
    sendData(jsonPayload, 100, DEBUG);
    String response = sendData("AT+HTTPACTION=1", 2000, DEBUG);
    sendData("AT+HTTPTERM", 100, DEBUG);

    if (breath) {
        delay(50);
    }

    return (response.indexOf("200") == -1);
}

void sendTrackData() {
    String gpsInfo = sendData("AT+CGPSINFO", 1300, DEBUG);
    String nmeaSentence = extractNMEA(gpsInfo);
    SerialUSB.println(nmeaSentence + " nmea");

    if (nmeaSentence.length() > 8) {
        SerialUSB.println("Sending Track data");
        validNMEA = true;

        String jsonPayload = "{\"nmea\": \"" + nmeaSentence + "\",\"carId\":1}";
        String requestBinURL = "https://blueband-speed-zr7gm6w4cq-el.a.run.app/track";
        if (sendHTTPRequest(requestBinURL, jsonPayload, false)) {
            // Retry logic if needed
        }
    } else {
        validNMEA = false;
    }
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

    // Draw CarID in top left corner
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(0, 0);
    display.print("CarID: 1");

    // Draw the circle on top
    drawCircleWithToggle(SCREEN_WIDTH / 2 + 18, 4, blinkState);

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

    drawLine(0, 20, SCREEN_WIDTH, 20);
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
    int boxY = 0;
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
    display.clearDisplay();
    display.setTextSize(2);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(SCREEN_WIDTH / 6, SCREEN_HEIGHT / 2 - 10);
    display.println("BLUEBAND  SPORTS");
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
    for (int x = x0; x < x1; x += 4) {
        display.drawPixel(x, y0, SSD1306_WHITE);
        display.drawPixel(x + 1, y0, SSD1306_WHITE);
    }
}