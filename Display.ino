/* 

THIS code is just for an UI for the OLED screen that has 1+4 pages: 
1. splash screen
2. speed and distance
3. lap timings
4. overall results
`

*/

#include <SPI.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Adafruit_GPS.h>

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64

#define OLED_MOSI   9
#define OLED_CLK   10
#define OLED_DC    11
#define OLED_CS    12
#define OLED_RESET 13
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, OLED_MOSI, OLED_CLK, OLED_DC, OLED_RESET, OLED_CS);

// GSM/GPS variables (assuming GPS is used for time)
Adafruit_GPS gps = Adafruit_GPS(&Serial1);  // Initialize GPS on Serial1

// Battery monitoring
#define BATTERY_PIN A1  // Pin for battery voltage monitoring
const float MAX_VOLTAGE = 4.2;  // Max voltage when battery is fully charged
const float MIN_VOLTAGE = 3.0;  // Min voltage when battery is almost empty

// Buttons for page switching and specific actions
#define PAGE_BUTTON 4  // Pin for the page switch button
#define SOS_BUTTON 8   // Pin for the SOS button (B2)
#define OK_BUTTON 3    // Pin for the OK button (B3)
#define STOPWATCH_BUTTON 2  // Pin for the stopwatch button (B4)

bool blinkState = false;
unsigned long lastBlinkTime = 0;
const unsigned long BLINK_INTERVAL = 500;
unsigned long lastPageSwitchTime = 0;
const unsigned long PAGE_SWITCH_DELAY = 2000;

int currentPage = 0;
unsigned long stopwatchStart = 0;
unsigned long stopwatchElapsed = 0;
int stopwatchState = -1;  // -1 = reset, 1 = running, 2 = stopped

int scrollOffset = 0; // Variable for scrolling message

String carID = "C1"; // Variable for car ID
String broadcastMessage = ""; // Default empty broadcast message
unsigned long broadcastClearTime = 0; // Time when to clear the broadcast message

// SOS call tracking variables
bool sosCallInProgress = false;
unsigned long sosCallStartTime = 0;
unsigned long sosCallElapsedTime = 0;
bool callEnded = false;

// Splash screen timing variables
bool splashScreenShown = false;
unsigned long splashScreenStartTime = 0;
const unsigned long SPLASH_SCREEN_DURATION = 3000; // 3 seconds

// Debounce for the stopwatch button
unsigned long lastStopwatchButtonPress = 0;
const unsigned long DEBOUNCE_DELAY = 300;  // 300 milliseconds debounce delay

void setup() {
    SerialUSB.begin(115200);
    gps.begin(9600);  // Start GPS at 9600 baud

    // Initialize the OLED display
    if (!display.begin(SSD1306_SWITCHCAPVCC)) {
        SerialUSB.println(F("SSD1306 allocation failed"));
        for (;;);
    }

    display.clearDisplay();
    display.display();

    // Initialize the button pins
    pinMode(PAGE_BUTTON, INPUT_PULLUP);
    pinMode(SOS_BUTTON, INPUT_PULLUP);
    pinMode(OK_BUTTON, INPUT_PULLUP);
    pinMode(STOPWATCH_BUTTON, INPUT_PULLUP);

    // Initialize battery pin
    pinMode(BATTERY_PIN, INPUT);
}

