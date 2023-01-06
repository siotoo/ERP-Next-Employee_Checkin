/*BRIEF 
This code enables the ESP32 to Read an RFID Tag and Request thought Frappe REST API to check in/out the Correspondent Employee. 
Code is Divided in Main + 3 functions (Button, RFID, Array_to_String). 

Button - Reads 2 Buttons Connected to GPIO 21 & 22 representing the necessity of the operator to Check IN or Check OUT. 

RFID - Reads RFID TAG, wait 5 seconds for a button to be pressed, and returns thought the STRUCT **RFID_return** employee ID and Check status (IN/OUT) 
Note : After 5 seconds if no button is pressed, it returns a "Time-Out" as Status, which will be used later to show user last check status (In/Out + Timestamp).
 
MAIN - Connects to Wi-Fi, wait for an RFID Device, Check Status and then update it thought a POST into the Frappe API.


                ESP32 DEVKIT V3
               _______________
          Clk |    | USB  |   | 5V
           D0 |    | port |   | CMD
           D1 |    |______|   | D3
           15 |               | D2   
BTN ZEIT    2 |               | 13
            0 |               | GND   RC522 GND
BTN OUT     4 |               | 12
           16 |               | 14     
           17 |               | 27    RC522 RST
RC522 SDA   5 |               | 26
RC522 SCK  18 |   _________   | 25    BTN IN 
RC522 MISO 19 |  |  ESP32  |  | 33
          GND |  | DEVKIT  |  | 32
SDADISPLAY 21 |  |   V3    |  | 35
           RX |  |         |  | 34
           TX |  |         |  | VN
SCLDISPLAY 22 |  |_________|  | VP
RC522 MOSI 23 |               | EN
          GND |_______________| 3V3   RC522 VCC
*/

#include <WiFi.h>
#include <NTPClient.h>
#include <WiFiUdp.h>
#include <HTTPClient.h>
#include <SPI.h>
#include <MFRC522.h>
#include <ArduinoJson.h>
#include "EEPROM.h"
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <HTTPUpdate.h>
#include <WiFiClientSecure.h>
#include "cert.h"

#define SS_PIN  5  // ESP32 pin GIOP5 
#define RST_PIN 2 // ESP32 pin GIOP27 
#define BUTTON_IN_PIN       25   // GIOP21 pin connected to button IN
#define BUTTON_OUT_PIN      4   // GIOP22 pin connected to button OUT
#define BUTTON_ZEIT_PIN      2   // GPIO2  pin connected to button ZEIT (Show Working Hours (Month/Week)
#define LONG_PRESS_TIME  300// 1000 milliseconds
#define LENGTH(x) (strlen(x) + 1)   // length of char string
#define EEPROM_SIZE 200             // EEPROM size
#define WiFi_rst 0                  //WiFi credential reset pin (Boot button on ESP32)


String ssid;                        //string variable to store ssid
String pss;                         //string variable to store password
unsigned long rst_millis;
int lastState_IN = LOW;  // the previous state from the input pin
int currentState_IN;     // the current reading from the input pin
unsigned long pressedTime_IN  = 0;

bool isPressing_IN = false;
bool isLongDetected_IN = false;

int lastState_OUT = LOW;  // the previous state from the input pin
int currentState_OUT;     // the current reading from the input pin
unsigned long pressedTime_OUT  = 0;

bool isPressing_OUT = false;
bool isLongDetected_OUT = false;


int lastState_ZEIT = LOW;  // the previous state from the input pin
int currentState_ZEIT;     // the current reading from the input pin
unsigned long pressedTime_ZEIT  = 0;

bool isPressing_ZEIT = false;
bool isLongDetected_ZEIT = false;

unsigned long waitingTime  = 0;

struct RFID_return {    //Holds RFID Function Response with Employee ID and Log Information (IN/OUT/TimeOut)
  String emp_RFID;
  String inout;
};

// Variables to save date and time
String formattedDate;
String dayStamp;
String timeStamp;
String mergedStamp;
String workinghours_stamp;
String last_firmware_check = "";

