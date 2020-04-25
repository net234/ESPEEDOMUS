/*************************************************
 *************************************************
 **   ESPSonOff V1.2.4  Module de gestion d'un sononff 100% compatible eedomus
 **   Pierre HENRY  V1.2.2 15/03/2020
 **
 **   Gestion d'evenement en boucle GetEvent HandleEvent
 **
 **    V1.0     P.HENRY  13/04/2020  premier prise en main d'un ESP8266
       V1.2.4   P.HENRY  21/04/2020  Compatible EEDOMUS avec retours d'info EEDOMUS + mode WEB autonome
 **
 **/
// todo:  cross detection des autres modules present (via UDP ?)


#include <arduino.h>
#include <EEPROM.h>             // sauvegarde des donnée du formulaire dans la flash
#include <ESP8266WiFi.h>        // base d'acces au WIFI
//#include <ESP8266mDNS.h>      // not used here we stay with IP on this side (Wifimanageg need id on his side)
#include <ESP8266WebServer.h>   // web server to answer WEB request and EEDOMUS BOX request
#include <ESP8266HTTPClient.h>  // need by ESP8266WebServer
#include <WiFiClient.h>         // need for reporting change to BOX EEDOMUS
#include <Arduino_JSON.h>       // need do get clean json from box EEDOMUS
#include "PH_Events.h"

//todo    timeout sur AP
//trim sur l'autoconfig


#define ESPEEDOMUS_VERSION   "ESPSonOff V1.2.4"     //version diplay in various place
#define RELAY_PIN 12                                // GPIO pin for 220V relay
#define BP0_PIN 0                                   // GPIO pin for push button



// event used in this sketch
//enum typeEvent { evNill=0, ev100Hz, ev10Hz, ev1Hz, ev24H, evDepassement1HZ,evInChar,evInString,evUser=99 };
enum myEvent  { evBP0Down = 100 ,    // (100) pousoir enfoncé
                evBP0Up,             // (101) pousoir relaché
                evBP0Sleep,          // (102) le poussoir n'a pas changé de puis 2 seconde => BPSeepTrue
                evCheckBox,          // (103) regular event to check if state of box is same with state of sonof
                evWifiStatusChanged, // (104) le Wifi a changé connecté ou deconnecté
                evEndOfAPMode        // (105) Delay de fin de okForAPmode)
              };

// données sauvegardée en EEPROM
#define WDSTRSIZE 25
struct {
  char hostname[WDSTRSIZE]   = "";
  char eedomus_ip[WDSTRSIZE] = "";
  char api_user[WDSTRSIZE]   = "";
  char api_secret[WDSTRSIZE] = "";
  char periph_id[WDSTRSIZE]  = "";
  char check[WDSTRSIZE]      = "";
} WifiData;





void buildHttpString();
char  magicWord[WDSTRSIZE]  __attribute__ ((section (".noinit")));       // clef pour recuperer la derniere adresseIP utilisee
char  lastLocalIp[WDSTRSIZE] __attribute__ ((section (".noinit")));      //


#include "WifiManagement.h"


//Mode: STA
//PHY mode: N
//Channel: 6
//AP id: 0
//Status: 5
//Auto connect: 1
//SSID (8): mon_wifi
//Passphrase (11): ultrasecret
//BSSID set: 0




// Gestionaire d'evenemnt
EventTrack MyEvent;   // le gestionaire d'event local a ce stretch




// Variables de l'application
bool  relay = LOW;     // etat du relai (low au boot)

//bool  relaySleep = false;  // true si le relai n'a pas changé depuis 4 secondes

bool  BP0 = false;              // etat du poussoir  (true = down)
bool  BP0Sleep = true;          // true = poussoir sans changement down depuis 2 secondes
byte  compteurBP0DownFast = 0;  // nombre BP0Down en mode rapide (raz sur evBPSleep)
bool  okForAPMode = true;       // APMode possible dans les 5 premieres minutes (evEndOfAPMode)
bool  boxNeedUpdate = false;    // relay has been changed with BP0 push so need to inforn BOX
int   wifiStatus = WL_IDLE_STATUS;  // etat du WIFI
String http_eedomus_get = "";
String http_eedomus_set = "";


