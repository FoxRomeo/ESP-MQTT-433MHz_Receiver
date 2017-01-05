/*
 * Rename this file to local_conf.ino and add your local settings.
 */

#define MQTT_SERVER       "<mqtt-hostname>"
#define MQTT_USERNAME     "<mqtt-user>"
#define MQTT_PASSWORD     "<mqtt-password>"
#define CHANNEL_BASENAME  "<mqtt-channel-basename>"

#define USE_SSL
#ifndef USE_SSL
 #define MQTT_SERVERPORT  1883                   // default non-SSL port
#else
 #define MQTT_SERVERPORT  8883                   // default SSL port
 //#define VERIFY_SSL
 // enter SSL SHA1 HASH in WiFiManager 
 // get it via "openssl x509 -fingerprint -in <key-filename>.crt"
#endif //USE_SSL
//------------------------------------------------------------------------------------------------
//#define HOSTNAME "ESP433MHzReceiver" // if not own defined NAME will be used, use HOSTNAME if more than one is used to avoid conflicts
//------------------------------------------------------------------------------------------------
#define DEBUG             // enables serial debug information
//------------------------------------------------------------------------------------------------
#define WITH_MDNS
//------------------------------------------------------------------------------------------------
#define RECEIVER_PIN D1
const int MINIMUM_BITS = 24;
//------------------------------------------------------------------------------------------------
const int blueLED = D10; 
//------------------------------------------------------------------------------------------------