//String for storing server response
String response = "";


String ID;
String inout;


String httpRequestData;
int httpResponseCode;

byte error, address; //variable for error and I2C address
int devicecount;

float Montly_hours;


void firmwareUpdate();
int FirmwareVersionCheck();
String  inoutserial();

String FirmwareVer = {
  "2.2"
};
#define URL_fw_Version "https://raw.githubusercontent.com/siotoo/ERP-Next-Employee_Checkin/main/bin_version.txt"
#define URL_fw_Bin "https://raw.githubusercontent.com/siotoo/ERP-Next-Employee_Checkin/main/fw.bin"


RFID_return RFID_response;
StaticJsonDocument<250> jsonDocument;

MFRC522 rfid(SS_PIN, RST_PIN);
// Define NTP Client to get time
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP);

// Create the lcd object address 0x3F and 16 columns x 2 rows 
LiquidCrystal_I2C lcd (0x27, 16,2);  //


void setup() {
  Wire.begin();
  Serial.begin(115200);             //Init serial
  pinMode(WiFi_rst, INPUT);
  SPI.begin(); // init SPI bus
  // Initialize the LCD connected 
  lcd. begin ();
  
  // Turn on the backlight on LCD. 
  lcd. backlight ();
  lcd. print ( "Soft4Automation " ); 
  lcd. setCursor (0, 1);
  lcd. print ( "GmbH            " );
  rfid.PCD_Init(); // init MFRC522

  
  pinMode(BUTTON_IN_PIN, INPUT_PULLUP); //init Button IN using internal Pullup
  pinMode(BUTTON_OUT_PIN, INPUT_PULLUP);//init Button OUT using internal Pullup
  pinMode(BUTTON_ZEIT_PIN, INPUT_PULLUP);//init Button ZEIT using internal Pullup

  
  if (!EEPROM.begin(EEPROM_SIZE)) { //Init EEPROM
    Serial.println("failed to init EEPROM");
    delay(1000);
  }
  
  else
  {
    ssid = readStringFromFlash(0); // Read SSID stored at address 0
    Serial.print("SSID = ");
    Serial.println(ssid);
    pss = readStringFromFlash(40); // Read Password stored at address 40
    Serial.print("psss = ");
    Serial.println(pss);
  }

  WiFi.begin(ssid.c_str(), pss.c_str());

  delay(3500);   // Wait for a while till ESP connects to WiFi

  if (WiFi.status() != WL_CONNECTED) // if WiFi is not connected
  {
    //Init WiFi as Station, start SmartConfig
    WiFi.mode(WIFI_AP_STA);
    WiFi.beginSmartConfig();

    //Wait for SmartConfig packet from mobile
    Serial.println("Waiting for SmartConfig.");
    while (!WiFi.smartConfigDone()) {
      delay(500);
      Serial.print(".");
    }

    Serial.println("");
    Serial.println("SmartConfig received.");

    //Wait for WiFi to connect to AP
    Serial.println("Waiting for WiFi");
    while (WiFi.status() != WL_CONNECTED) {
      delay(500);
      Serial.print(".");
    }

    Serial.println("WiFi Connected.");

    Serial.print("IP Address: ");
    Serial.println(WiFi.localIP());

    // read the connected WiFi SSID and password
    ssid = WiFi.SSID();
    pss = WiFi.psk();
    Serial.print("SSID:");
    Serial.println(ssid);
    Serial.print("PSS:");
    Serial.println(pss);
    Serial.println("Store SSID & PSS in Flash");
    writeStringToFlash(ssid.c_str(), 0); // storing ssid at address 0
    writeStringToFlash(pss.c_str(), 40); // storing pss at address 40
  }
  else
  {
    Serial.println("WiFi Connected");
  }

    // Initialize a NTPClient to get time
  timeClient.begin();
  // Set offset time in seconds to adjust for your timezone, for example:
  // GMT +1 = 3600
  // GMT +8 = 28800
  // GMT -1 = -3600
  // GMT 0 = 0
  timeClient.setTimeOffset(3600);
  if (FirmwareVersionCheck()) {
      firmwareUpdate();
    }
}

