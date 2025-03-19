#include <Arduino.h>
#include <WiFi.h>
#include <Firebase_ESP_Client.h>
#include "time.h"
#include "DHT.h"

// WiFi credentials
//#define WIFI_SSID "TP-Link_90C8"
//#define WIFI_PASSWORD "14539787"
#define WIFI_SSID "HelloIOT"
#define WIFI_PASSWORD "HelloWorld"

// Firebase credentials
#define API_KEY ""
#define USER_EMAIL ""
#define USER_PASSWORD ""
#define DATABASE_URL ""

// Sensor and LED pins
#define DHT_PIN 21
#define DHT_TYPE DHT11
#define LIGHT_PIN 34
#define LED1_PIN 25
#define LED2_PIN 26
#define LED3_PIN 27

// Firebase objects
FirebaseData fbdo;
FirebaseAuth auth;
FirebaseConfig config;
FirebaseData streamData;

// User information
String uid;
String databasePath;
String currentDataPath;
String dataHistoryPath;
String currentDevicePath;

const char* ntpServer = "pool.ntp.org"; 

// Timer settings
unsigned long sendDataPrevMillis = 0;
const unsigned long timerDelay = 3000; 
const int maxDataCount = 100; 

DHT dht(DHT_PIN, DHT_TYPE);

// Function prototypes
void initWiFi();
unsigned long getEpochTime();
void streamCallback(FirebaseStream data);
void checkCurrentDeviceStatus();
void streamTimeoutCallback(bool timeout);
void manageDataLimit();
void updateLEDStatus(const String& path, bool status);
void tokenStatusCallback(TokenInfo info);  

void setup() {
    Serial.begin(115200);
    initWiFi();
    configTime(3600*7, 0, ntpServer);

    struct tm timeinfo;
    while(!getLocalTime(&timeinfo)) {
        Serial.println("Failed to obtain time from NTP server. Retrying...");
        delay(1000); 
    }
    Serial.println("Time updated successfully from NTP server.");

    // Configure Firebase
    config.api_key = API_KEY;
    auth.user.email = USER_EMAIL;
    auth.user.password = USER_PASSWORD;
    config.database_url = DATABASE_URL;
    config.token_status_callback = tokenStatusCallback;
    config.max_token_generation_retry = 5;
    
    Firebase.reconnectWiFi(true);
    fbdo.setResponseSize(4096);
    Firebase.begin(&config, &auth);

    // Get user UID
    Serial.println("Getting User UID");
    while (auth.token.uid == "") {
        Serial.print('.');
        delay(1000);
    }
    uid = auth.token.uid.c_str();
    Serial.printf("User UID: %s\n", uid.c_str());

    // Set paths
    databasePath = "/" + uid;
    currentDataPath = databasePath + "/currentData";
    currentDevicePath = databasePath + "/currentDevice";
    dataHistoryPath = databasePath + "/dataHistory";

    // Initialize hardware
    pinMode(LED1_PIN, OUTPUT);
    pinMode(LED2_PIN, OUTPUT);
    pinMode(LED3_PIN, OUTPUT);
    dht.begin();

    // Check initial device status
    checkCurrentDeviceStatus();

    // Start listening for changes in current device status
    if (!Firebase.RTDB.beginStream(&streamData, currentDevicePath)) {
        Serial.printf("Stream begin error: %s\n", streamData.errorReason().c_str());
    }
    Firebase.RTDB.setStreamCallback(&streamData, streamCallback, streamTimeoutCallback);
}

void loop() {
    if (Firebase.ready() && (millis() - sendDataPrevMillis > timerDelay || sendDataPrevMillis == 0)) {
        sendDataPrevMillis = millis();

        // Collect sensor data
        float temperature = dht.readTemperature();
        float humidity = dht.readHumidity();
        float light = 4095 - analogRead(LIGHT_PIN);
        unsigned long timestamp = getEpochTime();

        // Update current data in Firebase
        FirebaseJson json;
        json.set("/temperature", temperature);
        json.set("/humidity", humidity);
        json.set("/light", light);
        json.set("/timestamp", timestamp);

        if (Firebase.RTDB.setJSON(&fbdo, currentDataPath.c_str(), &json)) {
            Serial.println("Current data updated.");
        } else {
            Serial.printf("Error updating current data: %s\n", fbdo.errorReason().c_str());
        }

        // Add data to history with timestamp as key
        if (Firebase.RTDB.setJSON(&fbdo, (dataHistoryPath + "/" + String(timestamp)).c_str(), &json)) {
            Serial.println("New data added to history.");
        } else {
            Serial.printf("Error adding data to history: %s\n", fbdo.errorReason().c_str());
        }
    }
}

void initWiFi() {
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    Serial.print("Connecting to WiFi");
    while (WiFi.status() != WL_CONNECTED) {
        Serial.print('.');
        delay(1000);
    }
    Serial.println("\nConnected to WiFi. IP Address: " + WiFi.localIP().toString());
}

unsigned long getEpochTime() {
    time_t now;
    struct tm timeinfo;
    if (!getLocalTime(&timeinfo)) {
        Serial.println("Failed to obtain time");
        return 0;
    }
    time(&now);
    return now;
}

void streamCallback(FirebaseStream data) {
    Serial.printf("Stream Path: %s\n", data.streamPath().c_str());
    Serial.printf("Event Path: %s\n", data.dataPath().c_str());
    Serial.printf("Data Type: %s\n", data.dataType().c_str());
    Serial.printf("Event Type: %s\n", data.eventType().c_str());

    if (data.dataTypeEnum() == fb_esp_rtdb_data_type_boolean) {
        updateLEDStatus(data.dataPath(), data.boolData());
    }
}

void updateLEDStatus(const String& path, bool status) {
    if (path == "/light") {
        digitalWrite(LED1_PIN, status ? HIGH : LOW);
        Serial.println(status ? "LED 1 (Light) is ON" : "LED 1 (Light) is OFF");
    } else if (path == "/ac") {
        digitalWrite(LED2_PIN, status ? HIGH : LOW);
        Serial.println(status ? "LED 2 (AC) is ON" : "LED 2 (AC) is OFF");
    } else if (path == "/tv") {
        digitalWrite(LED3_PIN, status ? HIGH : LOW);
        Serial.println(status ? "LED 3 (TV) is ON" : "LED 3 (TV) is OFF");
    }
}

void checkCurrentDeviceStatus() {
    if (Firebase.RTDB.getJSON(&fbdo, currentDevicePath.c_str())) {
        FirebaseJson& json = fbdo.jsonObject();
        FirebaseJsonData jsonData;
        bool ledLight = false, ledAc = false, ledTv = false;

        if (json.get(jsonData, "/light")) ledLight = jsonData.boolValue;
        if (json.get(jsonData, "/ac")) ledAc = jsonData.boolValue;
        if (json.get(jsonData, "/tv")) ledTv = jsonData.boolValue;

        updateLEDStatus("/light", ledLight);
        updateLEDStatus("/ac", ledAc);
        updateLEDStatus("/tv", ledTv);
    } else {
        Serial.printf("Error getting current device status: %s\n", fbdo.errorReason().c_str());
    }
}

void streamTimeoutCallback(bool timeout) {
    Serial.println(timeout ? "Stream timeout, reconnecting..." : "Stream error");
}

void tokenStatusCallback(TokenInfo info) {
    // Implement callback logic if needed, or leave as a placeholder
}
