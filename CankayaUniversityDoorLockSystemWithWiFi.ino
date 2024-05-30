/*
  This code sequence is written to enhance Çankaya University Technology Transfer Office Incubation Center's old door lock system.
  This is the next generation of the DoorLockSystemWithRFID, which has been enhanced with WiFi support to perform more advanced tasks.
  Previous system: https://github.com/barkinsarikartal/DoorLockSystemWithRFID

  This code executes the following statements:
  1) Get valid RFID cards from API,
  2) Read RFID cards by RC522,
  3) Trigger an electronic door lock if card ID is valid,
  4) Post data of the read RFID card IDs and the times they were read by the RC522.

  This code accounts for 1002625 bytes (76%) of program storage space, 48008 bytes (14%) of dynamic memory.

  Contributors:
    Arda YILDIZ           ardayildiz029@gmail.com
    Barkın SARIKARTAL     sarikartalbarkin@gmail.com
  Supporters:
    Abdul Kadir GÖRÜR     agorur@cankaya.edu.tr
    Burçin TUNA           btuna@cankaya.edu.tr
    H. Hakan MARAŞ        hhmaras@cankaya.edu.tr
*/

#include <WiFi.h>                    //This library is automatically installed when ESP32 add-on is installed in the Arduino IDE.
#include <HTTPClient.h>              //This library is automatically installed when ESP32 add-on is installed in the Arduino IDE.
#include <SPIFFS.h>                  //This library is automatically installed when ESP32 add-on is installed in the Arduino IDE.
#include <time.h>                    //This library is automatically installed when ESP32 add-on is installed in the Arduino IDE.
#include <Wire.h>                    //This library is automatically installed when ESP32 add-on is installed in the Arduino IDE.
#include <SPI.h>                     //This library is automatically installed when ESP32 add-on is installed in the Arduino IDE.
#include <WiFiUdp.h>                 //This library is automatically installed when ESP32 add-on is installed in the Arduino IDE.
#include <vector>                    //Comes with Arduino IDE (std::vector)
#include <ArduinoJson.h>             //Using version: 7.0.4
#include <RTClib.h>                  //Using version: 2.1.4
#include <NTPClient.h>               //Using version: 3.2.1
#include <MFRC522.h>                 //Using version: 1.4.10
#include <ESP32Ping.h>

#define BUZZER_PIN 2    //Buzzer is connected to ESP32's D2 pin.
#define RELAY_PIN 4     //Relay is connected to ESP32's D4 pin.
#define SS_PIN 5        //RC522's SDA pin is connected to ESP32's D5 pin.
#define RST_PIN 26      //RC522's RST pin is connected to ESP32's D26 pin.

RTC_DS3231 RTCModule;
MFRC522 reader(SS_PIN, RST_PIN);

const char* ssid = "ssid";                                //Write your ssid name here.
const char* hashFileName = "/hash.txt";
const char* cardsFileName = "/cards.txt";
const char* notSentCardsFileName = "/notSentCards.txt";
const char* ntpServer = "ntp-server-address";             //Write NTP server address here.
const char* bearerToken = "bearer-token";                 //Write your bearer token for REST API here.
const char* getAddress = "http-get-address";              //Write your REST API address for HTTP get here.
const char* postAddress = "http-post-address";            //Write your REST API address for HTTP post here.

IPAddress pingIp(0, 0, 0, 0);       //Write your gateway IP here.
IPAddress local_IP(0, 0, 0, 0);     //Write your local IP here.
IPAddress gateway(0, 0, 0, 0);      //Write your gateway IP here.
IPAddress subnet(0, 0, 0, 0);       //Write your subnet mask here.
IPAddress primaryDNS(0, 0, 0, 0);   //Write your primary DNS here.

const long utcOffsetInSeconds = 10800;    //+3 Hours because Türkiye's time zone is GMT +3.

WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, ntpServer, utcOffsetInSeconds);

String getHash = "";
String payload = "";
String imei = "imei";       //Write your module's location. For example: library.
String notSentCardIDs = "";

bool connectedToWiFi = false;
bool thereAreNotSentCards = false;

unsigned long rc522ResetTimer = 300000;   //5 minutes timer for resetting RC522.
unsigned long espResetTimer = 3600000;    //1 hour timer for resetting ESP32.

std::vector<String> cardsVector;
std::vector<String> notSentCardsVector;