// Objet serveur
//MDNSResponder mdns;
ESP8266WebServer Server(80);    // Serveur HTTP
HTTPClient       Client;        // Client HHTP (retours vers l'eedomus)
WiFiClient       ClientWifi;    // needed by HTTPClient to use Wifi


// initialisation
void setup() {
  // init entree sortie
  pinMode(LED_BUILTIN, OUTPUT);
  pinMode(RELAY_PIN, OUTPUT);
  pinMode(BP0_PIN, INPUT);
  Serial.begin(9600);
  delay(500);
  WiFi.forceSleepBegin();
  delay(1000);

  Serial.println(ESPEEDOMUS_VERSION);

  // init event et serial port
  MyEvent.begin();
  Serial.println("Bonjour");
  delay(500);

  //e croquis utilise 317132 octets (33%) de l'espace de stockage de programmes. Le maximum est de 958448 octets.
  //Init wifi EEPROM data if needed
  EEPROM.begin(512);
  EEPROM.get(0, WifiData);
  // recuperation de la deriere local ip utilisée (en ram)
  if (  strncmp(magicWord, ESPEEDOMUS_VERSION, WDSTRSIZE) != 0) {
    strncpy( magicWord, ESPEEDOMUS_VERSION, WDSTRSIZE);
    strncpy( lastLocalIp, "none", WDSTRSIZE);
  }


  // Si l'EEPROM ne contient rien de valide alors je met des valeur par defaut
  if (  strcmp(WifiData.check, "EMPTYDATA") != 0 &&  strcmp(WifiData.check, "USERDATA") != 0 ) {
    // configation du nom du reseau AP : SONOFF_XX_YY  avec les 2 dernier chifre hexa de la mac adresse
    String aString = "SONOFF_";
    aString += WiFi.macAddress();
    aString.remove(8, 12);
    aString.replace(":", "");

    strncpy(WifiData.hostname, aString.c_str(), WDSTRSIZE);
    strncpy(WifiData.eedomus_ip, "", WDSTRSIZE);
    strncpy(WifiData.api_user, "", WDSTRSIZE);
    strncpy(WifiData.api_secret, "", WDSTRSIZE);
    strncpy(WifiData.periph_id, "", WDSTRSIZE);
    strncpy(WifiData.check, "EMPTYDATA", WDSTRSIZE);



    EEPROM.put(0, WifiData);
    EEPROM.commit();
    Serial.println("INIT WifiData");

  }
  // Etat de l'EEPROM
  Serial.print("WifiData:");
  Serial.println(WifiData.check);
  if (  strcmp(WifiData.check, "EMPTYDATA") == 0 ) {
    ManageWifiSetup();  // if never convigured do setup
  }

  Serial.println("Set WIFI ON");
  WiFi.begin();


  //built hhtp string (run time build may be long)
  http_eedomus_set.reserve(130);
  http_eedomus_get.reserve(130);
  buildHttpString();

  // Inscriprion des call back pour les requetes HTTP
  Server.on("/sonoff/espeedomus/100", HTTPCallBack_eedomusOn); // gestion d'une commande SONOFF ON envoyée par l'eedomus
  Server.on("/sonoff/espeedomus/10", HTTPCallBack_eedomusOff); // gestion d'une commande SONOFF OFF envoyée par l'eedomus
  Server.on("/action", HTTPCallBack_action);                   // formulaire pour Allumer / eteindre
  Server.on("/setup", HTTPCallBack_setup);                     // gestion de la mise a jour de la configuration
  Server.on("/sonoff/on", HTTPCallBack_relayOn);               // gestion du formulaire pour allumer
  Server.on("/sonoff/off", HTTPCallBack_relayOff);             // gestion du formulaire pour eteindre
  Server.on("/", HTTPCallBack_display);                        // affichage de l'etat du SONOFF
  Server.onNotFound(HTTPCallBack_NotFound);

  Server.begin();
  Serial.setDebugOutput(false);
  MyEvent.pushEvent(evCheckBox, 5);     // check synchro dans 5 sec
  MyEvent.pushEvent(evEndOfAPMode, 60); // 1 minute pour declencher le mode AP avec 3 pressions
}



