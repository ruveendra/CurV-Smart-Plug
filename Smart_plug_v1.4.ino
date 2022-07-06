#include <FS.h>                   // this needs to be first
#include <WiFiManager.h>          

#ifdef ESP32
#include <SPIFFS.h>
#endif

#include <ArduinoJson.h>          
#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>

#include <WiFiClient.h>
#include <ESP8266WebServer.h>
#include <ESP8266HTTPClient.h>

//WiFiServer server(80);

char apikey[20];
char deviceid[10];

String api_key;
String device_id;

int resetbuttonpin = 5;
int device_switch_pin = 2;
int value = LOW;

const long interval1 = 2000;
const long interval2 = 5000;
const long interval3 = 10000;

unsigned long previousinterval1;
unsigned long previousinterval2;
unsigned long previousinterval3;


//#define SERVICE_PORT 80  // HTTP port

#ifndef STASSID
#define STASSID ""
#define STAPSK  ""
#endif
const char* ssid      = STASSID;
const char* password  = STAPSK;

const char* hostGet = "34.222.56.94";
//const char* hostGet = "192.168.1.29";
const int httpGetPort = 5000;

int readValue;
int maxValue = 0;
int minValue = 1024;
float Vpp;
float Vrms;
float current;
float ampere;

String url;


//flag for saving data
bool shouldSaveConfig = false;

//callback notifying us of the need to save config
void saveConfigCallback () {
  Serial.println("Should save config");
  shouldSaveConfig = true;
}

