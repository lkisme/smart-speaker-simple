/*
  Wiring：
  SU_03T      ESP01S
  VCC         5V
  GND         GND
  TX/B7       D4(GPIO2)
  RX/B6       D3(GPIO0)
*/

#include <FS.h>//保存记录
#include <SoftwareSerial.h>//导入软串口库
#include <ESP8266WiFi.h> 
#include <ESP8266WebServer.h>//在MQTT之外，保留一个http的入口
#include <PubSubClient.h>//MQTT
#include <ArduinoJson.h> //5.13.5
#include <EspSaveCrash.h>//定位问题
#include <ESP8266mDNS.h>
#include <WiFiUdp.h>
#include <ArduinoOTA.h>
//#include "jx_uart_send.h" //从平台上下载的函数

#define ESP_RX  2 //D2
#define ESP_TX  0 //D3
#define MQTT_CONNECT_TIME_MILLI 30000
//WiFi重连最多重试8分钟，否则重启
#define WIFI_CONNECT_TIME_MILLI 480000
unsigned long retryTime = 0;

#define TOPIC_PREFIX "smart-speaker/"
#define TOPIC_SUFFIX_AVAILABILITY "/availability"
#define TOPIC_SUFFIX_COMMAND "/command"
#define TOPIC_SUFFIX_VERSION "/version"
#define TOPIC_SUFFIX_LOG "/log"
#define VERSION "1.6"//
#define MAX_HISTORY_SIZE 10
#define UART_MAX_READ_BYTE_SIZE 256
#define UART_MAX_READ_PAYLOAD_SIZE 240
#define UART_HEADER_BYTE_SIZE 16
#define CLIENT_ID "SU-speaker-%06X"
//MQTT server address
#define MQTT_SERVER "ha.cn"
#define DEFAULT_SSID "SSID"
#define DEFAULT_PASSWD "PASS"
int restarted = 0;
boolean otaFlag = false;

char topic_availability[64];
char topic_command[64];
char topic_order[64];
char topic_version[64];
char topic_log[64];
char topic_attr[64];

char clientId[50]; 
char device_name[60] = "su03t";
char mqtt_server[60];
char wifi_ssid[20];
char wifi_pass[20];
boolean hasCredentials = false;

String history[10];
int historyIndex = 0;
int realSize = 0;


SoftwareSerial SU_03T(ESP_RX, ESP_TX);  //定义软串口接脚D4与D3

WiFiClient espClient;                   // 定义wifiClient实例
PubSubClient client(espClient);    
EspSaveCrash SaveCrash;
ESP8266WebServer webServer ( 80 );