void setup(){
  Serial.begin(115200);
  pinMode(BUZZER_PIN, OUTPUT);
  pinMode(RELAY_PIN, OUTPUT);
  digitalWrite(RELAY_PIN, HIGH);
  while(!SPIFFS.begin(true)){
    Serial.println("SPIFFS Başlatılamadı.");
  }
  SPI.begin();
  Wire.begin();
  RTCModule.begin();
  reader.PCD_Init();
  connectedToWiFi = ConnectSSID();    //Trying to connect to WiFi.
  if(connectedToWiFi){
    GetFunction();          //Checking if there are new cards at API.
    PostFunction();         //Checking if there are cards that couldn't been send to the API.
    timeClient.begin();     //Starting time client to get unix time from ntp server.
    updateRTCFromNTP();     //Syncing DS3231's time with unix time.
  }
  else{
    LoadCardsFromMemory();  //Continue with cards in memory.
  }
}

void loop(){
  if(reader.PICC_IsNewCardPresent() && reader.PICC_ReadCardSerial()){  //Checking if a new card is present and if card serial can be read.
    String readCardID = ReadCardID();   //Reading cards's UID.
    if(readCardID.length() == 8){
      if(IsCardIDValid(readCardID)){      //Checking if card ID is valid or not.
        ValidCard();
        DateTime now = RTCModule.now();
        uint32_t unixTimeStamp = now.unixtime();
        if(connectedToWiFi){
          PostReadCard(readCardID, String(unixTimeStamp), "Valid");
        }
        if(!connectedToWiFi){
          AddNotSentCardToMemory(readCardID, String(unixTimeStamp), "Valid");
        }
      }
      else {
        DeclinedCard();
        DateTime now = RTCModule.now();
        uint32_t unixTimeStamp = now.unixtime();
        if(connectedToWiFi){
          PostReadCard(readCardID, String(unixTimeStamp), "Declined");
        }
        if(!connectedToWiFi){
          AddNotSentCardToMemory(readCardID, String(unixTimeStamp), "Declined");
        }
      }
    }
    
    reader.PICC_HaltA();       //Halts the RC522's operation
    reader.PCD_StopCrypto1();  //Stops cryptographic functions to secure communication
  }
  if(millis() > espResetTimer){  //Restarts the ESP32 in every 1 hour.
    ESP.restart();
  }
  if(millis() > rc522ResetTimer){  //Resets the RC522 module in every 5 minutes to prevent it from falling asleep.
    ResetRC522();
    rc522ResetTimer += 300000;
  }
  delay(250);
}

bool ConnectSSID(){    //Function to connect to WiFi
  unsigned long wifiStartTime = millis();
  if(!WiFi.config(local_IP, gateway, subnet, primaryDNS)){
    return false;
  }
  WiFi.setTxPower(WIFI_POWER_19_5dBm);    //Maximizing ESP32's tranmission power to catch better signals.
  WiFi.begin(ssid);
  while(millis() - wifiStartTime < 10000) {
    delay(1000);
    if(WiFi.status() == WL_CONNECTED) {
      if(Ping.ping(pingIp)) {
        return true;
      }
      else {
        return false;
      }
    }
  }
  return false;
}

void GetFunction(){    //Function to check if there are changes at API.
  HTTPClient httpGet;
  httpGet.begin(getAddress);
  httpGet.addHeader("Authorization", "Bearer " + String(bearerToken));
  int httpGetResponseCode = httpGet.GET();
  if(httpGetResponseCode >= 200 && httpGetResponseCode <= 299){
    payload = httpGet.getString();
    DynamicJsonDocument jsonBuffer(1024);
    DeserializationError error = deserializeJson(jsonBuffer, payload);
    if(error){
      return;
    }
    getHash = jsonBuffer["Hash"].as<String>();
    String memoryHash = GetLastHash();
    if(memoryHash == getHash){
      LoadCardsFromMemory();
    }
    else{
      JsonArray cardList = jsonBuffer["CardList"].as<JsonArray>();
      for(JsonVariant card : cardList){
        String newCardID = card["CardId"].as<String>(); 
        newCardID.trim();
        newCardID.toUpperCase();
        cardsVector.push_back(newCardID);
      }
      NewCards();
    }
    httpGet.end();
    payload.remove(0, payload.length());
  }
  else{
    LoadCardsFromMemory();
  }
}