void setup() {
  
  Serial.begin(115200);
  Serial.println();

  //clean FS, for testing              
  //  SPIFFS.format();

  //read configuration from FS json
  Serial.println("mounting FS...");

  if (SPIFFS.begin()) {
    Serial.println("mounted file system");
    if (SPIFFS.exists("/config.json")) {
      //file exists, reading and loading
      Serial.println("reading config file");
      File configFile = SPIFFS.open("/config.json", "r");
      if (configFile) {
        Serial.println("opened config file");
        size_t size = configFile.size();
        // Allocate a buffer to store contents of the file.
        std::unique_ptr<char[]> buf(new char[size]);

        configFile.readBytes(buf.get(), size);

#ifdef ARDUINOJSON_VERSION_MAJOR >= 6
        DynamicJsonDocument json(1024);
        auto deserializeError = deserializeJson(json, buf.get());
        serializeJson(json, Serial);
        if ( ! deserializeError ) {
#else
        DynamicJsonBuffer jsonBuffer;
        JsonObject& json = jsonBuffer.parseObject(buf.get());
        json.printTo(Serial);
        if (json.success()) {
#endif
          Serial.println("\nparsed json");
          strcpy(apikey, json["apikey"]);
          strcpy(deviceid, json["deviceid"]);
          //          strcpy(statusledpin, json["statusledpin"]);
        } else {
          Serial.println("failed to load json config");
        }
        configFile.close();
      }
    }
  } else {
    Serial.println("failed to mount FS");
  }

  WiFiManagerParameter custom_apikey("apikey", "Enter APIKEY Here", apikey, 16);
  WiFiManagerParameter custom_deviceid("deviceid", "Enter Device ID Here", deviceid, 8);


  //WiFiManager
  WiFiManager wifiManager;

  //set config save notify callback
  wifiManager.setSaveConfigCallback(saveConfigCallback);

  //set static ip
  //wifiManager.setSTAStaticIPConfig(IPAddress(10, 0, 1, 99), IPAddress(10, 0, 1, 1), IPAddress(255, 255, 255, 0));

  //add all your parameters here

  wifiManager.addParameter(&custom_apikey);
  wifiManager.addParameter(&custom_deviceid);
 

  //sets timeout until configuration portal gets turned off
  //in seconds
  wifiManager.setTimeout(300);

  //fetches ssid and pass and tries to connect
  //if it does not connect it starts an access point with the specified name
  //here  "AutoConnectAP-chipid"
  //and goes into a blocking loop awaiting configuration

#define ESP_getChipId()   (ESP.getChipId())
  String soft_ap_ssid = "SmartHomeDefault-" + String(ESP_getChipId(), HEX);  // wifi accesspoint name broadcasted in the air make it unique with chipid
  char buf[25];
  soft_ap_ssid.toCharArray(buf, 25);                    // i dont know how strings an chars work could/should be done easier
  if (!wifiManager.autoConnect(buf, "")) {              // password field is left empty "" no password
    Serial.println("failed to connect and hit timeout");
    delay(3000);
    //reset and try again, or maybe put it to deep sleep
    ESP.restart();
    delay(5000);
  }

  //if you get here you have connected to the WiFi
  Serial.println("connected...yipee..yeey! :)");

  //read updated parameters

  strcpy(apikey, custom_apikey.getValue());
  strcpy(deviceid, custom_deviceid.getValue());


  Serial.println("The values in the file are: ");

  Serial.println("\tapikey : " + String(apikey));
  Serial.println("\tbuttonpin : " + String(deviceid));
  //  Serial.println("\tstatusledpin : " + String(statusledpin));

  //save the custom parameters to FS
  if (shouldSaveConfig) {
    Serial.println("saving config");
#ifdef ARDUINOJSON_VERSION_MAJOR >= 6
    DynamicJsonDocument json(1024);
#else
    DynamicJsonBuffer jsonBuffer;
    JsonObject& json = jsonBuffer.createObject();
#endif

    json["apikey"] = apikey;
    json["deviceid"] = deviceid;
    //    json["statusledpin"] = statusledpin;

    File configFile = SPIFFS.open("/config.json", "w");
    if (!configFile) {
      Serial.println("failed to open config file for writing");
    }

#ifdef ARDUINOJSON_VERSION_MAJOR >= 6
    serializeJson(json, Serial);
    serializeJson(json, configFile);
#else
    json.printTo(Serial);
    json.printTo(configFile);
#endif
    configFile.close();
    //end save
  }



  api_key = String(apikey);
  device_id = String(deviceid);


  pinMode(resetbuttonpin, INPUT);
  pinMode(device_switch_pin, OUTPUT);
  digitalWrite(device_switch_pin, LOW);

}

float current_sensor() {
  uint32_t startTime = millis();

  /* and start a loop for one second to determine the max and min sensor value */
  while ( millis() - startTime < 1000) {
    /* Read sensor value */
    readValue = analogRead(A0);

    /* Determine max value */
    if (readValue > maxValue) maxValue = readValue;

    /* Determine min value */
    if (readValue < minValue) minValue = readValue;
  }

  /* Then calculate the Vpp */
  Vpp = ((maxValue - minValue) * 3.3) / 1024.0;

  /* Determine the Vrms */
  Vrms = (Vpp / 2.0) * 0.707;

  /*
    Then find the current using sensitivity of sensor
    It is 185mV/A for 5A, 100 mV/A for 20A and 66mV/A for 30A Module
    We are using 5A sensor
  */
  current = (Vrms * 1000.0) / 100.0;
  Serial.println(current);

  /* Finally calculate the power considering ideal state with pure resistive load */
  return current;
}


void postData() {

  WiFiClient clientGet;


  //the path and file to send the data to:
  String urlGet = "/api/v1/sensor/";

//  ampere = 0.17;

  urlGet += "?api_key=" + api_key + "&device_id=" + device_id + "&ampere=" + ampere;


  if (!clientGet.connect(hostGet, httpGetPort)) {
    Serial.print("Connection failed: ");
    Serial.print(hostGet);
  } else {
    clientGet.println("GET " + urlGet + " HTTP/1.0");
    clientGet.print("Host: ");
    clientGet.println(hostGet);
    clientGet.println("User-Agent: ESP8266/1.1");
    clientGet.println("Connection: close\r\n\r\n");

    unsigned long timeoutP = millis();
    while (clientGet.available() == 0) {

      if (millis() - timeoutP > 10000) {
        Serial.print(">>> Client Timeout: ");
        Serial.println(hostGet);
        clientGet.stop();
        return;
      }
    }

    //just checks the 1st line of the server response. Could be expanded if needed.
    while (clientGet.available()) {
      String retLine = clientGet.readStringUntil('\r');
      //      Serial.println(retLine);
      break;
    }

  } //end client connection if else

  //  Serial.print(">>> Closing host: ");
  //  Serial.println(hostGet);

  clientGet.stop();
  Serial.println("Data Sent successfully");

}

void device_switch() {

  WiFiClient client;
  HTTPClient http;
  url = "http://" + String(hostGet) + ":" + String(httpGetPort) + "/api/v1/devices/read-switch-status?api_key=" + String(api_key) + "&device_id=" + String(device_id);
  http.begin(client, url);
  int httpCode = http.GET();

  if (httpCode > 0)
  {
    StaticJsonDocument<64> doc;
    DeserializationError error = deserializeJson(doc, http.getString());

    if (error) {
      Serial.print(F("deserializeJson() failed: "));
      Serial.println(error.f_str());
      return;
    }

    bool data = doc["data"]; // true

//    Serial.print("Name:");
//    Serial.println(data);

    if (data == 0) {
      digitalWrite(device_switch_pin, HIGH);
      //      Serial.println("LOW");
    } else {
      digitalWrite(device_switch_pin, LOW);
      //      Serial.println("HIGH");
    }
    http.end(); //Close connection
    Serial.println("Device Switch Status Checked");
  }
}

void set_online_status() {
  WiFiClient client;
  HTTPClient http;
  url = "http://" + String(hostGet) + ":" + String(httpGetPort) + "/api/v1/devices/read-switch-status?api_key=" + String(api_key) + "&device_id=" + String(device_id) + "&status=true";
  http.begin(client, url);
  int httpCode = http.GET();

  if (httpCode > 0) {

    http.end(); //Close connection
    Serial.println("Online Status Set");
  }
}


void loop() {
  // put your main code here, to run repeatedly:

  unsigned long currentMillis = millis();

  if ((currentMillis - previousinterval1) >= interval1) {

    Serial.print("--------------------------------");
    Serial.println(interval1);
    set_online_status();
    Serial.println("--------------------------------");
    previousinterval1 = currentMillis;

  }

  if ((currentMillis - previousinterval2) >= interval2) {

    Serial.print("--------------------------------");
    Serial.println(interval2);
    device_switch();
    Serial.println("--------------------------------");
    previousinterval2 = currentMillis;
  }


  if (currentMillis - previousinterval3 >= interval3) {

    Serial.print("--------------------------------");
    Serial.println(interval3);
    ampere = current_sensor();
    delay(10);
    postData();
    Serial.println("--------------------------------");

    previousinterval3 = currentMillis;
  }
  


//  if (digitalRead(resetbuttonpin) == HIGH) {
//
//    WiFiManager wifiManager;
//    delay(50);
//    wifiManager.resetSettings();
//    delay(50);
//    SPIFFS.format();
//    delay(100);
//    ESP.restart();
//
//  }

}