void loop() {
  // put your main code here, to run repeatedly:
  lcd. setCursor(0,0);
  lcd. print ( "Soft4Automation " ); //Bitte Transpondern nähern.
  lcd. setCursor (0, 1);
  lcd. print ( "GmbH            " );
  
  while (inout != "IN" && inout != "OUT" && inout != "Time Out" && inout != "ZEIT") {
   
    RFID_response = RFID();  
    inout = RFID_response.inout; 
    ID = RFID_response.emp_RFID;
    
    if (last_firmware_check != dayStamp.substring(0,10))
    {
    last_firmware_check = dayStamp.substring(0,10);
    if (FirmwareVersionCheck()) {
      firmwareUpdate();
    }
    }

    
    rst_millis = millis();
    while (digitalRead(WiFi_rst) == LOW)
    {
    }
      // check the button press time if it is greater than 3sec clear wifi cred and restart ESP 
    if (millis() - rst_millis >= 3000)
    {
      Serial.println("Reseting the WiFi credentials");
      writeStringToFlash("", 0); // Reset the SSID
      writeStringToFlash("", 40); // Reset the Password
      Serial.println("Wifi credentials erased");
      Serial.println("Restarting the ESP");
      delay(500);
      ESP.restart();            // Restart ESP
    }
    
  } //Wait for RFID Response (card being read + user chosing a log)
  Serial.println();
  Serial.print("Log : ");
  Serial.println(inout);
  Serial.print("ID : ");
  Serial.println(ID);
  
  
  while (!timeClient.update()) {timeClient.forceUpdate();} //Update time
  
  // Extract date
  formattedDate = timeClient.getFormattedDate();
  int splitT = formattedDate.indexOf("T");
  dayStamp = formattedDate.substring(0, splitT);
  workinghours_stamp = dayStamp.substring(0,8);
  
  // Extract time
  timeStamp = formattedDate.substring(splitT + 1, formattedDate.length() - 1);
  mergedStamp = "\"" + dayStamp + " " + timeStamp + ".0000";
  
  if (inout == "IN" || inout == "OUT" || inout == "ZEIT") //API Call
  {
    //Initiate HTTP client
    HTTPClient http;
    http.setReuse(true);
    
    //The API URL
    String request_checkin = "https://soft4automation.de/api/method/hrms.hr.doctype.employee_checkin.employee_checkin.add_log_based_on_employee_field";
    String request_employee_data = "https://soft4automation.de/api/resource/Employee?limit_page_length=none&fields=[\"name\"]&filters=[[\"attendance_device_id\",\"like\",\""+ID+"\"]]";
    
    //API Token & Key
    const char * Tkn = "c36bbb4f63ff68c";//d66da3e027d402c
    const char * Key = "d66da3e027d402c";//c36bbb4f63ff68c

    if (inout == "IN" || inout == "OUT")
    {   
      if (inout == "IN")
      {
        lcd. setCursor(0,0);
        lcd. print("Sie Werden      "); 
        lcd. setCursor(0,1);
        lcd. print("eingeloggt      ");
      } 
      else
      {
        lcd. setCursor(0,0);
        lcd. print("Sie Werden      "); 
        lcd. setCursor(0,1);
        lcd. print("ausgeloggt      ");
      } 
      //SetAuthorization
      http.setAuthorization(Tkn, Key);
      //Start Http client
      http.begin(request_checkin);
      //Add Header
      http.addHeader("Content-Type", "application/json");
      //Use HTTP POST request
      httpRequestData = "{\"employee_field_value\" : \"" + ID + "\", \"timestamp\" :  " + mergedStamp + "\" , \"employee_fieldname\" : \"attendance_device_id\", \"log_type\" : \"" + inout + "\"}";
      Serial.print("HTTP Request String: ");
      Serial.println(httpRequestData);
      httpResponseCode = http.POST(httpRequestData);
      //Response from server
      response = http.getString();
  
      Serial.print("HTTP Response code: ");
      Serial.println(httpResponseCode);
  
      Serial.print("HTTP Response: ");
      Serial.println(response); 
      lcd. setCursor(0,1);
      lcd. clear();
      if (response ==  "1" ){
      lcd. print("OK");}
      else
      {lcd. print("FEHLER");}
    //Close connection
    }
    if (inout == "ZEIT")
    {
      //SetAuthorization
      http.setAuthorization(Tkn, Key);
      //Start Http client
      http.begin(request_employee_data);
      //Add Header
      http.addHeader("Content-Type", "application/json");
      Serial.print("Requesting Employee Name: ");
      Serial.println(request_employee_data); 
      httpResponseCode = http.GET(); 
      response = http.getString();

      DynamicJsonDocument doc(2048);
      DeserializationError error = deserializeJson(doc, response);

      if (error) {
        Serial.print("deserializeJson() failed: ");
        Serial.println(error.c_str());
        return;
      }

      const char* employeeID = doc["data"][0]["name"]; // "HR-EMP-00005"


      Serial.print("Response code of Employee Data: ");
      Serial.println(httpResponseCode);
      Serial.print("Response of Employee Data: ");
      Serial.println(response);
      http.end();
      String request_working_hours2 = "https://beta.soft4automation.de/api/resource/Attendance?limit_page_length=none&fields=[\"working_hours\"]&filters=[[\"working_hours\",\">\",\"0.0\"],[\"attendance_date\",\">\",\""+workinghours_stamp+"01\"],[\"employee\",\"like\",\""+ String(employeeID) +"\"]]";
      http.begin(request_working_hours2);
      //Add Header
      http.addHeader("Content-Type", "application/json");
      Serial.print("Requesting Working Hours");
      Serial.println(request_working_hours2); 
      httpResponseCode = http.GET(); 
      response = http.getString();
      Serial.print("Response code of Working Hours Data: ");
      Serial.println(httpResponseCode);
      Serial.print("Response of Working Hours Data: ");
      Serial.println(response);
      StaticJsonDocument<4096> doc2;
      DeserializationError error2 = deserializeJson(doc2, response);

      if (error2) {
      Serial.print("deserializeJson() failed: ");
      Serial.println(error2.c_str());
      return;
      }

      for (JsonObject data_item : doc2["data"].as<JsonArray>()) {
        float data_item_working_hours = data_item["working_hours"]; // 10, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, ...      
        Montly_hours = Montly_hours + data_item_working_hours;
      }
      lcd. setCursor(0,0);
      lcd. print("Monats Arbeitstu");
      lcd. setCursor(0,1);
      lcd.print("nden :           ");
      lcd. setCursor(8,1);
      lcd. print(Montly_hours);
      Serial.println(Montly_hours);  
    }
    Montly_hours = 0;
    http.end();
  }
  else
  {Serial.println();Serial.println("Time Out");}
  delay(5000);
  inout = "";
}