void loop() {
  Server.handleClient ();     // handle http service
  MyEvent.GetEvent();         // get standart event
  MyEvent.HandleEvent();      // handle standart event
  switch (MyEvent.codeEvent())
  {

    // Check BP0 and generate event
    case ev10Hz:
      // gestion du pousoir : si il change on change le signal avec un envois d'evenement
      if (digitalRead(BP0_PIN) != BP0) {
        BP0 = not BP0;
        MyEvent.pushEvent( BP0 ? evBP0Up : evBP0Down);
      }
      break;

    // Check wifi status and generate event
    case ev1Hz: {
        int result = WiFi.status();
        if (result != wifiStatus) {
          wifiStatus = result;
          MyEvent.pushEvent(evWifiStatusChanged, 1);
        }
      }
      break;

    //  regulirement on relance le WiFi si il est off (c'est probablement inutile sauf au boot)
    //    case evCheckWifi:
    //      Serial.println("evCheckWifi");
    //      MyEvent.pushEvent(evCheckWifi, 5 * 60);
    //      if (wifiStatus != WL_CONNECTED && wifiWasNeverConnected) {
    //        Serial.println("Set WIFI ON");
    //        WiFi.begin();
    //
    //      }
    //      break;
    //

    // Time out of multiclick
    case evBP0Sleep:
      BP0Sleep = true;
      compteurBP0DownFast = 0;
      break;

    // BP0 Down  on first click toggle relay status
    case evBP0Down:
      Serial.println("BP0 Down");
      MyEvent.SetPulsePercent(50);
      if (BP0Sleep) {
        relay = not relay;
        UpdateRelay();
        boxNeedUpdate = true;                   // signale le report vers la box
        MyEvent.pushEventMillisec(evCheckBox, 500);       // report vers la box
      }
      BP0Sleep = false;
      compteurBP0DownFast++;
      MyEvent.pushEvent(evBP0Sleep, 2);   // reset timeout for multiclick

      // Gestion du multi click
      if (okForAPMode) {
        if (compteurBP0DownFast == 3) {
          Serial.println("ASK AP");
          Server.stop();
          ManageWifiSetup();
          compteurBP0DownFast = 5; // do reset
        }
      }
      if (compteurBP0DownFast == 5) {
        Serial.print("resetting");
        ESP.reset();
        Serial.println("--?-?--");
      }

      break;


    // la mise en mode AP est autorisée uniquement dans les X minutes apres le boot
    case evEndOfAPMode:
      Serial.println("Periode APMODE terminee");
      okForAPMode = false;
      break;


    //    // gestion pousoir relaché
    //    // rien de special
    //    case evBP0Up:
    //      //    Serial.println("BP0 Up");
    //      //    MyEvent.SetPulsePercent(10);
    //      break;



    //
    // if boxNeedUpdate  send relay to box
    // else read box to eventualy update state
    case evCheckBox:
      Serial.print("check BOX state : ");
      // if all ok we will retry this in 10 mintes
      MyEvent.pushEvent(evCheckBox, 10 * 60);

      // if no connection we cant do anything
      if (wifiStatus != WL_CONNECTED) {
        Serial.println("WIFI OFF");
        break;
      }

      if (boxNeedUpdate) {
        Serial.print("Try set box relay=");
        Serial.print(relay);
        Serial.print(" ");

        if (!setBoxStatus(relay)) {
          // if update didnt work we retry later
          Serial.println("Retry report later");
          MyEvent.pushEvent(evCheckBox, 60);
          MyEvent.SetPulsePercent(50);   // visual of no box connection
          break;
        }
        // all is ok report is done
        Serial.println("report OK");
        boxNeedUpdate = false;
        MyEvent.SetPulsePercent(relay ? 100 : 0);  // pour avoir un visuel de l'etat
        break;
      }


      // Check if box is same state than sonoff

      bool boxRelay;
      if (!getBoxStatus(&boxRelay)) {
        Serial.println("No answer from box");
        MyEvent.pushEvent(evCheckBox, 15 * 60);
        MyEvent.SetPulsePercent(50);   // visual of no box connection
        break;
      }

      if (relay != boxRelay) {
        relay = boxRelay;
        UpdateRelay();
        boxNeedUpdate = false;
        Serial.print("Updade Son Off Relay=");
        Serial.print(relay);
        Serial.println();

      } else {
        Serial.println("Box Ok");
      }

      break;




    // reporting du changement d'etat de la connection WIFI
    // Led clignotante si non connecté

    //          WL_NO_SHIELD        = 255,   // for compatibility with WiFi Shield library
    //    WL_IDLE_STATUS      = 0,
    //    WL_NO_SSID_AVAIL    = 1,
    //    WL_SCAN_COMPLETED   = 2,
    //    WL_CONNECTED        = 3,
    //    WL_CONNECT_FAILED   = 4,
    //    WL_CONNECTION_LOST  = 5,
    //    WL_DISCONNECTED     = 6

    case  evWifiStatusChanged:
      switch (wifiStatus) {
        case WL_CONNECTED: {
            MyEvent.SetPulsePercent(relay ? 100 : 0);
            Serial.print("WIFI Connected, IP address: ");
            String aString = WiFi.localIP().toString();
            Serial.println(aString);
            strncpy( lastLocalIp, aString.c_str(), WDSTRSIZE);
            MyEvent.pushEvent(evCheckBox, 3);
          }
          break;
        case WL_DISCONNECTED:
          Serial.println("WIFI Deconnecte");
          MyEvent.SetPulsePercent(10);
          MyEvent.pushEvent(evCheckBox, 60);
          break;
        case WL_NO_SSID_AVAIL:
          Serial.println("WIFI SSID non trouve");
          MyEvent.SetPulsePercent(10);
          break;
        default:
          Serial.print("Wifi Status:");
          Serial.println(wifiStatus);
      }
      break;

    // eventuelles commande via le port serie
    case evInString:
      //      Serial.print("evInString '");
      //      Serial.print(MyEvent.inputString);
      //      Serial.println("'");

      if (MyEvent.inputString.equals("WIFIOFF")) {
        Serial.print("WIFI OFF");
        WiFi.disconnect();
        delay(100);
        WiFi.mode(WIFI_OFF);
        WiFi.forceSleepBegin();
      }

      if (MyEvent.inputString.equals("WIFION")) {
        Serial.println("Set WIFI ON");
        WiFi.begin();
      }


      if (MyEvent.inputString.equals("RESET")) {
        Serial.print("resetting");
        ESP.reset();
        Serial.println("--?-?--");
      }


      if (MyEvent.inputString.equals("WIFIRAZ")) {
        Serial.print("WIFIRAZ->");
        WiFiManager wifiManager;
        //reset saved settings
        wifiManager.resetSettings();
        Serial.println("Done");
      }



      if (MyEvent.inputString.equals("AP")) {
        Serial.println("ASK AP");
        Server.stop();
        ManageWifiSetup();
        Serial.println("---EOJ----");
      }

      if (MyEvent.inputString.equals("DIAG")) {

        Serial.println("---DIAG---");
        Serial.print("Local IP :");
        Serial.println(lastLocalIp);
        Serial.print("Http Get :");
        Serial.println(http_eedomus_get);


        //       WiFi.printDiag(Serial);
        Serial.println("---EOT----");
      }

      break;
  }
}


