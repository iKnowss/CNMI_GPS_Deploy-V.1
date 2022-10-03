// ! code gps module v.1 03/10/2022
// ! เว่อชั้น อัพโหลดโค้ดแบบ OTA ได้ และนำไปใช้แล้ว

#include <WiFi.h>
#include <WiFiClient.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <Update.h>
#include <PubSubClient.h>
#include <SoftwareSerial.h>
#include <TinyGPS++.h>
#include <stdio.h>
#include <string.h>

#define LED 2
//------------------------------------------------------------
//ID
int boardID = 17;

// publish/username/apiKeyIn
const char* TopicGPS = "gps/17";
const char* TopicStatus = "status/17";

// TODO: ESP32 MQTT user config
const char* ssid = "CNMI-017";
const char* password = "Cnmi@017";
//-------------------------------------------------------------

const char* host = "esp32";
//MQTT
const char* Mqttusername = "gpsclient"; // Mqtt username
const char* Mqttpassword = "1q2w3e4r5t"; // Mqtt password
const char* mqtt_server = "mqtt.rfidpatient.com";//AskSensors MQTT config
unsigned int mqtt_port = 1883;

// client id
String client_id = "esp32-client-";;

// write interval (in ms)
const unsigned int writeInterval = 100;

static const int RXPin = 19, TXPin = 18;
static const uint32_t GPSBaud = 9600;

bool cn;
bool GPS;

unsigned long previousMillis = 0;
unsigned long interval = 60000;
// objects
WiFiClient espClient;
PubSubClient client(espClient);
TinyGPSPlus gps; // The TinyGPS++ object
SoftwareSerial ss(RXPin, TXPin); // The serial connection to the GPS device

// set Port of Web Server is Port 80
WebServer server(80);

// status
String gps_normal = "GPS_NORMAL";
String gps_no_data = "GPS_NO_DATA";
String gps_issue = "GPS_ISSUE";
String mqtt_connected = "MQTT_CONNECTED";
String mqtt_online = "MQTT_ONLINE";
char status_payload[20] = "";

// หน้า Login
const char* loginIndex = 
 "<form name='loginForm'>"
    "<table width='20%' align='center'>"
        "<tr>"
            "<td colspan=2>"
                "<center><font size=4><b>Login Page</b></font></center>"
                "<br>"
            "</td>"
            "<br>"
            "<br>"
        "</tr>"
        "<td>Username:</td>"
        "<td><input type='text' size=25 name='userid'><br></td>"
        "</tr>"
        "<br>"
        "<br>"
        "<tr>"
            "<td>Password:</td>"
            "<td><input type='Password' size=25 name='pwd'><br></td>"
            "<br>"
            "<br>"
        "</tr>"
        "<tr>"
            "<td><input type='submit' onclick='check(this.form)' value='Login'></td>"
        "</tr>"
    "</table>"
"</form>"
"<script>"
    "function check(form)"
    "{"
    "if(form.userid.value=='admin' && form.pwd.value=='admin')"
    "{"
    "window.open('/serverIndex')"
    "}"
    "else"
    "{"
    " alert('Error Password or Username')/*displays error message*/"
    "}"
    "}"
"</script>";
 

// หน้า Index Page
const char* serverIndex = 
"<script src='https://ajax.googleapis.com/ajax/libs/jquery/3.2.1/jquery.min.js'></script>"
"<form method='POST' action='#' enctype='multipart/form-data' id='upload_form'>"
   "<input type='file' name='update'>"
        "<input type='submit' value='Update'>"
    "</form>"
 "<div id='prg'>progress: 0%</div>"
 "<script>"
  "$('form').submit(function(e){"
  "e.preventDefault();"
  "var form = $('#upload_form')[0];"
  "var data = new FormData(form);"
  " $.ajax({"
  "url: '/update',"
  "type: 'POST',"
  "data: data,"
  "contentType: false,"
  "processData:false,"
  "xhr: function() {"
  "var xhr = new window.XMLHttpRequest();"
  "xhr.upload.addEventListener('progress', function(evt) {"
  "if (evt.lengthComputable) {"
  "var per = evt.loaded / evt.total;"
  "$('#prg').html('progress: ' + Math.round(per*100) + '%');"
  "}"
  "}, false);"
  "return xhr;"
  "},"
  "success:function(d, s) {"
  "console.log('success!')" 
 "},"
 "error: function (a, b, c) {"
 "}"
 "});"
 "});"
 "</script>";

void Wifi_connection(){
// เชื่อมต่อ WiFi
  WiFi.begin(ssid, password);
  Serial.println("");
// รอจนกว่าจะเชื่อมต่อสำเร็จ
  while (WiFi.status() != WL_CONNECTED) {
    digitalWrite(LED, LOW);
    delay(500);
    Serial.print(".");
  }
// หากเชื่อมต่อสำเร็จ แสดงข้อมูลต่างๆทาง Serial Monitor
  Serial.println("");
  digitalWrite(LED, HIGH);
  Serial.print("********** connecting to WIFI : ");
  Serial.println(ssid);
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());
}

void MQTT_connection(){
  Serial.println("*****************************************************");
  Serial.println("********** Program Start : ESP32 publishes NEO-6M GPS position to AskSensors over MQTT");

  client.setServer(mqtt_server, mqtt_port);
  client.setCallback(callback);

  client_id += String(WiFi.macAddress());
  // GPS baud rate
  ss.begin(GPSBaud);
}

void mDNS_use(){
  if (!MDNS.begin(host)) { //http://esp32.local
    Serial.println("Error setting up MDNS responder!");
    while (1) {
      delay(1000);
    }
  }
  Serial.println("mDNS responder started");
}