void writeStringToFlash(const char* toStore, int startAddr) {
  int i = 0;
  for (; i < LENGTH(toStore); i++) {
    EEPROM.write(startAddr + i, toStore[i]);
  }
  EEPROM.write(startAddr + i, '\0');
  EEPROM.commit();
}


String readStringFromFlash(int startAddr) {
  char in[128]; // char array of size 128 for reading the stored data 
  int i = 0;
  for (; i < 128; i++) {
    in[i] = EEPROM.read(startAddr + i);
  }
  return String(in);
}

RFID_return RFID() {
  RFID_return msg_instance;
  
  if (rfid.PICC_IsNewCardPresent()) { // new tag is available
    if (rfid.PICC_ReadCardSerial()) { // NUID has been readed
      
      MFRC522::PICC_Type piccType = rfid.PICC_GetType(rfid.uid.sak);
      Serial.print("RFID/NFC Tag Type: ");
      Serial.println(rfid.PICC_GetTypeName(piccType));

      // print UID in Serial Monitor in the hex format
      Serial.print("UID:");
      for (int i = 0; i < rfid.uid.size; i++) {
        Serial.print(rfid.uid.uidByte[i] < 0x10 ? " 0" : " ");
        Serial.print(rfid.uid.uidByte[i], HEX);
        
      }
      char str[32] = "";
      array_to_string(rfid.uid.uidByte, rfid.uid.size, str); //Insert (byte array, length, char array for output)
      msg_instance.emp_RFID= str;
      Serial.println("Essa é a var STR : ");
      Serial.println(str);
      if (str != "044057924D5380" and str != "04823E9A4D5380" and str!= "04D53F9A4D5380") 
      {
        lcd.clear();
        lcd.setCursor(0,0);
        lcd.print("Mitarbeiter");
        lcd.setCursor(0,1);
        lcd.print("existiert nicht");
        delay(1500);
        exit; 
      }
      else{
        if (str == "044057924D5380") {
        lcd.clear();
        lcd.setCursor(0,0);
        lcd.print("Hallo");
        lcd.setCursor(0,1);
        lcd.print("Felipe");
        }
        if (str == "04823E9A4D5380") {
        lcd.clear();
        lcd.setCursor(0,0);
        lcd.print("Hallo");
        lcd.setCursor(0,1);
        lcd.print("Conny");
        }
        if (str == "04D53F9A4D5380") {
        lcd.clear();
        lcd.setCursor(0,0);
        lcd.print("Servus");
        lcd.setCursor(0,1);
        lcd.print("Erik");
        }
      delay(1500);  
      }

      msg_instance.inout = button_checkin();
      Serial.println(msg_instance.inout);

      
      Serial.println();
      
      rfid.PICC_HaltA(); // halt PICC
      rfid.PCD_StopCrypto1(); // stop encryption on PCD
    }
  }
  return msg_instance;
}



