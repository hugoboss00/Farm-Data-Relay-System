//  FARM DATA RELAY SYSTEM
//
//  GATEWAY Main Functions
//  Developed by Timm Bogner (timmbogner@gmail.com)

#ifndef __FDRS_FUNCTIONS_H__
#define __FDRS_FUNCTIONS_H__
#include "fdrs_datatypes.h"
#include "fdrs_globals.h"

enum
{
  event_clear,
  event_espnowg,
  event_espnow1,
  event_espnow2,
  event_serial,
  event_mqtt,
  event_lorag,
  event_lora1,
  event_lora2
};


// void debug_OLED(String debug_text);



SystemPacket theCmd;
DataReading theData[256];
uint8_t ln;
uint8_t newData = event_clear;
uint8_t newCmd = cmd_clear;
bool is_ping = false;

#include "fdrs_gateway_oled.h"
#include "fdrs_gateway_debug.h"
#include "fdrs_gateway_espnow.h"
#include "fdrs_gateway_lora.h"
#include "fdrs_gateway_wifi.h"
#include "fdrs_gateway_filesystem.h"
#include "fdrs_gateway_mqtt.h"
#include "fdrs_gateway_serial.h"

#ifdef DEBUG_CONFIG
#include "fdrs_checkConfig.h"
#endif
int getFDRSPeer(uint8_t *mac)
{ // Returns the index of the array element that contains the provided MAC address
  DBG("Getting peer #");

  for (int i = 0; i < 16; i++)
  {
    if (memcmp(mac, &peer_list[i].mac, 6) == 0)
    {
      DBG("Peer is entry #" + String(i));
      return i;
    }
  }

  // DBG("Couldn't find peer");
  return -1;
}
int findOpenPeer()
{ // Returns an expired entry in peer_list, -1 if full.
  // uint8_t zero_addr[] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
  for (int i = 0; i < 16; i++)
  {
    if (peer_list[i].last_seen == 0)
    {
      DBG("Using peer entry " + String(i));
      return i;
    }
  }
  for (int i = 0; i < 16; i++)
  {
    if ((millis() - peer_list[i].last_seen) >= PEER_TIMEOUT)
    {
      DBG("Recycling peer entry " + String(i));
      esp_now_del_peer(peer_list[i].mac);

      return i;
    }
  }
  DBG("No open peers");
  return -1;
}

void handleCommands()
{
  switch (theCmd.cmd)
  {
  case cmd_ping:
    DBG("Ping back to sender");
    SystemPacket sys_packet;
    sys_packet.cmd = cmd_ping;
    if (!esp_now_is_peer_exist(incMAC))
    {
#ifdef ESP8266
      esp_now_add_peer(incMAC, ESP_NOW_ROLE_COMBO, 0, NULL, 0);
#endif
#if defined(ESP32)
      esp_now_peer_info_t peerInfo;
      peerInfo.ifidx = WIFI_IF_STA;
      peerInfo.channel = 0;
      peerInfo.encrypt = false;
      memcpy(peerInfo.peer_addr, incMAC, 6);
      if (esp_now_add_peer(&peerInfo) != ESP_OK)
      {
        DBG("Failed to add peer");
        return;
      }
#endif
      esp_now_send(incMAC, (uint8_t *)&sys_packet, sizeof(SystemPacket));
      esp_now_del_peer(incMAC);
    }
    else
    {
      esp_now_send(incMAC, (uint8_t *)&sys_packet, sizeof(SystemPacket));
    }
    break;

  case cmd_add:
    DBG("Device requesting peer registration");
    int peer_num = getFDRSPeer(&incMAC[0]);
    if (peer_num == -1)
    { // if the device isn't registered
      DBG("Device not yet registered, adding to internal peer list");
      int open_peer = findOpenPeer();                // find open spot in peer_list
      memcpy(&peer_list[open_peer].mac, &incMAC, 6); // save MAC to open spot
      peer_list[open_peer].last_seen = millis();
#if defined(ESP32)
      esp_now_peer_info_t peerInfo;
      peerInfo.ifidx = WIFI_IF_STA;
      peerInfo.channel = 0;
      peerInfo.encrypt = false;
      memcpy(peerInfo.peer_addr, incMAC, 6);
      if (esp_now_add_peer(&peerInfo) != ESP_OK)
      {
        DBG("Failed to add peer");
        return;
      }
#endif
#if defined(ESP8266)
      esp_now_add_peer(incMAC, ESP_NOW_ROLE_COMBO, 0, NULL, 0);

#endif
      SystemPacket sys_packet = {.cmd = cmd_add, .param = PEER_TIMEOUT};
      esp_now_send(incMAC, (uint8_t *)&sys_packet, sizeof(SystemPacket));
    }
    else
    {
      DBG("Refreshing existing peer registration");
      peer_list[peer_num].last_seen = millis();

      SystemPacket sys_packet = {.cmd = cmd_add, .param = PEER_TIMEOUT};
      esp_now_send(incMAC, (uint8_t *)&sys_packet, sizeof(SystemPacket));
    }
    break;
  }

  theCmd.cmd = cmd_clear;
  theCmd.param = 0;
}
void beginFDRS()
{
#if defined(ESP8266)
  Serial.begin(115200);
#elif defined(ESP32)
  Serial.begin(115200);
  UART_IF.begin(115200, SERIAL_8N1, RXD2, TXD2);
#endif
#ifdef USE_OLED
  pinMode(OLED_RST, OUTPUT);
  digitalWrite(OLED_RST, LOW);
  delay(30);
  digitalWrite(OLED_RST, HIGH);
  Wire.begin(OLED_SDA, OLED_SCL);
  display.init();
  display.flipScreenVertically();
  draw_OLED_header();
  DBG("Display initialized!")
  DBG("Hello, World!")

#endif

  DBG("Address:" + String(UNIT_MAC, HEX));

#ifdef USE_LED
  FastLED.addLeds<WS2812B, LED_PIN, GRB>(leds, NUM_LEDS);
  leds[0] = CRGB::Blue;
  FastLED.show();
#endif
#ifdef USE_LORA
  begin_lora();
#endif
#ifdef USE_WIFI
  delay(10);
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED)
  {
    DBG("Connecting to WiFi...");
    DBG(FDRS_WIFI_SSID);

    delay(500);
  }
  DBG("WiFi Connected");
  client.setServer(mqtt_server, mqtt_port);
  if (!client.connected())
  {
    reconnect(5);
  }
  client.setCallback(mqtt_callback);