// divers
//void setWifiOn() {
//  Serial.println("Set WIFI ON");
//  WiFi.begin();
//}

// try to get box relay status

bool getBoxStatus(bool * boxstatus) {
  bool result = false;
  Client.begin(ClientWifi, WifiData.eedomus_ip, 80, http_eedomus_get);
  int httpCode = Client.GET();

  if (httpCode != 200) {
    //   Serial.print("Erreur accces box : ");
    //   Serial.println(httpCode);
  } else {
    JSONVar answer = JSON.parse(Client.getString());
    if ((int)answer["success"] == 1) {
      result = true;
      *boxstatus = ((int)answer["body"]["last_value"] == 1) || ((int)answer["body"]["last_value"] == 100);
      //   Serial.print("EEDOMUS relay  ");
      //   Serial.println(*boxstatus);


      //Serial.println(JSON.stringify(answer));
    }
  }
  Client.end();
  return (result);
}


// try to set box to relay state
bool setBoxStatus(const bool aStatus) {

  bool result = false;

  String aString = http_eedomus_set;
  if (aStatus) {
    aString += "1";
  } else {
    aString += "0";
  }

  Client.begin(ClientWifi, WifiData.eedomus_ip, 80, aString);
  int httpCode = Client.GET();

  if (httpCode == 200) {
    //aString = Client.getString();
    JSONVar answer = JSON.parse(Client.getString());
    //       Serial.println(aString);
    //  Serial.print("answer[\"success\"] = ");

    //   Serial.println((int) answer["success"]);
    if ((int)answer["success"] == 1) {
      //  Serial.print("Result=");

      //Serial.println((const char*) answer["body"]["result"]);
      //         Serial.println(JSON.stringify(answer));
      result = (strcmp("[OK]", answer["body"]["result"]) == 0);
    }
    //} else {
    //  Serial.println(httpCode);
  }
  Client.end();
  return (result);
}