void setupTopics() {
  strcpy(topic_availability, TOPIC_PREFIX);
  strcat(topic_availability, device_name);
  strcat(topic_availability, TOPIC_SUFFIX_AVAILABILITY);
  
  strcpy(topic_command, TOPIC_PREFIX);
  strcat(topic_command, device_name);
  strcat(topic_command, TOPIC_SUFFIX_COMMAND);
  
  strcpy(topic_order, TOPIC_PREFIX);
  strcat(topic_order, device_name);
  strcat(topic_order, "/order");
  
  strcpy(topic_version, TOPIC_PREFIX);
  strcat(topic_version, device_name);
  strcat(topic_version, TOPIC_SUFFIX_VERSION);
  
  strcpy(topic_log, TOPIC_PREFIX);
  strcat(topic_log, device_name);
  strcat(topic_log, TOPIC_SUFFIX_LOG);
  
  strcpy(topic_attr, TOPIC_PREFIX);
  strcat(topic_attr, device_name);
  strcat(topic_attr, "/attrs");
  sprintf(clientId, CLIENT_ID, ESP.getChipId());
}
void connectWiFi() {
  WiFi.disconnect();
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_STA);
  if (hasCredentials) {
    WiFi.begin(wifi_ssid, wifi_pass);
  } else {
    WiFi.begin(DEFAULT_SSID, DEFAULT_PASSWD);
  }
  
  sprintf(mqtt_server, MQTT_SERVER);
  loadConfig();
  Serial.printf("Device name : %s", device_name);

  for (int i = 0; i < 25; i++) {
    if ( WiFi.status() != WL_CONNECTED ) {
      delay ( 1000 );
      Serial.print ( "." );
    } else {
      break;
    }
  }
  delay(10000);
  if ( WiFi.status() != WL_CONNECTED ) {
    Serial.println ("Failed to connect WiFi");
  } else {
    Serial.println ("Success to connect WiFi");
  }
}
void setup() {
  Serial.begin(115200);
  SU_03T.begin(115200);
  
  connectWiFi();
  setupTopics();
  Serial.println("WiFi Connected!");
  Serial.print("device_name:");
  Serial.println(device_name);
  client.setServer(mqtt_server, 1883);                              //设定MQTT服务器与使用的端口，1883是默认的MQTT端口
  client.setCallback(callback); 

  
    webServer.on("/history", []() {
      String coms = "History:\n";
      
      for(int i = 0;i<realSize; i++) {
        coms += history[(historyIndex - realSize + MAX_HISTORY_SIZE)%MAX_HISTORY_SIZE];
        coms += "\n";
      }
      webServer.send ( 200, "text/plain", coms );
    });
    webServer.on("/hello", []() {
      
      webServer.send ( 200, "text/plain", "hello" );
    });
    webServer.on("/", []() {
      
      webServer.send ( 200, "text/plain", clientId);
    });
    webServer.on("/ota", []() {
      otaFlag = true;
      webServer.send ( 200, "text/plain", "OTA!!!" );
      
    });
    webServer.on("/error", []() {
      char _debugOutputBuffer[1024];
      SaveCrash.print(_debugOutputBuffer, 1024);
      
      webServer.send ( 200, "text/plain", _debugOutputBuffer);
    });
    webServer.on("/crash", []() {
      Serial.println("clearing crash log");
      SaveCrash.clear();
      
      webServer.send ( 200, "text/plain", "Crash log cleared!" );
    });
    webServer.on("/changeName", handleChangeName);
    webServer.on("/changeWifi", handleChangeCredentials);
    webServer.begin();
    setupOTA();
}
void loop() {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi断开了，尝试重连");
    if (retryTime > WIFI_CONNECT_TIME_MILLI) {
      ESP.reset();
    }
    unsigned long now = millis();
    connectWiFi();
    delay(100);
    retryTime += (millis() - now);
    return;
  }
  retryTime =0;
  if (otaFlag) {
    ArduinoOTA.handle();
//    return;
  }
  if (!client.connected()) {
    reconnect();
  }
  client.loop();
  webServer.handleClient();
    
  if (SU_03T.available()) {
    byte content[UART_MAX_READ_BYTE_SIZE];
    int lengthOfBytes = SU_03T.readBytes(content, UART_MAX_READ_BYTE_SIZE);
    /**TODO 容错，对于前几个字符丢失的情况**/
    if (lengthOfBytes >= 0 ) {//否则数据为空
      
      
      char* realChar = new char[lengthOfBytes];
      memcpy(realChar,content,lengthOfBytes);
      realChar[lengthOfBytes] = 0;
      
      Serial.print("串口命令=");
      Serial.println(realChar); //串口打印SU_03T输出的反馈数据
      Serial.println("---");
      client.publish(topic_command, realChar);
    }
//    Serial.println("读取空，退出。。。");
    
    delay(50);
  }
  
  
}

void reconnect() {
  int timeConsumed = 0;
  while (timeConsumed < MQTT_CONNECT_TIME_MILLI && !client.connected()) {
    Serial.print("Attempting MQTT connection...");
      
    // Attempt to connect
    if (client.connect(clientId, topic_availability, 0, true, "offline")) {
      Serial.println("connected");
  
      // 连接成功时订阅主题
      client.publish(topic_availability, "online", true);
      client.subscribe(topic_order);
      char attrs[200];
      sprintf(attrs, "{\"ip\":\"%s\", \"version\":\"%s\", \"client_id\":\"%s\", \"rssi\":\"%d dB\"}", WiFi.localIP().toString().c_str(), VERSION, clientId, WiFi.RSSI());
      
      client.publish(topic_attr, attrs, true);
      client.publish(topic_version, VERSION, true);
      if (restarted == 0) {
        Serial.println("First time restarted");
        restarted = 1;
      }
    } else {
      Serial.print("failed, rc=");
      Serial.print(client.state());
      Serial.println(" try again in 5 seconds");
      // Wait 5 seconds before retrying
      delay(5000);
      timeConsumed += 5000;
    }
  }
}

void setupOTA(){

  // Port defaults to 8266
  // ArduinoOTA.setPort(8266);

  // Hostname defaults to esp8266-[ChipID]
   ArduinoOTA.setHostname(clientId);

   ArduinoOTA.setPassword("lkk");

  ArduinoOTA.onStart([]() {
    String type;
    if (ArduinoOTA.getCommand() == U_FLASH) {
      type = "sketch";
    } else { // U_FS
      type = "filesystem";
    }

    // NOTE: if updating FS this would be the place to unmount FS using FS.end()
    Serial.println("Start updating " + type);
  });
  ArduinoOTA.onEnd([]() {
    Serial.println("\nEnd");
  });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
//    Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
  });
  ArduinoOTA.onError([](ota_error_t error) {
    Serial.printf("Error[%u]: ", error);
//    char progress[100];
//    sprintf(progress,"Error[%u]: ", error);
//    client.publish(topic_log, progress);
    if (error == OTA_AUTH_ERROR) {
      Serial.println("Auth Failed");
    } else if (error == OTA_BEGIN_ERROR) {
      Serial.println("Begin Failed");
    } else if (error == OTA_CONNECT_ERROR) {
      Serial.println("Connect Failed");
    } else if (error == OTA_RECEIVE_ERROR) {
      Serial.println("Receive Failed");
    } else if (error == OTA_END_ERROR) {
      Serial.println("End Failed");
    }
  });
  ArduinoOTA.begin();
}