#else
  begin_espnow();
#endif

#ifdef USE_SD_LOG
  begin_SD();
#endif
#ifdef USE_FS_LOG
  begin_FS();
#endif

  // DBG(sizeof(DataReading));
#ifdef USE_WIFI
  client.publish(TOPIC_STATUS, "FDRS initialized");
#endif
}

void loopFDRS()
{
  handleCommands();
#ifdef ESPNOWG_DELAY
  if ((millis() - timeESPNOWG) >= ESPNOWG_DELAY)
  {
    timeESPNOWG = millis();
    if (lenESPNOWG > 0)
      releaseESPNOW(0);
  }
#endif
#ifdef ESPNOW1_DELAY
  if ((millis() - timeESPNOW1) >= ESPNOW1_DELAY)
  {
    timeESPNOW1 = millis();
    if (lenESPNOW1 > 0)
      releaseESPNOW(1);
  }
#endif
#ifdef ESPNOW2_DELAY
  if ((millis() - timeESPNOW2) >= ESPNOW2_DELAY)
  {
    timeESPNOW2 = millis();
    if (lenESPNOW2 > 0)
      releaseESPNOW(2);
  }
#endif
#ifdef SERIAL_DELAY
  if ((millis() - timeSERIAL) >= SERIAL_DELAY)
  {
    timeSERIAL = millis();
    if (lenSERIAL > 0)
      releaseSerial();
  }
#endif
#ifdef MQTT_DELAY
  if ((millis() - timeMQTT) >= MQTT_DELAY)
  {
    timeMQTT = millis();
    if (lenMQTT > 0)
      releaseMQTT();
  }
#endif
#ifdef LORAG_DELAY
  if ((millis() - timeLORAG) >= LORAG_DELAY)
  {
    timeLORAG = millis();
    if (lenLORAG > 0)
      releaseLoRa(0);
  }
#endif
#ifdef LORA1_DELAY
  if ((millis() - timeLORA1) >= LORA1_DELAY)
  {
    timeLORA1 = millis();
    if (lenLORA1 > 0)
      releaseLoRa(1);
  }
#endif
#ifdef LORA2_DELAY
  if ((millis() - timeLORA2) >= LORA2_DELAY)
  {
    timeLORA2 = millis();
    if (lenLORA2 > 0)
      releaseLoRa(2);
  }
#endif
#if defined(USE_SD_LOG) || defined(USE_FS_LOG)
  if ((millis() - timeLOGBUF) >= LOGBUF_DELAY)
  {
    timeLOGBUF = millis();
    if (logBufferPos > 0)
      releaseLogBuffer();
  }
#endif

  while (UART_IF.available() || Serial.available())
  {
    getSerial();
  }
  handleLoRa();
#ifdef USE_WIFI
  if (!client.connected())
  {
    reconnect(1, true);
  }
  client.loop(); // for recieving incoming messages and maintaining connection

#endif
  if (newData != event_clear)
  {
    switch (newData)
    {
    case event_espnowg:
      ESPNOWG_ACT
      break;
    case event_espnow1:
      ESPNOW1_ACT
      break;
    case event_espnow2:
      ESPNOW2_ACT
      break;
    case event_serial:
      SERIAL_ACT
      break;
    case event_mqtt:
      MQTT_ACT
      break;
    case event_lorag:
      LORAG_ACT
      break;
    case event_lora1:
      LORA1_ACT
      break;
    case event_lora2:
      LORA2_ACT
      break;
    }
    newData = event_clear;
  }
}
#endif //__FDRS_FUNCTIONS_H__