// Gestion de changement d'etat du relai
// ajustement de la sortie relai sur l'etat de la variable relai
// ajustement de la led idem
void  UpdateRelay() {
  digitalWrite(RELAY_PIN, relay);
  MyEvent.SetPulsePercent(relay ? 100 : 0);  // pour avoir un visuel de l'etat
  Serial.print("GPIO Relay=");
  Serial.print(relay);
  Serial.println();
}


void buildHttpString() {
  String aString = "http://";
  aString += WifiData.eedomus_ip;
  aString += "/api/set?api_user=";
  aString += WifiData.api_user;
  aString += "&api_secret=";
  aString += WifiData.api_secret;
  aString += "&action=periph.value&periph_id=";
  aString += WifiData.periph_id;
  aString += "&value=";
  http_eedomus_set = aString;
  aString = "http://";
  aString += WifiData.eedomus_ip;
  aString += "/api/get?api_user=";
  aString += WifiData.api_user;
  aString += "&api_secret=";
  aString += WifiData.api_secret;
  aString += "&action=periph.value&periph_id=";
  aString += WifiData.periph_id;
  http_eedomus_get = aString;
}

// from https://lastminuteengineers.com/creating-esp8266-web-server-arduino-ide/
String SendHTML(bool relay) {
  String ptr = F("<!DOCTYPE html> <html>\n"
                 "<head><meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0, user-scalable=no\">\n"
                 "<meta http-equiv=\"refresh\" content=\"15\">"
                 "<title>SonOff Control</title>\n"
                 "<style>html { font-family: Helvetica; display: inline-block; margin: 0px auto; text-align: center;}\n"
                 "body{margin-top: 50px;} h1 {color: #444444;margin: 50px auto 30px;} h3 {color: #444444;margin-bottom: 50px;}\n"
                 ".button {display: block;width: 80px;background-color: #1abc9c;border: none;color: white;padding: 13px 30px;text-decoration: none;font-size: 25px;margin: 0px auto 35px;cursor: pointer;border-radius: 6px;}\n"
                 ".button-on {background-color: #1abc9c;}\n"
                 ".button-on:active {background-color: #16a085;}\n"
                 ".button-off {background-color: #34495e;}\n"
                 ".button-off:active {background-color: #2c3e50;}\n"
                 "p {font-size: 14px;color: #888;margin-bottom: 10px;}\n"
                 "</style>\n"
                 "</head>\n"
                 "<body>\n"
                 "<h2>Controle</h2>\n"
                 "<h1>");
  ptr += WifiData.hostname;
  ptr += F("</h1>\n");
  if (relay) {
    ptr += F("<p>press to turn OFF</p><a class=\"button button-on\" href=\"/sonoff/off\">ON</a>\n");
  } else {
    ptr += F("<p>press to turn ON</p><a class=\"button button-off\" href=\"/sonoff/on\">OFF</a>\n");
  }

  ptr += F("<p><a href=\"/\">Etat</a></p>"
           "<h4>" ESPEEDOMUS_VERSION "</h4>\n"
           "</body>\n"
           "</html>\n");
  return ptr;
}

