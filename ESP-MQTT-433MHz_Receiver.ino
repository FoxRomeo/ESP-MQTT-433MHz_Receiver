/*
 433MHz receiver based on ESP8266/NodeMCU with MQTT
    Copyright (C) 2016  Oliver Faßbender

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/
//------------------------------------------------------------------------------------------------
const String NAME = "ESP433MHzReceiver";
const String VERSION = "V 0.0.3";
//------------------------------------------------------------------------------------------------
/* 
 *  internal blue LED     D10 only usable/controlable without USB UART
 *  433MHz receiver       D1
 *  USB/Flashing          D9
 *                        D10
 *  Free (check reset)    D8 // GPIO15 must be low for reset (bad with I2C pullups)
 *                        D4 // GPIO2 sometimes used for buildin LED (must be high for reset)
 *                        D3 // GPIO0 must be low for reset
 */
//------------------------------------------------------------------------------------------------
//------------------------------------------------------------------------------------------------
#include "local_conf.h"
//------------------------------------------------------------------------------------------------
//------------------------------------------------------------------------------------------------
#include <FS.h>                   //this needs to be first, or it all crashes and burns...
#include <ESP8266WiFi.h>          //https://github.com/esp8266/Arduino
#include <DNSServer.h>
#include <ESP8266WebServer.h>
#include <WiFiManager.h>          //https://github.com/tzapu/WiFiManager
//------------------------------------------------------------------------------------------------
#include <RCSwitch.h>
//------------------------------------------------------------------------------------------------
// multicast DNS responder / Zeroconf
#ifdef WITH_MDNS
#include <ESP8266mDNS.h>
MDNSResponder mdns;
#endif //WITH_MDNS
//------------------------------------------------------------------------------------------------
#include <ArduinoJson.h>          //https://github.com/bblanchon/ArduinoJson
#include "Adafruit_MQTT.h"        // Required Adafruit MQTT Lib Version >=0.16.1
#include "Adafruit_MQTT_Client.h"
#ifndef USE_SSL
 WiFiClient client;
#else
 WiFiClientSecure client;
#endif
String ipStr = "0.0.0.0";
Adafruit_MQTT_Client mqtt(&client, MQTT_SERVER, MQTT_SERVERPORT, MQTT_USERNAME, MQTT_PASSWORD);
#define RECEIVER_TEXT_FEED_PATH  CHANNEL_BASENAME "/text"
Adafruit_MQTT_Publish receiver_publish_text = Adafruit_MQTT_Publish(&mqtt, RECEIVER_TEXT_FEED_PATH );
#define RECEIVER_BIN_FEED_PATH  CHANNEL_BASENAME "/bin"
Adafruit_MQTT_Publish receiver_publish_bin = Adafruit_MQTT_Publish(&mqtt, RECEIVER_BIN_FEED_PATH );
#define BUFFER_MAX 100
char buffer[BUFFER_MAX];
//------------------------------------------------------------------------------------------------
unsigned long ping_currentMillis = 0;
unsigned long ping_previousMillis = 0;
const long ping_interval = 10L * 1000L; // delay between MQTT pings, in milliseconds      
//------------------------------------------------------------------------------------------------
unsigned long last_send2mqtt_currentMillis = 0;
unsigned long last_send2mqtt_previousMillis = 0;
const unsigned long last_send2mqtt_interval = 3L * 1000L; // reset last_received value, in milliseconds      
unsigned long last_send2mqtt = 0;
//------------------------------------------------------------------------------------------------
#ifdef USE_SSL
 //flag for saving data
 bool shouldSaveConfig = false;
 // SSL fingerprint (SHA1?)
 #define SSL_HASH_MAX 100
 char ssl_hash[SSL_HASH_MAX] = "";
#endif //USE_SSL
//------------------------------------------------------------------------------------------------
RCSwitch mySwitch = RCSwitch();
//############################
#ifdef USE_SSL
 //callback notifying us of the need to save config
 void saveConfigCallback () {
  #ifdef DEBUG
    Serial.println("Should save config");
  #endif //DEBUG
  shouldSaveConfig = true;
 }
