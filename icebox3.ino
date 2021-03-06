#define VER "ver 3.1"
#define INTVER 31000

#include <ESP8266WebServer.h>
#include <WiFiManager.h>
#include <ArduinoJson.h>
#include <LittleFS.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <EEPROM.h>
#include <AceButton.h>
using namespace ace_button;

#include "Ref.h"
#include "Display.h"
#include "PotentLevel.h"

//пины реле компрессора холодильной камеры
#define RELAY1 D5 //рабочая обмотка
#define RELAY2 D6 //пусковая обмотка
//пины реле компрессора морозильной камеры
#define RELAY3 D7 //рабочая обмотка
#define RELAY4 D4 //пусковая обмотка

//пин реле отключения лампы
#define RELAYLAMP D0

//пин кнопки смены режима дисплея
//#define BUTTON_PIN 1 //TX
#define BUTTON_PIN 10

//подключение термометров
#define ONE_WIRE_BUS D3

AceButton button(BUTTON_PIN);
OneWire oneWire(ONE_WIRE_BUS);
DallasTemperature sensors(&oneWire);

ESP8266WebServer server(80);

//камеры и дисплей
Ref ref1(RELAY1, RELAY2);
Frz ref2(RELAY3, RELAY4);
Display dspl(&ref1, &ref2);
PotentLevel pl0;
PotentLevel pl1;

unsigned long lastUpdate = 0;
unsigned long updateInterval = 20000;
String lastTime;

class Parameters {
  public:
    //уровни камер
    byte lvl1 = 3;
    byte lvl2 = 3;
    //индексы термометров холодильной камеры
    byte trm1 = 0;
    byte trm2 = 1;
    //индексы термометров морозильной камеры
    byte trm3 = 2;
    byte trm4 = 3;
    //уровень детализации информации на дисплее
    byte dsplDetail = 2;
    
    void load() {
      EEPROM.begin(8);
      byte l1 = EEPROM.read(0);
      byte l2 = EEPROM.read(1);
      byte t1 = EEPROM.read(2);
      byte t2 = EEPROM.read(3);
      byte t3 = EEPROM.read(4);
      byte t4 = EEPROM.read(5);
      byte dd = EEPROM.read(6);
      byte chksum = EEPROM.read(7);
      if (chksum == checksum(l1, l2, t1, t2, t3, t4, dd)) {
        lvl1 = l1;
        lvl2 = l2;
        trm1 = t1;
        trm2 = t2;
        trm3 = t3;
        trm4 = t4;
        dsplDetail = dd;
      }
    }
    void save() {
      EEPROM.begin(8);
      EEPROM.write(0, lvl1);
      EEPROM.write(1, lvl2);
      EEPROM.write(2, trm1);
      EEPROM.write(3, trm2);
      EEPROM.write(4, trm3);
      EEPROM.write(5, trm4);
      EEPROM.write(6, dsplDetail);
      EEPROM.write(7, checksum());
      EEPROM.commit();
    }
  private:
    byte checksum(byte l1, byte l2, byte t1, byte t2, byte t3, byte t4, byte dd) {
      return (123 + l1 + l2 + t1 + t2 + t3 + t4 + dd) % 255;
    }
    byte checksum() {
      return checksum(lvl1, lvl2, trm1, trm2, trm3, trm4, dsplDetail);
    }
} params;

void onLvlChange1(Ref* ref) {
  dspl.lvlPageBegin(1, ref->getLvl(), ref->lvlChar());
}

void onLvlChange2(Ref* ref) {
  dspl.lvlPageBegin(2, ref->getLvl(), ref->lvlChar());
}

void onDetailChange(Display* dsp) {
  params.dsplDetail = dsp->getDetail();
  params.save();
}

void onPotentLevelChange1(PotentLevel* ppl) {
  ref1.lvlSet(ppl->getLevel());
}

void onPotentLevelChange2(PotentLevel* ppl) {
  ref2.lvlSet(ppl->getLevel());
}

void buttonHandleEvent(AceButton* /*button*/, uint8_t eventType, uint8_t /*buttonState*/) {
  switch (eventType) {
    case AceButton::kEventPressed:
      dspl.detailUp();
      break;
    case AceButton::kEventLongPressed:
      ESP.reset();
      break;
  }
}

bool shouldSaveConfig = false;
void saveConfigCallback () {
  shouldSaveConfig = true;
}