String SendHTMLDisplay(bool relay) {
  String ptr = F("<!DOCTYPE html> <html>\n"
                 "<head><meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0, user-scalable=no\">\n"
                 "<meta http-equiv=\"refresh\" content=\"15\">"
                 "<title>SonOff Control</title>\n"
                 "<style>html { font-family: Helvetica; display: inline-block; margin: 0px auto; text-align: center;}\n"
                 "body{margin-top: 50px;} h1 {color: #444444;margin: 50px auto 30px;} h3 {color: #444444;margin-bottom: 50px;}\n"
                 ".button {display: block;width: 80px;background-color: #1abc9c;border: none;color: white;padding: 13px 30px;text-decoration: none;font-size: 25px;margin: 0px auto 35px;cursor: pointer;border-radius: 0px;}\n"
                 ".button-on {background-color: #1abc9c;}\n"
                 ".button-on:active {background-color: #16a085;}\n"
                 ".button-off {background-color: #34495e;}\n"
                 ".button-off:active {background-color: #2c3e50;}\n"
                 "p {font-size: 14px;color: #888;margin-bottom: 10px;}\n"
                 "</style>\n"
                 "</head>\n"
                 "<body>\n"
                 "<h2>Etat</h2>\n"
                 "<h1>");
  ptr += WifiData.hostname;
  ptr += F("</h1>\n");
  if (relay) {
    ptr += F("<a class=\"button button-on\" href=\"/\">ON</a>\n");
  } else {
    ptr += F("<a class=\"button button-off\" href=\"/\">OFF</a>\n");
  }

  ptr += F("<p><a href=\"/action\">Change</a></p>"
           "<h4>" ESPEEDOMUS_VERSION "</h4>\n"
           "</body>\n"
           "</html>\n");
  return ptr;
}

//gestion de la comande Setup
void  HTTPCallBack_setup() {
  Serial.println("WEB -> setup");
  bool aChange = false;
  String message = "";
  for (byte  N = 0; N < Server.args(); N++) {
    if (Server.argName(N) == "hostname" ) {
      strncpy(WifiData.hostname, Server.arg(N).c_str(), WDSTRSIZE);
      aChange = true;
      message += "<p>" + Server.argName(N) + ": " + Server.arg(N) + "</p>\n";
    }
    if (Server.argName(N) == "api_secret") {
      strncpy(WifiData.api_secret, Server.arg(N).c_str(), WDSTRSIZE);
      message += "<p>" + Server.argName(N) + ": " + Server.arg(N) + "</p>\n";
      aChange = true;
    }
    if (Server.argName(N) == "api_user") {
      strncpy(WifiData.api_user, Server.arg(N).c_str(), WDSTRSIZE);
      message += "<p>" + Server.argName(N) + ": " + Server.arg(N) + "</p>\n";
      aChange = true;
    }
    if (Server.argName(N) == "periph_id") {
      strncpy(WifiData.periph_id, Server.arg(N).c_str(), WDSTRSIZE);
      message += "<p>" + Server.argName(N) + ": " + Server.arg(N) + "</p>\n";
      aChange = true;
    }
  }
  if (aChange) {
    strncpy(WifiData.eedomus_ip, Server.client().remoteIP().toString().c_str(),  WDSTRSIZE);
    EEPROM.put(0, WifiData);
    EEPROM.commit();
    buildHttpString();
    Serial.println("WifiData Saved");
    shouldSaveConfig = false;

  }
  message += "<p>" ESPEEDOMUS_VERSION "</p>\n";
  Server.send(200, "text/html", message);
}