void index_server(){
  server.on("/", HTTP_GET, []() {
    server.sendHeader("Connection", "close");
    server.send(200, "text/html", loginIndex);
  });
  server.on("/serverIndex", HTTP_GET, []() {
    server.sendHeader("Connection", "close");
    server.send(200, "text/html", serverIndex);
  });
}
void file_upload(){
  server.on("/update", HTTP_POST, []() {
    server.sendHeader("Connection", "close");
    server.send(200, "text/plain", (Update.hasError()) ? "FAIL" : "OK");
    ESP.restart();
  }, []() {
    HTTPUpload& upload = server.upload();
    if (upload.status == UPLOAD_FILE_START) {
      Serial.printf("Update: %s\n", upload.filename.c_str());
      if (!Update.begin(UPDATE_SIZE_UNKNOWN)) { //start with max available size
        Update.printError(Serial);
      }
    } else if (upload.status == UPLOAD_FILE_WRITE) {

// แฟลช(เบิร์นโปรแกรม)ลง ESP32
      if (Update.write(upload.buf, upload.currentSize) != upload.currentSize) {
        Update.printError(Serial);
      }
    } else if (upload.status == UPLOAD_FILE_END) {
      if (Update.end(true)) { //true to set the size to the current progress
        Serial.printf("Update Success: %u\nRebooting...\n", upload.totalSize);
      } else {
        Update.printError(Serial);
      }
    }
  });
  server.begin();
}

// GPS displayInfo
void displayInfo() {

  if (gps.location.isValid()) {
    String latitude = String(gps.location.lat(),10);
    String longitude = String(gps.location.lng(),10);
    String pipeline = "|";
    
    //! fomat data for send
    String data_local = boardID + pipeline + latitude + pipeline + longitude;
    
    Serial.println("********** Publish MQTT data to ASKSENSORS");
    
    char mqtt_payload[50] = "";
    data_local.toCharArray(mqtt_payload, 50);

    gps_normal.toCharArray(status_payload, 20);
    
    Serial.print("Publish message: ");
    Serial.println(mqtt_payload);
    
    client.publish(TopicGPS, mqtt_payload);
    client.publish(TopicStatus, status_payload);
    
    Serial.println("> MQTT data published");
    Serial.println("********** End ");
    Serial.println("*****************************************************");
    
    delay(writeInterval);// delay 
  } else {
    Serial.println(F("INVALID"));
    Serial.println(client.state());

    gps_no_data.toCharArray(status_payload, 20);
    client.publish(TopicStatus,status_payload);
    delay(writeInterval);// delay 
  }
}

//MQTT callback
void callback(char* topic, byte* payload, unsigned int length) {
  Serial.print("Message arrived [");
  Serial.print(topic);
  Serial.print("] ");
  for (int i = 0; i < length; i++) {
    Serial.print((char)payload[i]);
  }
  Serial.println();
  Serial.println("-----------------------");
}


//MQTT reconnect
void Reconnect() {
  Serial.println("");
  Serial.printf("The client %s connects to the public mqtt broker\n", client_id.c_str());
  // Loop until we're reconnected
  while (!client.connected()) {
    Serial.print("********** Attempting MQTT connection...");
    // Attempt to connect
    if (client.connect(client_id.c_str() , Mqttusername, Mqttpassword)) { //, Mqttusername, Mqttpassword
      Serial.println("-> MQTT client connected");
      cn = true;
      if (cn == true) {
        mqtt_connected.toCharArray(status_payload, 20);
        client.publish(TopicStatus, status_payload);
        cn = false;
      }
    } else {
      Serial.print("failed, rc=");
      Serial.print(client.state());
      Serial.println("-> try again in 3 seconds");
      // Wait 5 seconds before retrying
      delay(5000);

      if((client.state() == -2) && (millis() >= 60000)) {
          Wifi_connection();
        }
    }
    // Serial.println("Connection Status: " + String(client.state()));
  }
}

void MQTT_online(){
  unsigned long currentMillis = millis();
  if (currentMillis - previousMillis >= interval){
    mqtt_online.toCharArray(status_payload, 20);
    client.publish(TopicStatus, status_payload);
    
    Serial.println("> MQTT_ONLINE");
    previousMillis = currentMillis;
  }
}

void setup(void) {
  Serial.begin(115200);
  pinMode(LED, OUTPUT);
  Wifi_connection();
  MQTT_connection();
  mDNS_use();// ใช้งาน mDNS
  index_server();// แสดงหน้า Server Index หลังจาก Login
  file_upload();// ขั้นตอนการ Upload ไฟล์
}

void loop(void) {
// Server พร้อมการเปิดหน้าเว็บทุก Loop (เพื่อรอการเรียกหน้าเว็บเพื่อรับโปรแกรมใหม่ตลอดเวลา)
  server.handleClient();

  if (!client.connected())
    Reconnect();

  client.loop();
  
  while (ss.available() > 0)
    if (gps.encode(ss.read())){
        server.handleClient();
      displayInfo();
      MQTT_online();
      }
    if (millis() > 30000 && gps.charsProcessed() < 10)
    {
      server.handleClient();
      Serial.println(F("No GPS detected: check wiring."));
      
      gps_issue.toCharArray(status_payload, 20);
      client.publish(TopicStatus, status_payload);
      
      while(!gps.encode(ss.read())){
        server.handleClient();
          Serial.println(F("No GPS detected but online"));
          MQTT_online();
        }
    }
}