void loop() {
    unsigned long currentTime = millis();

    // Page switch button logic
    if (digitalRead(PAGE_BUTTON) == LOW && currentTime - lastPageSwitchTime >= 300) {
        currentPage = (currentPage + 1) % 5;  // Cycle through 5 pages (0 to 4)
        lastPageSwitchTime = currentTime;
    }

    // SOS button logic (B2)
    if (digitalRead(SOS_BUTTON) == LOW && !sosCallInProgress && !callEnded) {
        broadcastMessage = "SOS alert pressed";
        makeCall("9944546840");  // Make the SOS call using AT command
        sosCallStartTime = currentTime;
        sosCallInProgress = true;
    }

    // OK button logic (B3)
    if (digitalRead(OK_BUTTON) == LOW && broadcastMessage == "SOS alert pressed") {
        broadcastMessage = "";  // Clear the SOS alert when OK button is pressed
        callEnded = false;
    }

    // Stopwatch button logic (B4) with debounce
    if (digitalRead(STOPWATCH_BUTTON) == LOW && currentTime - lastStopwatchButtonPress >= DEBOUNCE_DELAY) {
        currentPage = 2;  // Switch to stopwatch page
        lastStopwatchButtonPress = currentTime;  // Update the last button press time

        switch (stopwatchState) {
            case -1:  // Reset state, start the stopwatch
                stopwatchState = 1;
                stopwatchStart = millis();
                stopwatchElapsed = 0;
                break;
            case 1:  // Running state, stop the stopwatch
                stopwatchState = 2;
                stopwatchElapsed = millis() - stopwatchStart;
                break;
            case 2:  // Stopped state, reset the stopwatch
                stopwatchState = -1;
                stopwatchElapsed = 0;
                break;
        }
    }

    // Show the splash screen if not yet displayed
    if (!splashScreenShown) {
        if (currentTime - splashScreenStartTime >= SPLASH_SCREEN_DURATION) {
            splashScreenShown = true;  // Splash screen has been displayed for 3 seconds
            currentPage = 1;  // Start from Page 1 after splash screen
            return;
        } else {
            // Handle the splash screen blink effect
            if (currentTime - lastBlinkTime >= BLINK_INTERVAL) {
                blinkState = !blinkState;  // Toggle blink state
                lastBlinkTime = currentTime;
            }

            // Display the splash screen
            display.clearDisplay();
            if (blinkState) {
                SplashscreenUI();
            }
            display.display();
            return;
        }
    }

    // Update GPS to fetch real-time clock (RTC)
    if (gps.newNMEAreceived()) {
        gps.parse(gps.lastNMEA());  // Parse new NMEA data
    }

    // Clear the display for each page
    display.clearDisplay();

    // Read the battery percentage
    int batteryPercentage = getBatteryPercentage();

    // Draw the top bar once
    Topbar(gpsTime(), carID, batteryPercentage, blinkState);

    // Draw the center content based on the current page
    switch (currentPage) {
        case 1:
            SpeedAndDistanceUI("--.--", "--.--");
            break;
        case 2:
            if (stopwatchState == 1) {
                stopwatchElapsed = millis() - stopwatchStart;
            }
            StopwatchUI(formatElapsedTime(stopwatchElapsed));
            break;
        case 3:
            LapTimingUI("00:00:00", "00:00:00");
            break;
        case 4:
            OverallResultsUI("--.--", "00:00:00", "--");
            break;
    }

    // Handle SOS call progress and end
    if (sosCallInProgress) {
        sosCallElapsedTime = currentTime - sosCallStartTime;
        broadcastMessage = "Making call to 9944546840\nCall duration: " + formatElapsedTime(sosCallElapsedTime);
    }

    if (callEnded) {
        broadcastMessage = "Call ended\nSOS alert pressed";
    }

    // Draw the bottom bar with the broadcast message, and always show the divider lines
    Bottombar(broadcastMessage);

    // Draw the vertical dividers to create the boxy look
    drawVerticalDividers();

    // Update the display with the new content
    display.display();

    delay(10);  // Small delay to avoid hogging CPU
}

// PAGE 0: Splash screen page (center content only)
void SplashscreenUI() {
    display.setTextSize(2);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(SCREEN_WIDTH / 6, SCREEN_HEIGHT / 2 - 10);
    display.println("BLUEBAND  SPORTS");
}

// PAGE 1: Speed and Distance page (center content only)
void SpeedAndDistanceUI(String speed, String distance) {
    display.setCursor(5, 16);
    display.setTextSize(2);
    display.println("spd:" + (speed != "--.--" ? speed : "N/A"));
    display.setCursor(5, 36);
    display.println("dis:" + (distance != "--.--" ? distance : "N/A"));
}

// PAGE 2: Stopwatch and Elapsed Time page (center content only)
void StopwatchUI(String elapsedTime) {
    display.setTextSize(1);
    display.setCursor(2, 16);
    display.println("Stopwatch");
    display.setTextSize(2);
    display.setCursor(2, 30);
    display.println((elapsedTime != "00:00" ? elapsedTime : "N/A"));
}

// PAGE 3: Lap Timing page (center content only)
void LapTimingUI(String lapTime, String lastLap) {
    display.setCursor(5, 16);
    display.setTextSize(1);
    display.println("Lap Time: " + lapTime);
    display.setCursor(5, 26);
    display.println("Last Lap: " + lastLap);
}

