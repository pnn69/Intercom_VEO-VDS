#include <Arduino.h>
#include <ArduinoJson.h>    //use ver > 6.0
#include <ArduinoOTA.h>     //use ver 1.0.0
#include <ESP8266WiFi.h>
#include <NTPClient.h>
#include <SoftwareSerial.h>
#include <Ticker.h>
#include <UniversalTelegramBot.h>
#include <WiFiClientSecure.h>
#include <TelnetStream.h>
/*
 change this in PubSubClient.h
 #define MQTT_MAX_PACKET_SIZE 128
 Put this line infront of the lib include.
 Otherwise it has no effect.
 Needed for the long pacages generatied by domotiz
*/
#define MQTT_MAX_PACKET_SIZE 400
#include <PubSubClient.h>


#if defined(ESP8266) && !defined(D5)
#define D5 (14)
#define D6 (12)
#define D7 (13)
#define D8 (15)
#define D0 (16)
#define TX (1)
#endif


#define Video D6
#define displayOn D0
#define Key D5
#define BlueLed 2 // Blue led
#define mqtt_user "alibabba"
#define mqtt_password "sesamopenu"
#define BlueLedOff HIGH
#define BlueLedOn LOW
#define sys_idle 0
#define sys_open_start 1
#define sys_open_front_door 3
#define sys_send_ipadress 2

// your Bot Token (Get from Botfather Telegram)
#define botToken "1045368619:AAGtCFl__nxhqg7W3JQXbSHDb5gNR3pBC7E"
String TelegramChatId = "1086363450"; // This can be got by using a bot called "myIdBot"
String CHAT_ID = "1086363450";
//Alocate the JSON document
StaticJsonDocument<500> doc;

//Checks for new messages every 1 second.
int botRequestDelay = 1000;
unsigned long lastTimeBotRan;

Ticker flipper;

//#define work
#ifdef work
const char *ssid = "airsupplies";
const char *password = "02351228133648477429";
const char *mqtt_server = "95.97.132.178";
const char *domoticz_server = "95.97.132.178";
#else
const char *ssid = "NicE_Engineering_UPC";
const char *password = "1001100110";
const char *mqtt_server = "192.168.1.11";
const char *domoticz_server = "192.168.1.11";
#endif
const char *domoticz_port = "8084";
const char *host = "alibaba";
const char idx_txt = 24;     // IDX
const char idx_bel = 4;      // IDX of deurbel
const char idx_sesam = 12;   // IDX
const char idx_display = 30; // IDX dispay switch

SoftwareSerial swSer;
unsigned long timestamp;
unsigned long belstamp;
unsigned long displayoff;

const int buflen = 100;
char inbuf[buflen];
char logbuf[buflen];
int logbufcnt = 0;
unsigned long logstamp;
char idbuf[] = {0x59, 0xF0, 0xA9, 0x05}; //Doorbel code 820
char msg[150];
byte mac[6];
char stamp[10];
char TckAck, TckCnt = 0;
char sys_status = sys_idle;

WiFiClient espClient;
WiFiClientSecure teleclient;
UniversalTelegramBot bot(botToken, teleclient);
PubSubClient client(espClient);
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "europe.pool.ntp.org", 3600, 60000);

/* ****************************************************************************/
/* Over the air setup */
/* If timers are used kill the processes befor going to dwonload the new */
/* firmware */
/* To realise that kill them in the function ArduinoOTA.onStart */
/* ****************************************************************************/
void setup_OTA(){
    char buf[30];
    Serial.println();
    Serial.print("[SETUP] OTA...");
    sprintf(buf, "%s-%02x:%02x:%02x:%02x:%02x:%02x", host, mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    ArduinoOTA.setHostname(buf);
    ArduinoOTA.onStart([]() {
        /* switch off all processes here!!!!! */
        Serial.println();
        Serial.println("Recieving new firmware now!");
    });
    ArduinoOTA.onEnd([]() {
        /* do stuff after update here!! */
        Serial.println("\nRecieving done!");
        Serial.println("Storing in memory and reboot!");
        Serial.println();
    });
    ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
        Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
    });
    ArduinoOTA.onError([](ota_error_t error) { ESP.restart(); });
    /* setup the OTA server */
    ArduinoOTA.begin();
    Serial.println("...done!");
}