String button_checkin() {
  
  isLongDetected_IN = false;
  isLongDetected_OUT = false;
  isLongDetected_ZEIT = false;
  
  lcd. setCursor (0, 0);
  lcd. clear();
  lcd. print("Bitte Waehlen");
  Serial.println("");
  Serial.println("IN");
  Serial.println(isLongDetected_IN);

  Serial.println("OUT");
  
  Serial.println(isLongDetected_OUT);
  String status_string = "0";
  waitingTime = millis();
  
  while(isLongDetected_IN != true && isLongDetected_OUT != true && isLongDetected_ZEIT != true && millis()- waitingTime < 30000 ){
    // read the state of the switch/button:
    currentState_IN = digitalRead(BUTTON_IN_PIN);
    currentState_OUT = digitalRead(BUTTON_OUT_PIN);
    currentState_ZEIT = digitalRead(BUTTON_ZEIT_PIN);
    
    if(lastState_IN == HIGH && currentState_IN == LOW) {        // button is pressed
      pressedTime_IN = millis();
      isPressing_IN = true;
      isLongDetected_IN = false;
    } else if(lastState_IN == LOW && currentState_IN == HIGH) { // button is released
      isPressing_IN = false;
    }

     if(lastState_OUT == HIGH && currentState_OUT == LOW) {        // button is pressed
      pressedTime_OUT = millis();
      isPressing_OUT = true;
      isLongDetected_OUT = false;
    } else if(lastState_OUT == LOW && currentState_OUT == HIGH) { // button is released
      isPressing_OUT = false;
    }

    if(lastState_ZEIT == HIGH && currentState_ZEIT == LOW) {        // button is pressed
      pressedTime_ZEIT = millis();
      isPressing_ZEIT = true;
      isLongDetected_ZEIT = false;
    } else if(lastState_ZEIT == LOW && currentState_ZEIT == HIGH) { // button is released
      isPressing_ZEIT = false;
    }
  
    if(isPressing_IN == true && isLongDetected_IN == false) {
      long pressDuration_IN = millis() - pressedTime_IN;
  
      if( pressDuration_IN > LONG_PRESS_TIME) {
        Serial.println("A long press is detected");
        status_string = "IN";
        isLongDetected_IN = true;
      }
    }

     if(isPressing_OUT == true && isLongDetected_OUT == false) {
      long pressDuration_OUT = millis() - pressedTime_OUT;
  
      if( pressDuration_OUT > LONG_PRESS_TIME ) {
        Serial.println("A long press is detected");
        status_string = "OUT";
        isLongDetected_OUT = true;
      }
    }

     if(isPressing_ZEIT == true && isLongDetected_ZEIT == false) {
      long pressDuration_ZEIT = millis() - pressedTime_ZEIT;
  
      if( pressDuration_ZEIT > LONG_PRESS_TIME ) {
        Serial.println("A long press is detected");
        status_string = "ZEIT";
        isLongDetected_OUT = true;
      }
    }
  
    // save the the last state
    lastState_IN = currentState_IN;
    lastState_OUT = currentState_OUT;
    lastState_ZEIT = currentState_ZEIT;
    lcd. setCursor (0, 1);
    lcd. print ( "                " );
    lcd. print ( millis () / 1000);
    lcd. print ( "SECONDS" );
    }
  if (isLongDetected_IN == false && isLongDetected_OUT == false && isLongDetected_ZEIT == false) status_string = "Time Out";
  return status_string;
}