String GetLastHash(){    //Function to get hash code in memory.
  bool openedHashFile = false;
  File hashFile = SPIFFS.open(hashFileName, "r"); 
  if(hashFile){
    openedHashFile = true;
  }
  while(hashFile.available() && openedHashFile){
    String lastHash = hashFile.readString();
    hashFile.close();
    return lastHash;
  }
}

void NewCards(){    //Function to call ChangeHash() function, sort new cards' vector and call WriteCards() function.
  ChangeHash();
  std::sort(cardsVector.begin(), cardsVector.end());
  WriteCards();
}

void ChangeHash(){    //Function to replace the hash code in memory with the new hash code on the server.
  bool openedHashFile = false;
  File hashFile = SPIFFS.open(hashFileName, FILE_WRITE); 
  if(hashFile){
    openedHashFile = true;
  }
  while(hashFile.available() && openedHashFile){
    hashFile.print(getHash);
    hashFile.close();
  }
}

void WriteCards(){    //Function to save new cards in the server to memory.
  File cardsFile = SPIFFS.open(cardsFileName, FILE_WRITE);
  if(cardsFile){
    for(const auto& data : cardsVector){
      cardsFile.print(data);
    }
    cardsFile.close();
  }
}

void PostFunction(){    //Function to post cards in memory that haven't been sent before.
  bool openedNotSentCardsFile = false;
  File notSentCardsFile = SPIFFS.open(notSentCardsFileName, FILE_READ); 
  if(notSentCardsFile){
    openedNotSentCardsFile = true;
  }
  while(notSentCardsFile.available() && openedNotSentCardsFile){
    if(notSentCardsFile.size() != 0){
      thereAreNotSentCards = true;
      notSentCardIDs = notSentCardsFile.readString();
    }
  }
  notSentCardsFile.close();
  openedNotSentCardsFile = false;
  if(thereAreNotSentCards){
    String tempStr = notSentCardIDs;
    HTTPClient httpPost;
    httpPost.begin(postAddress);
    httpPost.addHeader("Authorization", "Bearer " + String(bearerToken));
    httpPost.addHeader("Content-Type", "application/json");
    for(int i = 0; i < tempStr.length(); i += 19){
      String cardID = tempStr.substring(i, i + 8);
      String readTime = tempStr.substring(i + 8, i + 18);
      String Auth = "";
      if(tempStr.substring(i + 18, i + 19) == "1"){
        Auth = "Valid";
      }
      else if(tempStr.substring(i + 18, i + 19) == "0"){
        Auth = "Declined";
      }
      StaticJsonDocument<200> doc;
      doc["CardId"] = cardID;
      doc["imei"] = imei;
      doc["ReadDate"] = readTime;
      doc["Auth"] = Auth;
      String postData;
      serializeJson(doc, postData);
      int httpPostResponseCode = httpPost.POST(postData);
      if(httpPostResponseCode >= 200 && httpPostResponseCode <= 299){
        String deleteStr = "";
        if(tempStr.substring(i + 18, i + 19) == "1"){
          deleteStr = cardID + readTime + "1";
        }
        else if(tempStr.substring(i + 18, i + 19) == "0"){
          deleteStr = cardID + readTime + "0";
        }
        notSentCardIDs.replace(deleteStr, "");
      }
    }
    httpPost.end();
    File notSentCardsFile = SPIFFS.open(notSentCardsFileName, FILE_WRITE); 
    if(notSentCardsFile){
      openedNotSentCardsFile = true;
    }
    while(notSentCardsFile.available() && openedNotSentCardsFile){
      notSentCardsFile.print(notSentCardIDs);
      openedNotSentCardsFile = false;
    }
    notSentCardsFile.close();
  }
  else{
    return;
  }
}

void updateRTCFromNTP(){    //Function to update DS3231's time with the unix time coming from the NTP server.
  DateTime rtcnow = RTCModule.now();
  timeClient.update();
  unsigned long ntpEpochTime = timeClient.getEpochTime();
  if(String(rtcnow.unixtime()).length() != 10 && String(ntpEpochTime).length() == 10){
    if(rtcnow.unixtime() != ntpEpochTime){
      DateTime ntpTime = DateTime(ntpEpochTime);
      RTCModule.adjust(ntpTime);
    }
  }
  timeClient.end();
}