//############################
void verifyFingerprint() {
  const char* host = MQTT_SERVER;
  char* hash = ssl_hash;
  #ifdef DEBUG
    Serial.print("Connecting to ");
    Serial.println(host);
    Serial.print("Verify Hash: ");
    Serial.println(hash);
  #endif //DEBUG
  if (! client.connect(host, MQTT_SERVERPORT)) {
    #ifdef DEBUG
      Serial.println("Connection failed. Halting execution.");
    #endif //DEBUG
    // basically die and wait for WDT to reset me
    while (1);
  }
  if (client.verify(hash, host)) {
    #ifdef DEBUG
      Serial.println("Connection secure.");
    #endif //DEBUG
  } else {
    #ifdef DEBUG
      Serial.println("Connection insecure! Halting execution.");
    #endif //DEBUG
    // basically die and wait for WDT to reset me
    while(1);
  }
}
#endif //USE_SSL
//############################
static char * dec2binWzerofill(unsigned long Dec, unsigned int bitLength){
  static char bin[64]; 
  unsigned int i=0;
  while (Dec > 0) {
    bin[32+i++] = (Dec & 1 > 0) ? '1' : '0';
    Dec = Dec >> 1;
  }
  for (unsigned int j = 0; j< bitLength; j++) {
    if (j >= bitLength - i) {
      bin[j] = bin[ 31 + i - (j - (bitLength - i)) ];
    }else {
      bin[j] = '0';
    }
  }
  bin[bitLength] = '\0';
  return bin;
}
//############################
// Function to connect and reconnect as necessary to the MQTT server.
// Should be called in the loop function and it will take care if connecting.
void MQTT_connect() {
  int8_t ret;
  // Stop if already connected.
  if (mqtt.connected()) {
    return;
  }
  #ifdef DEBUG
    Serial.print(F("Connecting to MQTT... "));
  #endif //DEBUG
  uint8_t retries = 3;
  while ((ret = mqtt.connect()) != 0) { 
   // connect will return 0 for connected
   #ifdef DEBUG
     Serial.println(mqtt.connectErrorString(ret));
     Serial.println(F("Retrying MQTT connection in 500 milliseconds..."));
   #endif //DEBUG
   mqtt.disconnect();
   delay(500);  // wait 
   retries--;
   if (retries == 0) {
    // basically die and wait for WDT to reset me
    while (1);
   }
  }
  // check the fingerprint of io.adafruit.com's SSL cert
  #ifdef VERIFY_SSL
    verifyFingerprint();
  #endif //VERIFY_SSL
  #ifdef DEBUG
    Serial.println("MQTT Connected!");
  #endif //DEBUG
}
//############################
void output(unsigned long decimal, unsigned int length, unsigned int delay, unsigned int* raw, unsigned int protocol) {
  #ifdef DEBUG
    if (decimal == 0) {
      Serial.println("Unknown encoding.");
    } else {
      char* b = dec2binWzerofill(decimal, length);
      Serial.print("Decoding -> Decimal: ");
      Serial.print(decimal);
      Serial.print(" (");
      Serial.print( length );
      Serial.print("Bit) Binary: ");
      Serial.print( b );
      Serial.print(" PulseLength: ");
      Serial.print(delay);
      Serial.print(" microseconds");
      Serial.print(" Protocol: ");
      Serial.println(protocol);
    } 
  #endif //DEBUG
    if (decimal > 0) {
      char* b = dec2binWzerofill(decimal, length);
      if ( (int)length >= (int)MINIMUM_BITS ) {
      String mqtt_text;
      mqtt_text += "Sending => Decimal: ";
      mqtt_text += (int)decimal;
      mqtt_text += " (";
      mqtt_text += (int)length;
      mqtt_text += "Bit) Binary: ";
      mqtt_text += b;
      mqtt_text += " PulseLength: ";
      mqtt_text += (int)delay;
      mqtt_text += " microseconds Protocol: ";
      mqtt_text += (int)protocol;
      #ifdef DEBUG
        Serial.println( mqtt_text );
      #endif //DEBUG
      if ( !receiver_publish_bin.publish( b ) ) {
        #ifdef DEBUG
          Serial.println(F("Sending binary failed"));
        #endif //DEBUG
      }
      mqtt_text.toCharArray(buffer, BUFFER_MAX);
      if ( !receiver_publish_text.publish( buffer ) ) {
        #ifdef DEBUG
          Serial.println(F("Sending text failed"));
        #endif //DEBUG
      }
    }
  }
  #ifdef DEBUG
    Serial.println();
  #endif //DEBUG
}
//############################
//############################
void setup() {
  #ifdef DEBUG
   Serial.begin(115200); 
   delay(500); // wait for uart to settle and print Espressif blurb..
   Serial.println(NAME);
   Serial.println(VERSION);
   Serial.println("serial init done");
   // print out all system information
   Serial.print("Heap: "); 
   Serial.println(system_get_free_heap_size());
   Serial.print("Boot Vers: "); 
   Serial.println(system_get_boot_version());
   Serial.print("CPU: "); 
   Serial.println(system_get_cpu_freq());
  #endif //DEBUG
  pinMode(RECEIVER_PIN,INPUT);
  mySwitch.enableReceive(RECEIVER_PIN);
  #ifdef DEBUG
   Serial.println("receiver init done");
   Serial.println("Connect WIFI");
  #endif //DEBUG
  //---------------------
  //WiFiManager
  #ifdef USE_SSL
    //read configuration from FS json
    #ifdef DEBUG
      Serial.println("mounting FS...");
    #endif //DEBUG
    if (SPIFFS.begin()) {
      #ifdef DEBUG
        Serial.println("mounted file system");
      #endif //DEBUG
      if (SPIFFS.exists("/config.json")) {
        //file exists, reading and loading
        #ifdef DEBUG
          Serial.println("reading config file");
        #endif //DEBUG
        File configFile = SPIFFS.open("/config.json", "r");
        if (configFile) {
          #ifdef DEBUG
            Serial.println("opened config file");
          #endif //DEBUG
          size_t size = configFile.size();
          // Allocate a buffer to store contents of the file.
          std::unique_ptr<char[]> buf(new char[size]);
          configFile.readBytes(buf.get(), size);
          DynamicJsonBuffer jsonBuffer;
          JsonObject& json = jsonBuffer.parseObject(buf.get());
          #ifdef DEBUG
            json.printTo(Serial);
          #endif //DEBUG
          if (json.success()) {
            #ifdef DEBUG
              Serial.println("\nparsed json");
            #endif //DEBUG
            strcpy(ssl_hash, json["ssl_hash"]);
          } else {
            #ifdef DEBUG
              Serial.println("failed to load json config");
            #endif //DEBUG
          }
        }
      }
    } else {
      #ifdef DEBUG
        Serial.println("failed to mount FS");
      #endif //DEBUG
    }
    //end read FS
    //----
    // The extra parameters to be configured (can be either global or just in the setup)
    // After connecting, parameter.getValue() will get you the configured value
    // id/name placeholder/prompt default length
    WiFiManagerParameter custom_ssl_hash_token("ssl", "ssl hash", ssl_hash, SSL_HASH_MAX);
  #endif //USE_SSL
  //------------------
  //Local intialization. Once its business is done, there is no need to keep it around
  WiFiManager wifiManager;
  //set minimu quality of signal so it ignores AP's under that quality
  //defaults to 8%
  wifiManager.setMinimumSignalQuality();
  //sets timeout until configuration portal gets turned off
  //useful to make it all retry or go to sleep
  //in seconds
  wifiManager.setTimeout(300);
  //set hostname
  #ifdef HOSTNAME
    WiFi.hostname(HOSTNAME);
  #else
    WiFi.hostname(NAME);
  #endif
  #ifdef USE_SSL
    //set config save notify callback
    wifiManager.setSaveConfigCallback(saveConfigCallback);
    //add all your parameters here
    wifiManager.addParameter(&custom_ssl_hash_token);
  #endif //USE_SSL  
  //fetches ssid and pass and tries to connect
  //if it does not connect it starts an access point with the specified name
  //here  "AutoConnectAP"
  //and goes into a blocking loop awaiting configuration
  if (!wifiManager.autoConnect("ESPConnectAP", "ESP8266")) {
    #ifdef WITH_OLED
     Draw_WAVES();
    #endif //WITH_OLED
    #ifdef DEBUG
      Serial.println("failed to connect and hit timeout");
    #endif // DEBUG
    //reset and try again, or maybe put it to deep sleep
    delay(3000);
    ESP.reset();
    delay(5000);
  }
  //if you get here you have connected to the WiFi
  #ifdef DEBUG
    Serial.println("Connected OK");
  #endif //DEBUG
  //
  // Set up mDNS responder:
  // - first argument is the domain name
  // - second argument is the IP address to advertise
  //   we send our IP address on the WiFi network
  #ifdef WITH_MDNS
   #ifdef WITH_OLED
    sendStrXY("Init mDNS", 5, 1); // 16 Character max per line with font set
   #endif //WITH_OLED
   if (!mdns.begin(CHANNEL_BASENAME, WiFi.localIP())) {
     Serial.println(F("Error setting up MDNS responder!"));
   }
   else
   {
     Serial.println(F("mDNS responder started"));  
   }
   //
   delay(1000);
  #endif //WITH_MDNS
  //
  #ifdef USE_SSL
    //read updated parameters
    String ssl_string = custom_ssl_hash_token.getValue();
    // strcpy(ssl_hash, custom_ssl_hash_token.getValue());
    #ifdef DEBUG
      Serial.println(ssl_hash);
    #endif //DEBUG
    if ( ssl_string.startsWith("&#8206;", 0) ) {
      ssl_string.remove(0,7); // "&#8206;"
    }
    int ssl_length;
    if (ssl_string.length() < SSL_HASH_MAX) {
      ssl_length = ssl_string.length() + 1;
    }
    else {
      #ifdef DEBUG
        Serial.print(F("ERROR: SSL hash >="));
        Serial.print(SSL_HASH_MAX);
        Serial.println(F("characters!"));
      #endif //DEBUG
      ssl_length = SSL_HASH_MAX;
    }
    ssl_string.toCharArray(ssl_hash, ssl_length);
    //save the custom parameters to FS
    if (shouldSaveConfig) {
      #ifdef DEBUG
        Serial.println("saving config");
      #endif //DEBUG
      DynamicJsonBuffer jsonBuffer;
      JsonObject& json = jsonBuffer.createObject();
      json["ssl_hash"] = ssl_hash;
      File configFile = SPIFFS.open("/config.json", "w");
      #ifdef DEBUG
        if (!configFile) {
          Serial.println("failed to open config file for writing");
        }
        json.printTo(Serial);
      #endif //DEBUG
      json.printTo(configFile);
      configFile.close();
      //end save
    }
  #endif //USE_SSL
  //---------
  MQTT_connect();
}
//############################
//############################
void loop() {
  // Ensure the connection to the MQTT server is alive (this will make the first
  // connection and automatically reconnect when disconnected).  See the MQTT_connect
  // function definition further below.
  MQTT_connect();
  //
  last_send2mqtt_currentMillis = millis();
  if(last_send2mqtt_currentMillis - last_send2mqtt_previousMillis >= last_send2mqtt_interval) {
    last_send2mqtt_previousMillis = last_send2mqtt_currentMillis;   
    last_send2mqtt = 0;
  }
  if (mySwitch.available()) {
    if ( (int)last_send2mqtt != (int)mySwitch.getReceivedValue() ) {
      output( mySwitch.getReceivedValue(), mySwitch.getReceivedBitlength(), mySwitch.getReceivedDelay(), mySwitch.getReceivedRawdata(),mySwitch.getReceivedProtocol() );
      last_send2mqtt = mySwitch.getReceivedValue();
    }
    mySwitch.resetAvailable();
  }
  // ping the server to keep the mqtt connection alive
  ping_currentMillis = millis();
  if(ping_currentMillis - ping_previousMillis >= ping_interval) {
    ping_previousMillis = ping_currentMillis;   
    if(! mqtt.ping()) {
      mqtt.disconnect();
    }  
  }
  delay (25);
}