void array_to_string(byte array[], unsigned int len, char buffer[])
{
   for (unsigned int i = 0; i < len; i++)
   {
      byte nib1 = (array[i] >> 4) & 0x0F;
      byte nib2 = (array[i] >> 0) & 0x0F;
      buffer[i*2+0] = nib1  < 0xA ? '0' + nib1  : 'A' + nib1  - 0xA;
      buffer[i*2+1] = nib2  < 0xA ? '0' + nib2  : 'A' + nib2  - 0xA;
   }
   buffer[len*2] = '\0';
}

void firmwareUpdate(void) {
  lcd.clear();
  lcd.setCursor(0,0);
  lcd.print("UPDATING");
  WiFiClientSecure client;
  client.setCACert(rootCACertificate);
  t_httpUpdate_return ret = httpUpdate.update(client, URL_fw_Bin);

  switch (ret) {
  case HTTP_UPDATE_FAILED:
    Serial.printf("HTTP_UPDATE_FAILD Error (%d): %s\n", httpUpdate.getLastError(), httpUpdate.getLastErrorString().c_str());
    break;

  case HTTP_UPDATE_NO_UPDATES:
    Serial.println("HTTP_UPDATE_NO_UPDATES");
    break;

  case HTTP_UPDATE_OK:
    Serial.println("HTTP_UPDATE_OK");
    break;
  }
}
int FirmwareVersionCheck(void) {

  String payload;
  int httpCode;
  String fwurl = "";
  fwurl += URL_fw_Version;
  fwurl += "?";
  fwurl += String(rand());
  Serial.println(fwurl);
  WiFiClientSecure * client = new WiFiClientSecure;

  if (client) 
  {
    client -> setCACert(rootCACertificate);

    // Add a scoping block for HTTPClient https to make sure it is destroyed before WiFiClientSecure *client is 
    HTTPClient https;
    if (https.begin( * client, fwurl)) 
    { // HTTPS      
      Serial.print("[HTTPS] GET...\n");
      // start connection and send HTTP header
      delay(100);
      httpCode = https.GET();
      delay(100);
      Serial.println(httpCode);
      if (httpCode == HTTP_CODE_OK) // if version received
      {
        payload = https.getString(); // save received version
        Serial.println(payload);
      } else {
        Serial.print("error in downloading version file:");
        Serial.println(httpCode);
      }
      https.end();
    }
    delete client;
  }
      
  if (httpCode == HTTP_CODE_OK) // if version received
  {
    payload.trim();
    if (payload.equals(FirmwareVer)) {
      Serial.printf("\nDevice already on latest firmware version:%s\n", FirmwareVer);
      return 0;
    } 
    else 
    {
      Serial.println(payload);
      Serial.println("New firmware detected");
      return 1;
    }
  } 
  return 0;  
}