void LoadCardsFromMemory(){    //Function to load cards in memory into vector for faster control.
  bool openedCardsFile = false;
  File cardsFile = SPIFFS.open(cardsFileName, "r"); 
  if(cardsFile){
    openedCardsFile = true;
  }
  while(cardsFile.available() && openedCardsFile){
    String cardsInMemory = cardsFile.readString();
    for(int i = 0; i <= cardsInMemory.length(); i+=8){
      cardsVector.push_back(cardsInMemory.substring(i, i + 8));
    }
  }
  cardsFile.close();
}

String ReadCardID(){    //Function to return card ID that have been read by the RC522.
  String readCardID = "";
  for(byte i = 0; i < reader.uid.size; i++){
    if(reader.uid.uidByte[i] < 0x10){
      readCardID += 0;
    }
    readCardID += String(reader.uid.uidByte[i], HEX);
  }
  readCardID.toUpperCase();
  return readCardID;
}

bool IsCardIDValid(const String& key){    //Function to check if card ID is valid or not with binary search.
  int l = 0;
  int r = cardsVector.size() - 1;
  while(l <= r){
    int m = l + (r - l) / 2;
    int res;
    if(key == cardsVector[m]){
      res = 0;
    }
    else if(key > cardsVector[m]){
      res = 1;
    }
    else{
      res = -1;
    }
    if(res == 0){
      return true;
    }
    if(res > 0){
      l = m + 1;
    }
    else{
      r = m - 1;
    }
  }
  return false;
}

void ValidCard(){    //Function to trigger the relay if card ID is valid.
  digitalWrite(BUZZER_PIN, HIGH);
  delay(50);
  digitalWrite(BUZZER_PIN, LOW);
  delay(20);
  digitalWrite(BUZZER_PIN, HIGH);
  delay(50);
  digitalWrite(BUZZER_PIN, LOW);
  digitalWrite(RELAY_PIN, LOW);
  delay(2000);
  digitalWrite(RELAY_PIN, HIGH);
  delay(50);
}

void DeclinedCard(){    //Function to trigger the buzzer with a warning sound if card ID is not valid.
  digitalWrite(BUZZER_PIN, HIGH);
  delay(600);
  digitalWrite(BUZZER_PIN, LOW);
  delay(100);
  digitalWrite(BUZZER_PIN, HIGH);
  delay(600);
  digitalWrite(BUZZER_PIN, LOW);
  delay(100);
  digitalWrite(BUZZER_PIN, HIGH);
  delay(600);
  digitalWrite(BUZZER_PIN, LOW);
}

void PostReadCard(String readCardID, String readTimeUnix, String Auth){    //Function to post cards that have been read at loop() function.
  HTTPClient httpPost;
  httpPost.begin(postAddress);
  httpPost.addHeader("Authorization", "Bearer " + String(bearerToken));
  httpPost.addHeader("Content-Type", "application/json");
  StaticJsonDocument<200> doc;
  doc["CardId"] = readCardID;
  doc["imei"] = imei;
  doc["ReadDate"] = readTimeUnix;
  doc["Auth"] = Auth;
  String postData;
  serializeJson(doc, postData);
  int httpPostResponseCode = httpPost.POST(postData);
  if(httpPostResponseCode >= 200 && httpPostResponseCode <= 299){
    httpPost.end();
  }
  else{
    AddNotSentCardToMemory(readCardID, readTimeUnix, Auth);
    httpPost.end();
  }
}

void AddNotSentCardToMemory(String readCardID, String readTimeUnix, String Auth){    //Function to add cards to memory that have been read at loop() function but couldn't be sent to the server.
  bool openedNotSentCardsFile = false;
  File notSentCardsFile = SPIFFS.open(notSentCardsFileName, "r+");
  if(!notSentCardsFile){
    return;
  }
  notSentCardsFile.seek(notSentCardsFile.size());
  if(Auth == "Valid"){
    notSentCardsFile.print(readCardID + readTimeUnix + "1");
  }
  else if(Auth == "Declined"){
    notSentCardsFile.print(readCardID + readTimeUnix + "0");
  }
  notSentCardsFile.close();
  File file = SPIFFS.open(notSentCardsFileName, "r");
  if(!file){
    return;
  }
  file.close();
}

void ResetRC522(){    //Function to reset RC522.
  reader.PCD_Reset();
  delay(300);
  reader.PCD_Init();
  delay(300);
}