// PAGE 4: Overall results page (center content only)
void OverallResultsUI(String avgSpeed, String totalTime, String penalties) {
    display.setCursor(5, 18);
    display.println("avg spd: " + (avgSpeed != "--.--" ? avgSpeed : "N/A"));
    display.setCursor(5, 28);
    display.println("total time: " + (totalTime != "00:00:00" ? totalTime : "N/A"));
    display.setCursor(5, 38);
    display.println("penalties: " + (penalties != "--" ? penalties : "N/A"));
}

// Function to draw Battery Status
void drawBatteryStatus(int batteryPercentage) {
    display.setCursor(SCREEN_WIDTH - 25, 3);
    if (batteryPercentage < 20 && blinkState) {
        display.println("LOW");
    } else {
        display.println(String(batteryPercentage) + "%");
    }
}

// Function to draw a dashed line
void drawLine(int x0, int y0, int x1, int y1) {
    for (int x = x0; x < x1; x += 1) {
        display.drawPixel(x, y0, SSD1306_WHITE);
    }
}

// Function for Top Bar
void Topbar(const String& time, const String& carID, int batteryPercentage, bool blinkState) {
    drawLine(0, 0, SCREEN_WIDTH, 12);  // Top divider line

    display.setTextSize(1);

    // Display the time
    display.setCursor(2, 3);
    display.print(time);

    // Display Car ID
    display.setCursor(SCREEN_WIDTH / 2 - 15, 3);
    display.print(carID);

    // Display battery status
    drawBatteryStatus(batteryPercentage);

    drawLine(0, 12, SCREEN_WIDTH, 12);  // Bottom divider line
}

// Function for Bottom Bar
void Bottombar(const String& message) {
    drawLine(0, 52, SCREEN_WIDTH, 36);  // Always draw top divider line
    updateScrollingMessage(message);
    drawLine(0, 63, SCREEN_WIDTH, 12);  // Always draw bottom divider line
}

// Scrolling message function
void updateScrollingMessage(const String& message) {
    int messageWidth = message.length() * 6;  // Approximate message width
    display.setCursor(-scrollOffset, 55);
    display.setTextSize(1);
    display.print(message + " ");  // Append space for separation
    display.print(message);  // Repeat message to create loop

    scrollOffset++;
    if (scrollOffset > messageWidth) scrollOffset = 0;  // Reset scroll
}

// Draw vertical dividers on both edges of the display
void drawVerticalDividers() {
    for (int i = 0; i < SCREEN_HEIGHT; i++) {
        display.drawPixel(0, i, SSD1306_WHITE);  // Left vertical line
        display.drawPixel(SCREEN_WIDTH - 1, i, SSD1306_WHITE);  // Right vertical line
    }
}

// Function to get the battery percentage
int getBatteryPercentage() {
    int analogValue = analogRead(BATTERY_PIN);
    float voltage = (analogValue * 3.3) / 1023.0;  // Convert analog reading to voltage
    float batteryVoltage = voltage * 2;  // Assuming voltage divider

    // Map the voltage to a percentage between MIN_VOLTAGE and MAX_VOLTAGE
    int percentage = map(batteryVoltage * 100, MIN_VOLTAGE * 100, MAX_VOLTAGE * 100, 0, 100);

    return constrain(percentage, 0, 100);  // Ensure the percentage is between 0 and 100
}

// Function to get the current time from GPS (mocked for now)
String gpsTime() {
    if (gps.hour == 0 && gps.minute == 0) {
        return "00:00";
    }
    String hour = String(gps.hour);
    String minute = String(gps.minute);
    if (hour.length() < 2) hour = "0" + hour;
    if (minute.length() < 2) minute = "0" + minute;
    return hour + ":" + minute;
}

// Function to format the elapsed time (MM:SS.M format)
String formatElapsedTime(unsigned long elapsed) {
    unsigned long milliseconds = (elapsed % 1000) / 100;  // Only show 1 digit of milliseconds
    unsigned long seconds = (elapsed / 1000) % 60;
    unsigned long minutes = (elapsed / (1000 * 60)) % 60;

    String formattedTime = "";
    if (minutes < 10) formattedTime += "0";
    formattedTime += String(minutes) + ":";
    if (seconds < 10) formattedTime += "0";
    formattedTime += String(seconds) + ".";
    formattedTime += String(milliseconds);  // Only 1 digit for milliseconds
    return formattedTime;
}

// Function to make a call using AT commands
void makeCall(String phoneNumber) {
// TODO: Implement function to make call
}