//xecutable segment sizes:
//IROM   : 301856          - code in flash         (default or ICACHE_FLASH_ATTR)
//IRAM   : 27428   / 32768 - code in IRAM          (ICACHE_RAM_ATTR, ISRs...)
//DATA   : 1268  )         - initialized variables (global, static) in RAM/HEAP
//RODATA : 3704  ) / 81920 - constants             (global, static) in RAM/HEAP
//BSS    : 30280 )         - zeroed variables      (global, static) in RAM/HEAP
//Le croquis utilise 334256 octets (34%) de l'espace de stockage de programmes. Le maximum est de 958448 octets.
//Les variables globales utilisent 35252 octets (43%) de mémoire dynamique, ce qui laisse 46668 octets pour les variables locales. Le maximum est de 81920 octets.



void  HTTPCallBack_display() {
  Serial.print("WEB : Display from ");
  Serial.print(Server.client().remoteIP());
  Serial.println();
  Server.send(200, "text/html", SendHTMLDisplay(relay));
}


void  HTTPCallBack_action() {
  Serial.print("WEB : Action from ");
  Serial.print(Server.client().remoteIP());
  Serial.println();
  Server.send(200, "text/html", SendHTML(relay));
}

void HTTPCallBack_relayOn() {
  Serial.println("WEB : relay ON");
  relay = true;
  UpdateRelay();
  boxNeedUpdate = true;                   // signale le report vers la box
  Server.sendHeader("Location", "/action");
  Server.send ( 302);
  MyEvent.pushEventMillisec(evCheckBox, 100);       // report vers la box
}

void HTTPCallBack_relayOff() {
  Serial.println("WEB : relay OFF");
  relay = false;
  UpdateRelay();
  boxNeedUpdate = true;                   // signale le report vers la box
  Server.sendHeader("Location", "/action");
  Server.send ( 302);
  MyEvent.pushEventMillisec(evCheckBox, 100);       // report vers la box
}




// Call back des requetes HTTP
// gestion d'une commande SONOFF ON envoyée par l'eedomus
void  HTTPCallBack_eedomusOn() {
  Serial.println("EEDOMUS -> relay on");
  if (WifiData.eedomus_ip[0] == 0) {
    strncpy(WifiData.eedomus_ip, Server.client().remoteIP().toString().c_str(),  WDSTRSIZE);
  }
  Server.send(200, "text/html", "ON");
  relay = true;
  UpdateRelay();
  MyEvent.pushEvent(evCheckBox, 60);
}





// gestion d'une commande SONOFF OFF envoyée par l'eedomus
void HTTPCallBack_eedomusOff() {
  Serial.println("EEDOMUS -> relay off");
  if (WifiData.eedomus_ip[0] == 0) {
    strncpy(WifiData.eedomus_ip, Server.client().remoteIP().toString().c_str(),  WDSTRSIZE);
  }
  Server.send(200, "text/html", "OFF");
  relay = false;
  UpdateRelay();
  MyEvent.pushEvent(evCheckBox, 60);
}

// en cas de commande invalde on reponds
void HTTPCallBack_NotFound() {
  Serial.println("error 404");
  String message = "File Not Found\n\n";
  message += "URI: ";
  message += Server.uri();
  message += "\nMethod: ";
  message += (Server.method() == HTTP_GET) ? "GET" : "POST";
  message += "\nArguments: ";
  message += Server.args();
  message += "\n<br>";
  for (uint8_t i = 0; i < Server.args(); i++) {
    message += " " + Server.argName(i) + ": " + Server.arg(i) + "\n";
  }
  message += "<H2><a href=\"/\">go home</a></H2><br>";
  Server.send(404, "text/html", message);

}