bool connectWiFi(bool startConfigPortal = false) {

  WiFiManagerParameter custom_t1idx("t1idx", "index t1", String(params.trm1).c_str(), 3);
  WiFiManagerParameter custom_t2idx("t2idx", "index t2", String(params.trm2).c_str(), 3);
  WiFiManagerParameter custom_t3idx("t3idx", "index t3", String(params.trm3).c_str(), 3);
  WiFiManagerParameter custom_t4idx("t4idx", "index t4", String(params.trm4).c_str(), 3);
  
  WiFiManager wifiManager;
  wifiManager.setSaveConfigCallback(saveConfigCallback);
  wifiManager.addParameter(&custom_t1idx);
  wifiManager.addParameter(&custom_t2idx);
  wifiManager.addParameter(&custom_t3idx);
  wifiManager.addParameter(&custom_t4idx);
  
  //wifiManager.resetSettings();
  wifiManager.setConfigPortalTimeout(30);

  if (startConfigPortal) {
    Serial.println("startConfigPortal");
    if(!wifiManager.startConfigPortal())
      return false;
  }
  else {
    Serial.println("autoConnect");
    if(!wifiManager.autoConnect())
      return false;
  }

  params.trm1 = String(custom_t1idx.getValue()).toInt();
  params.trm2 = String(custom_t2idx.getValue()).toInt();
  params.trm3 = String(custom_t3idx.getValue()).toInt();
  params.trm4 = String(custom_t4idx.getValue()).toInt();
  
  if (shouldSaveConfig) {
    params.save();
  }  
  return true;
}

String IpAddress2String(const IPAddress& ipAddress) {
  return String(ipAddress[0]) + String(".") +\
  String(ipAddress[1]) + String(".") +\
  String(ipAddress[2]) + String(".") +\
  String(ipAddress[3])  ;
}

void setup() {
  Serial.begin(115200);
  Serial.println();

  sensors.begin();
  DeviceAddress deviceAddress;
  while (oneWire.search(deviceAddress)) {
    if (sensors.validAddress(deviceAddress) && sensors.validFamily(deviceAddress)) {
      sensors.setResolution(deviceAddress, 12);
    }
  }

  params.load();
  
  pl0.begin(0, onPotentLevelChange1);
  pl1.begin(1, onPotentLevelChange2);

  ref1.connectLamp(RELAYLAMP);
  ref1.begin(pl0.getCurrentLevel(), onLvlChange1);
  ref2.begin(pl1.getCurrentLevel(), onLvlChange2);

  dspl.begin(params.dsplDetail, onDetailChange);
  dspl.page0();

  pinMode(BUTTON_PIN, INPUT_PULLUP);
  button.setEventHandler(buttonHandleEvent);
  
  Serial.println("wi-fi connecting...");
  if(connectWiFi()) {
    String info = IpAddress2String(WiFi.localIP()) + ":80";
    dspl.page1(info);

    unsigned long time = millis();
    
    httpServerConfig();

    if (time + 5000 > millis())
      delay(time + 5000 - millis());
      
  } else {
    String info = "control: " + IpAddress2String(WiFi.localIP());
    dspl.page1(info);
    delay(5000);
  }
  dspl.update(true);
}

void loop() {
  ref1.loop();
  ref2.loop();

  pl0.loop();
  pl1.loop();

  button.check();
  
  if ((millis() - lastUpdate >= updateInterval) || lastUpdate == 0) {
    lastUpdate = millis();
    sensors.requestTemperatures();
    float t1 = sensors.getTempCByIndex(params.trm1);
    float t2 = sensors.getTempCByIndex(params.trm2);
    float t3 = sensors.getTempCByIndex(params.trm3);
    float t4 = sensors.getTempCByIndex(params.trm4);
    ref1.controlLoop(t1, t2);
    ref2.controlLoop(t3, t4);
  }
  dspl.loop();
  server.handleClient();
}