/* ***************************************************************************
 */
/* MQTT setup */
/* ***************************************************************************
 */
void reconnect(){
    // Loop until we're reconnected
    char b[10];
    sprintf(b, "%02x%02x%02x%02x%02x%02x", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    while (!client.connected()){
        Serial.println("Attempting MQTT connection...");
        // Attempt to connect
        doc["idx_bel"] = idx_bel;
        doc["nvalue"] = 4;
        doc["svalue"] = "Offline";
        serializeJson(doc, msg);
        if (client.connect(b, mqtt_user, mqtt_password, "domoticz/in", 1, 1, msg)){ // login and send last will
            // ... and resubscribe
            client.subscribe("domoticz/out");
            Serial.println("Lucky loggin in");
            TckAck = 10;
        }else{
            Serial.print("failed, rc=");
            Serial.print(client.state());
            Serial.println(" try again in 5 seconds");
            // Wait 5 seconds before retrying
        delay(5000);
        }
    }
}

/* ***************************************************************************
 */
/* MQTT stuff comming in */
/* ***************************************************************************
 */
void callback(char *topic, byte *payload, int length){
    DeserializationError error = deserializeJson(doc, (char*)payload);
    if(error){
        TelnetStream.println("Error decoding JSON sting");
        Serial.println("Error decoding JSON sting");
        return;
    }
    int r_idx = doc["idx"];
    int r_nvalue = doc["nvalue"];
    if (strstr(topic, "domoticz/out") && r_idx == idx_sesam && r_nvalue ==1 ){ // command from domoticz open door downstairs
        sys_status = sys_open_start;
        Serial.println("Open door downstairs command detected");
        TelnetStream.println("Open door downstairs command detected");
    }
    if (strstr(topic, "domoticz/out") && r_idx == idx_display){ // command from domoticz open door downstairs
        if (r_nvalue ==1){
            Serial.println("Display on command detected");
            TelnetStream.println("Display on command detected");
            displayoff = 1000 * 60 * 60 * 4 + millis();  //turn dispay on for 4 hours
            bot.sendMessage(TelegramChatId, "Display on\n", "Markdown");
            digitalWrite(displayOn, true);
        }else{
            Serial.println("Display off command detected");
            TelnetStream.println("Display off command detected");
            displayoff = millis();
        }
    }
    if (r_idx == 999){
        const char *nvalue = doc["nvalue"];
        bot.sendMessage(TelegramChatId, nvalue, "Markdown");
        Serial.print("Message forwarded to telgram:");
        Serial.println(nvalue);
        TelnetStream.print("Message forwarded to telgram!");
    }
}

/*****************************************************************************/
// Telegram stuff
/*****************************************************************************/
void handleNewMessages(int numNewMessages) {
    Serial.print("Handle New Messages: ");
    Serial.println(numNewMessages);
    for (int i = 0; i < numNewMessages; i++) {
        String chat_id = String(bot.messages[i].chat_id);
        if (chat_id != CHAT_ID){
            bot.sendMessage(chat_id, "Unauthorized user", "");
            continue;
        }
        // Print the received message
        String text = bot.messages[i].text;
        Serial.println(text);
        String from_name = bot.messages[i].from_name;
        if (text == "/start") {
            String welcome = "Welcome , " + from_name + "\n";
            welcome += "Use the following commands to interact with the intercom \n";
            welcome += "/gallarydoor : opens the gallary door\n";
            welcome += "/frontdoor : opens the front door \n";
            welcome += "/ipadress : returns the local Ipaddress \n";
            bot.sendMessage(CHAT_ID, welcome, "");
        }
        if (text == "/gallarydoor") {
            sys_status = sys_open_start;
            Serial.println("Open gallary door now");
        }
        if (text == "/frontdoor") {
            Serial.println("Open front door now");
            sys_status = sys_open_front_door;
        }
        if (text == "/ipadress") {
            Serial.println("IP address: ");
            Serial.println(WiFi.localIP()); // You can get IP address assigned to ESP
            sys_status = sys_send_ipadress;
        }
    }
}

/*****************************************************************************/
// Blue led blink cycle
/*****************************************************************************/
void Blink(){
    if (TckCnt == 1){
        digitalWrite(BlueLed, BlueLedOn);
        //TelnetStream.print(millis()/1000 + "seconds online");
    }
    if (TckCnt == 2){
        digitalWrite(BlueLed, BlueLedOff);
    }
    if (TckCnt >= 11){
        TckCnt = 0;
    }
    TckCnt++;
}

void OpenDoor(void){
    digitalWrite(Video, 0);
    delay(250);
    digitalWrite(Key, 0);
    bot.sendMessage(TelegramChatId, "Galley door openend\n", "Markdown");
    TelnetStream.println("Galley door openend\n");
    delay(1500);
    digitalWrite(Video, 1);
    delay(1000);
    digitalWrite(Key, 1);
}

void RingDetect(void){
    Serial.println("Deurbel notification to MQTT");
    TelnetStream.println("Deurbel notification to MQTT");
    doc["idx"] = idx_bel;
    doc["nvalue"] = 1;
    serializeJson(doc, msg);
    client.publish("domoticz/in", msg);
    TelnetStream.println(msg);
    doc["idx"] = idx_txt;
    doc["svalue"] = "Deurbel";
    serializeJson(doc, msg);
    client.publish("domoticz/in", msg);
    TelnetStream.println(msg);
}

void SendDisplayOn(void){
    Serial.println("Dispay on notification to MQTT");
    TelnetStream.println("Dispay on notification to MQTT");
    doc["idx"] = idx_display;
    doc["nvalue"] = 1;
    serializeJson(doc, msg);
    client.publish("domoticz/in", msg);
    TelnetStream.println(msg);
}

void SendDisplayOff(void){
    Serial.println("Dispay off notification to MQTT");
    TelnetStream.println("Dispay off notification to MQTT");
    doc["idx"] = idx_display;
    doc["nvalue"] = 0;
    serializeJson(doc, msg);
    client.publish("domoticz/in", msg);
    TelnetStream.println(msg);
}

void array_to_string(char array[], unsigned int len, char buffer[]){
    for (unsigned int i = 0; i < len; i++){
        byte nib1 = (array[i] >> 4) & 0x0F;
        byte nib2 = (array[i] >> 0) & 0x0F;
        buffer[i * 2 + 0] = nib1 < 0xA ? '0' + nib1 : 'A' + nib1 - 0xA;
        buffer[i * 2 + 1] = nib2 < 0xA ? '0' + nib2 : 'A' + nib2 - 0xA;
    }
    buffer[len * 2] = '\0';
}

void SendSerialData(char *b, int leng){
    array_to_string(b, leng, msg);
    doc["idx"] = idx_txt;
    doc["svalue"] = msg;
    serializeJson(doc, msg);
    client.publish("domoticz/in", msg);
    TelnetStream.println(msg);
}

int BufComp(char buf1[], char buf2[], int t){
    int n;
    if (t > 4){ 
        return 0;
    }
    for (n = 0; n < t; n++){
        if (buf1[n] != buf2[n])
            return 0;
    }
    return 1;
}

void setup(){
    pinMode(Video, OUTPUT);
    pinMode(Key, OUTPUT);
    pinMode(displayOn, OUTPUT);
    digitalWrite(displayOn, 0);
    pinMode(BlueLed, OUTPUT);
    digitalWrite(Video, 1);
    digitalWrite(Key, 1);
    // put your setup code here, to run once:
    flipper.attach(1, Blink); // call blink eatch 1 sec
    Serial.begin(115200);
    swSer.begin(1200, SWSERIAL_8N1, D7, D4, false, 95, 11);
    delay(100);
    Serial.println("Booting...");
    WiFi.disconnect();
    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid, password);
    Serial.printf("[SETUP] BOOT WAIT ...");
    while (WiFi.status() != WL_CONNECTED)
    {
        delay(100);
        Serial.print(".");
    }
    WiFi.macAddress(mac);
    setup_OTA();
    sprintf(msg, "MAC adress: %02x%02x%02x%02x%02x%02x", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    Serial.print(msg);
    Serial.println();
    Serial.println("IP address: ");
    Serial.println(WiFi.localIP()); // You can get IP address assigned to ESP
    client.setServer(mqtt_server, 1883);
    client.setCallback(callback);
    reconnect();
    teleclient.setInsecure();
    bot.sendMessage(TelegramChatId, "Intercom rebooted...\n", "Markdown");
    TelnetStream.begin();
    Serial.println("READY...");
}

void loop(){
    ArduinoOTA.handle();
    if (!client.connected()){
        delay(500);
        reconnect();
    }else{
        client.loop();
    }
    if(sys_status == sys_send_ipadress){
        sys_status = 0;
        String ms = WiFi.localIP().toString();
        bot.sendMessage(TelegramChatId,"Ipaddress: " + ms, "Markdown");
        TelnetStream.println("Ipaddress: " + ms);
    }
    if(sys_status == sys_open_start){
        sys_status = sys_idle;
        flipper.detach(); // stop blink
        digitalWrite(BlueLed, BlueLedOn);
            if (millis() < belstamp + 100000){ // onley open door if the doorbel downstairs is pushed.
            OpenDoor();
            displayoff = 1000 * 60 * 2 + millis();  //turn dispay on for 2 minuts
        }else{
            bot.sendMessage(TelegramChatId, "Galley door prefend for opening!...\n", "Markdown");
            TelnetStream.println("Galley door prefend for opening!...");
            displayoff = 1000 * 60 * 2 + millis();  //turn dispay on for 2 minuts
        }
    digitalWrite(BlueLed, BlueLedOff);
    flipper.attach(1, Blink); // call blink eatch 1 sec
    }
    if(sys_status == sys_open_front_door){
        sys_status = sys_idle;
    }
    //Check for incomming serial data form intercom bus.
    while (swSer.available() > 0){
        timestamp = millis();
        int tel = 0;
        while (timestamp + 500 > millis()){
            while (swSer.available() > 0){
                inbuf[tel] = swSer.read();
                if (logbufcnt == 0){
                    logstamp = millis();
                }
                if (logbufcnt == buflen - 1){
                    logbufcnt = 0;
                }
                if (tel == buflen - 1){
                    tel = 0;
                }
                logbuf[logbufcnt++] = inbuf[tel];
                Serial.print(inbuf[tel], HEX);
                Serial.print(" ");
                yield();
                tel++;
            }
        }
        inbuf[tel] = 0; //add terminater
        //Decode serial data
        if (BufComp(inbuf, idbuf, tel) && tel == 4){
            bot.sendMessage(TelegramChatId, "Mijn deurbel!!!\n", "Markdown");
            TelnetStream.println("Mijn deurbel!!!");
            Serial.println("Mijn deurbel!!!");
            RingDetect();
            belstamp = millis();
            digitalWrite(displayOn, true);
            displayoff = 1000 * 60 * 5 + millis();  //turn dispay on for 5 minuts
            SendDisplayOn();
        }else if (BufComp(inbuf, idbuf, tel - 1) && tel == 4){
            bot.sendMessage(TelegramChatId, "Deurbel\n", "Markdown");
            TelnetStream.println("Deurbel!!!");
            Serial.println("Deurbel!!!");
            digitalWrite(displayOn, true);
            displayoff = 1000 * 60 * 2 + millis();  //turn dispay on for 1 minuts
            SendDisplayOn();
        }
    }
    //reset buffer after timeout
    if ((logstamp + 10000) < millis() && logbufcnt > 0){
        SendSerialData(logbuf, logbufcnt);
        logbufcnt = 0;
    }
    //turn display off after timeout
    if (digitalRead(displayOn) == true && displayoff < millis()){ //turn display off after timeout
        SendDisplayOff();
        digitalWrite(displayOn, false);
    }

    if (millis() > lastTimeBotRan + botRequestDelay)  {
        int numNewMessages = bot.getUpdates(bot.last_message_received + 1);
        while (numNewMessages) {
            Serial.println("got response");
            handleNewMessages(numNewMessages);
            numNewMessages = bot.getUpdates(bot.last_message_received + 1);
        }
    lastTimeBotRan = millis();
    }
}