void saveConfig() {
  Serial.println("saving config");
  DynamicJsonBuffer jsonBuffer;
  JsonObject& json = jsonBuffer.createObject();
  json["device_name"] = device_name;
  json["wifi_ssid"] = wifi_ssid;
  json["wifi_pass"] = wifi_pass;

  File configFile = SPIFFS.open("/device.json", "w");
  if (!configFile) {
    Serial.println("failed to open config file for writing");
  }

  json.printTo(Serial);
  json.printTo(configFile);
  configFile.close();
}

void handleChangeName(){
  Serial.println("handleChangeName");
  if (webServer.hasArg("device_name")){
    webServer.arg("device_name").toCharArray(device_name, 60);
    saveConfig();
    webServer.send(200, "text/html", "ChangeName");
    ESP.reset();
  }
  
}

void handleChangeCredentials(){
  Serial.println("handleChangeCredentials");
  if (webServer.hasArg("ssid") && webServer.hasArg("pass")){
    webServer.arg("ssid").toCharArray(wifi_ssid, 20);
    webServer.arg("pass").toCharArray(wifi_pass, 20);
    saveConfig();
    webServer.send(200, "text/html", "ChangeCredentials");
    ESP.reset();
  }
  
}

void loadConfig() {
   if (SPIFFS.begin()) {
    //"mounted file system"
   } else {
    Serial.println("Failed to mount file system ");
    SPIFFS.format();
    if (SPIFFS.begin()) {
      Serial.println("mounted file system after formating");
    }
  }
  if (SPIFFS.exists("/device.json")) {
    //file exists, reading and loading
    Serial.println("file exists, reading and loading");
    File configFile = SPIFFS.open("/device.json", "r");
    if (configFile) {
      size_t size = configFile.size();
      // Allocate a buffer to store contents of the file.
      std::unique_ptr<char[]> buf(new char[size]);
      configFile.readBytes(buf.get(), size);
      DynamicJsonBuffer jsonBuffer;
      JsonObject& json = jsonBuffer.parseObject(buf.get());
      json.printTo(Serial);
      if (json.success()) {
        if (json.containsKey("device_name"))
          strcpy(device_name, json["device_name"]);
        if (json.containsKey("wifi_ssid") && json.containsKey("wifi_pass")) {
          strcpy(wifi_ssid, json["wifi_ssid"]);
          strcpy(wifi_pass, json["wifi_pass"]);
          hasCredentials = true;
        }          
        
      } else {
        Serial.println("failed to load json config");
      }
    }
  }
}
void callback(char* topic, byte* payload, unsigned int length) {
  Serial.print("Message arrived [");
  Serial.print(topic);   // 打印主题信息
  Serial.print("] ");
  Serial.print("Payload [");
  char message[length];
  for(int i=0; i< length; i++) {
    message[i] = (char)payload[i];
    Serial.print(message[i]); 
  }
  Serial.println("] ");
  if (compare(topic, topic_order)) {
    StaticJsonBuffer<256> jsonBuffer;
    JsonObject& json = jsonBuffer.parseObject(message);
    if (json.success()) {
      Serial.println("Json deseiralization successfully!");
      if (json.containsKey("cmd") && json.containsKey("para")) {
        Serial.println("Json content legal !");
        const char* command = json["cmd"];
        int parameter = json["para"];
        unsigned char buff[9] = {0};
        /*
        if (compare(command, "alert")) {
          _uart_alert(parameter,buff);//由于第一次设置变量，不会触发播放语音
          serialPrint(buff);
          _uart_read_cmd(0, buff);//所以进行第二次设置变量，且设置为不播放语音的变量
          delay(100);
        }else if (compare(command, "status")) {
          _uart_report_status(parameter,buff);
          serialPrint(buff);
          _uart_read_cmd(-1, buff);
          delay(100);
        }else if (compare(command, "cmd")) {
          _uart_read_cmd(parameter, buff);
          serialPrint(buff);
          _uart_read_cmd(0, buff);
          delay(100);
        }
        */
        serialPrint(buff);
      }
      
    } 
  }
  
}
bool compare(const char* topicOri, const char* toAdd) {
  if (strstr(topicOri, toAdd) != NULL) {
    return true;
  } else {
    return false;
  }
}

void serialPrint(unsigned char* buff) {
  int len = 9;
  byte bytes[len];
  int i = 0;
  unsigned char c;
  for (i = 0; i < len; i++) {
    bytes[i] = (byte)buff[i];
//    SU_03T.print( c);
    Serial.printf("%2x ",buff[i]);
  }
  Serial.println();
  SU_03T.write(bytes, len);
  
}
