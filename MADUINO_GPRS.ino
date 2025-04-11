#define DEBUG true

#define LTE_PWRKEY_PIN 5
#define LTE_RESET_PIN 6
#define LTE_FLIGHT_PIN 7

String sendData(String command, const int timeout, boolean debug = false) {
  String response = "";
  Serial1.println(command);

  long int startTime = millis();
  while (millis() - startTime < timeout) {
    if (Serial1.available()) {
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

void setup() {
  SerialUSB.begin(115200);
  Serial1.begin(115200);

  // Initialize LTE module
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

  SerialUSB.println("Initializing GPRS module...");

  // Reset the module
  String response = sendData("AT+CFUN=1,1", 5000, DEBUG); // Full reset
  delay(10000); // Wait for the module to reset

  // Disable echo
  response = sendData("ATE0", 2000, DEBUG);
  if (response.indexOf("OK") != -1) {
    SerialUSB.println("Echo disabled successfully.");
  } else {
    SerialUSB.println("Failed to disable echo.");
  }

  // Wait for the module to be ready
  while (response.indexOf("RDY") == -1) {
    response = sendData("", 2000, DEBUG);
  }
  SerialUSB.println("Module is ready.");

  // Wait for SIM card to be ready
  response = sendData("AT+CPIN?", 2000, DEBUG);
  while (response.indexOf("+CPIN: READY") == -1) {
    delay(1000);
    response = sendData("AT+CPIN?", 2000, DEBUG);
  }
  SerialUSB.println("SIM card is ready.");

  // Set APN
  response = sendData("AT+CGDCONT=1,\"IP\",\"bsnlnet\"", 3000, DEBUG);
  if (response.indexOf("OK") != -1) {
    SerialUSB.println("APN set successfully.");
  } else {
    SerialUSB.println("Failed to set APN.");
  }

  // Attach to the GPRS network
  response = sendData("AT+CGATT=1", 5000, DEBUG);
  if (response.indexOf("OK") != -1) {
    SerialUSB.println("GPRS attached successfully.");
  } else {
    SerialUSB.println("Failed to attach to GPRS.");
  }

  // Activate the PDP context
  response = sendData("AT+CGACT=1,1", 5000, DEBUG);
  if (response.indexOf("OK") != -1) {
    SerialUSB.println("PDP context activated successfully.");
  } else {
    SerialUSB.println("Failed to activate PDP context.");
  }

  // Check IP address
  response = sendData("AT+CGPADDR=1", 3000, DEBUG);
  if (response.indexOf("OK") != -1) {
    SerialUSB.println("IP address obtained successfully.");
  } else {
    SerialUSB.println("Failed to obtain IP address.");
  }
}

void loop() {
  // You can add additional functionality here if needed
}
