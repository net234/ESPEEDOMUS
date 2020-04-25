// Gestion de WifiManager

#include <WiFiManager.h>        // Gestion du mode AP pour configurer l'acces au WIFI  //https://github.com/tzapu/WiFiManager WiFi 
#include <Ticker.h>           // Uniquement pour le clignotement led durant le mode AP


Ticker ticker;


void tick()
{
  //toggle Led
  digitalWrite(LED_BUILTIN, !digitalRead(LED_BUILTIN));     // set pin to the opposite state
}




// un callback appelé au debut du mode AP
// on active un ticker pour signaler le MOde AP avec un clignotement rapide de led
void configModeCallback (WiFiManager *myWiFiManager) {
  Serial.println("Mode config");
  ticker.attach(0.1, tick);
  //  Serial.println(WiFi.softAPIP());
  //  Serial.println(myWiFiManager->getConfigPortalSSID());
}

// call back sauver la config
//flag for saving data
bool shouldSaveConfig = false;

//callback notifying us of the need to save config
void saveConfigCallback () {
  shouldSaveConfig = true;
}


void ManageWifiSetup() {

  //WiFiManager
  //Local intialization. Once its business is done, there is no need to keep it around
  WiFiManager wifiManager;
  
  //reset saved settings
  //wifiManager.resetSettings();

 // remove verbiose output to serial
  wifiManager.setDebugOutput(false);
  // add specific parameters
  WiFiManagerParameter custom_hostname("hostname", "Nom du Sonoff", WifiData.hostname, WDSTRSIZE);
  wifiManager.addParameter(&custom_hostname);
  WiFiManagerParameter custom_eedomus_ip("eedomus_ip", "IP de la box eedomus", WifiData.eedomus_ip, WDSTRSIZE);
  wifiManager.addParameter(&custom_eedomus_ip);
  String aString = "Local IP :";
  aString += lastLocalIp;
  WiFiManagerParameter custom_textip(aString.c_str());
  wifiManager.addParameter(&custom_textip);  
  WiFiManagerParameter custom_text("<br>voir EEDOMUS-> mon Compte-> Parametre-> Consulter vos identifiants");
  wifiManager.addParameter(&custom_text);
  WiFiManagerParameter custom_api_user("api_user", "api_user (eedomus)", WifiData.api_user, WDSTRSIZE);
  wifiManager.addParameter(&custom_api_user);
  WiFiManagerParameter custom_api_secret("api_secret", "api_secret (eedomus)", WifiData.api_secret, WDSTRSIZE);
  wifiManager.addParameter(&custom_api_secret);
  WiFiManagerParameter custom_periph_id("periph_id", "periph_id (eedomus)", WifiData.periph_id, WDSTRSIZE);
  wifiManager.addParameter(&custom_periph_id);


  // wifiManager.setCustomHeadElement("<style>html{filter: invert(100%); -webkit-filter: invert(100%);}</style>");  // inverse color White on Balck backgroung
  WiFi.persistent(true);
  WiFiManagerParameter custom_version("<p>" ESPEEDOMUS_VERSION "</p>");
  wifiManager.addParameter(&custom_version);
  Serial.print("AutoConfig->");

  wifiManager.setConfigPortalTimeout(5 * 60);
  wifiManager.setMinimumSignalQuality(10);
  wifiManager.setSaveConfigCallback(saveConfigCallback);
  wifiManager.setAPCallback(configModeCallback);
//  wifiManager.autoConnect(WifiData.hostname);
  
  wifiManager.startConfigPortal(WifiData.hostname);
  
  ticker.detach();
  if (shouldSaveConfig) {
    strncpy(WifiData.hostname,  custom_hostname.getValue(), WDSTRSIZE);
    strncpy(WifiData.eedomus_ip, custom_eedomus_ip.getValue(), WDSTRSIZE);
    strncpy(WifiData.api_user, custom_api_user.getValue(), WDSTRSIZE);
    strncpy(WifiData.api_secret, custom_api_secret.getValue(), WDSTRSIZE);
    strncpy(WifiData.periph_id, custom_periph_id.getValue(), WDSTRSIZE);
    strncpy(WifiData.check, "USERDATA", WDSTRSIZE);
    EEPROM.put(0, WifiData);
    EEPROM.commit();
    buildHttpString();
    Serial.println("WifiData Saved");
    shouldSaveConfig = false;
  }
  Serial.println("Done");

};
