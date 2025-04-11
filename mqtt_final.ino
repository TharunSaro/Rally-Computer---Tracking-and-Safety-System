#define TINY_GSM_MODEM_SIM7600
#define DEBUG false

#include <PubSubClient.h>
#include <TinyGsmClient.h>

// LTE Modem pins
#define LTE_PWRKEY_PIN 5
#define LTE_RESET_PIN 6
#define LTE_FLIGHT_PIN 7

// Switch pins
const int switch1Pin = 8;
const int switch2Pin = 9;

// APN and MQTT credentials
const char apn[] = "airtelgprs.com";
const char broker[] = "35.200.163.26";
const int brokerPort = 1883;

const char topic[] = "Blueband/car2/status"; // MQTT topic to publish NMEA data

// Intervals
const unsigned long trackSendInterval = 1500; // 1.5 seconds
unsigned long lastTrackSendTime = 0;

// Maximum retries
int gpsSendAttempts = 0;
const int maxAttempts = 25;

// GSM and MQTT clients
TinyGsm modem(Serial1);
TinyGsmClient gsmClient(modem);
PubSubClient mqttClient(gsmClient);

// Function to extract NMEA sentence
String extractNMEA(String response) {
    int start = response.indexOf("+CGPSINFO: ");
    if (start != -1) {
        start += 11; // Move past "+CGPSINFO: "
        int end = response.indexOf("OK", start);
        if (end != -1 && end > start) {
            String nmea = response.substring(start, end);
            nmea.trim(); // Trim any leading/trailing whitespace
            return nmea;
        }
    }
    return ""; // Return empty string if NMEA sentence not found or invalid format
}

// Send AT command to the modem
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

// Function to publish NMEA data to MQTT
void sendTrackData() {
    String gpsInfo = sendData("AT+CGPSINFO", 1300, DEBUG);
    String nmeaSentence = extractNMEA(gpsInfo);
    SerialUSB.println(nmeaSentence + " nmea");

    if (nmeaSentence.length() > 8) {
        SerialUSB.println("Publishing Track data to MQTT");
        String payload = "{\"nmea\": \"" + nmeaSentence + "\",\"carId\":2}";

        if (mqttClient.publish(topic, payload.c_str())) {
            SerialUSB.println("Data published successfully.");
        } else {
            SerialUSB.println("Failed to publish data.");
        }
    }
}

// MQTT reconnect function
void reconnectMQTT() {
    while (!mqttClient.connected()) {
        SerialUSB.print("Connecting to MQTT broker...");
        if (mqttClient.connect("TrackingClient")) {
            SerialUSB.println("connected.");
        } else {
            SerialUSB.print("failed, rc=");
            SerialUSB.print(mqttClient.state());
            SerialUSB.println(" trying again in 5 seconds.");
            delay(5000);
        }
    }
}

// LTE modem initialization
void initModem() {
    modem.restart();
    if (!modem.gprsConnect(apn, "", "")) {
        SerialUSB.println("Failed to connect to GPRS. Retrying...");
        while (!modem.gprsConnect(apn, "", "")) {
            delay(5000);
        }
    }
    SerialUSB.println("GPRS connected.");
}

void setup() {
    // Initialize serial communication
    SerialUSB.begin(115200);
    Serial1.begin(115200);

    // Configure LTE pins
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

    // Configure switches
    pinMode(switch1Pin, INPUT_PULLUP);
    pinMode(switch2Pin, INPUT_PULLUP);

    // Wait for SerialUSB
    while (!SerialUSB) {
        delay(10);
    }

    SerialUSB.println("Initializing modem...");
    initModem();

    // Initialize MQTT
    mqttClient.setServer(broker, brokerPort);

    SerialUSB.println("Enabling GPS...");
    sendData("AT+CGPS=0", 3000, DEBUG); // Disable GPS (if already running)
    sendData("AT+CGPS=1", 3000, DEBUG); // Enable GPS
}

void loop() {
    // Reconnect MQTT if disconnected
    if (!mqttClient.connected()) {
        reconnectMQTT();
    }
    mqttClient.loop();

    // Send track data at regular intervals
    unsigned long currentTime = millis();
    if (currentTime - lastTrackSendTime >= trackSendInterval) {
        lastTrackSendTime = currentTime;
        sendTrackData();
    }
}