void httpServerConfig() {
  
  server.on("/", []() {
    server.send(200, "text/html", webPage());
  });
  
  server.on("/R1Up", [](){
    ref1.lvlUp();
    server.sendHeader("Location", "/");
    server.send(302);
  });
  server.on("/R1Dn", [](){
    ref1.lvlDn();
    server.sendHeader("Location", "/");
    server.send(302);
  });
  server.on("/R1Sw", [](){
    dspl.detailUp();
    server.sendHeader("Location", "/");
    server.send(302);
  });
  
  server.on("/R2Up", [](){
    ref2.lvlUp();
    server.sendHeader("Location", "/");
    server.send(302);
  });
  server.on("/R2Dn", [](){
    ref2.lvlDn();
    server.sendHeader("Location", "/");
    server.send(302);
  });
  server.on("/R2Sw", [](){
    //резерв
    server.sendHeader("Location", "/");
    server.send(302);
  });
  
  server.on("/Reset", [](){
    server.sendHeader("Location", "/");
    server.send(302);
    ESP.reset();
    delay(5000);
  });
  server.on("/StartConfigPortal", [](){
    server.sendHeader("Location", "/");
    server.send(302);
    connectWiFi(true);
  });
  
  server.begin();
}

String webPage() {
  String web;
  web += "<head><meta name=\"viewport\" content=\"width=device-width, initial-scale=1\"/> <meta charset=\"utf-8\"><title>Refrigerator</title><style>button{color:red;padding: 10px 27px;}</style></head>";
  web += "<h1 style=\"text-align: center;font-family: Open sans;font-weight: 100;font-size: 20px;\">Web Server Refrigerator</h1><div>";
  
  //++++++++++ R-1 +++++++++++++
  web += "<p style=\"text-align: center;margin-top: 0px;margin-bottom: 5px;\">----R1----</p>";
  if (ref1.compressorStarted())
  {
    web += "<div style=\"text-align: center;width: 98px;color:white ;padding: 10px 30px;background-color: #43a209;margin: 0 auto;\">ON</div>";
  }
  else
  {
    web += "<div style=\"text-align: center;width: 98px;color:white ;padding: 10px 30px;background-color: #ec1212;margin: 0 auto;\">OFF</div>";
  }
  web += "<div style=\"text-align: center;width: 98px;color:white ;padding: 10px 30px;background-color: #5191e4;margin: 0 auto;\">"+ String(ref1.lvlChar()) +" / "+ String(ref1.getTemperatureFreezer()) +"</div>";
  web += "<div style=\"text-align: center;margin: 5px 0px;\"> <a href=\"R1Dn\"><button>Lvl -</button></a>&nbsp;<a href=\"R1Up\"><button>Lvl +</button></a></div>";
  web += "<div style=\"text-align: center;margin: 5px 0px;\"> <a href=\"R1Sw\"><button></button></a></div>";
  //++++++++++ R-1 +++++++++++++

  //++++++++++ R-2 +++++++++++++
  web += "<p style=\"text-align: center;margin-top: 0px;margin-bottom: 5px;\">----R2----</p>";
  if (ref2.compressorStarted())
  {
    web += "<div style=\"text-align: center;width: 98px;color:white ;padding: 10px 30px;background-color: #43a209;margin: 0 auto;\">ON</div>";
  }
  else
  {
    web += "<div style=\"text-align: center;width: 98px;color:white ;padding: 10px 30px;background-color: #ec1212;margin: 0 auto;\">OFF</div>";
  }
  web += "<div style=\"text-align: center;width: 98px;color:white ;padding: 10px 30px;background-color: #5191e4;margin: 0 auto;\">"+ String(ref2.lvlChar()) +" / "+ String(ref2.getTemperatureFreezer()) +"</div>";
  web += "<div style=\"text-align: center;margin: 5px 0px;\"> <a href=\"R2Dn\"><button>Lvl -</button></a>&nbsp;<a href=\"R2Up\"><button>Lvl +</button></a></div>";
  web += "<div style=\"text-align: center;margin: 5px 0px;\"> <a href=\"R2Sw\"><button></button></a></div>";
  //++++++++++ R-2 +++++++++++++
 
  // ========REFRESH=============
  web += "<div style=\"text-align:center;margin-top: 20px;\"><a href=\"/\"><button style=\"width:158px;\">REFRESH</button></a></div>";
  // ========REFRESH=============
 
  // ========RESET=============
  web += "<div style=\"text-align:center;margin-top: 20px;\"><a href=\"Reset\"><button>RESET ESP</button></a></div>";
  // ========RESET=============
 
  // ========StartConfigPortal=============
  web += "<div style=\"text-align:center;margin-top: 20px;\"><a href=\"StartConfigPortal\"><button>Config Portal</button></a></div>";
  // ========StartConfigPortal=============
 
  web += "</div>";
  return(web);
}
