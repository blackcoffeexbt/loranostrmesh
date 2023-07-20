#include <Arduino.h>
#include <LoRa.h>
#include "boards.h"
#include "WiFiClientSecure.h"
#include "time.h"
#include <NostrEvent.h>
#include <NostrRelayManager.h>
#include "loranostrmesh.h"

#include <WebServer.h>
#include <AutoConnect.h>

WebServer Server;

WebServer server;
AutoConnect portal(server);
AutoConnectConfig config;
String apPassword = "ToTheMoon1"; //default WiFi AP password

NostrEvent nostr;
NostrRelayManager nostrRelayManager;
NostrQueueProcessor nostrQueue;

bool hasSentEvent = false;

// NTP server to request epoch time
const char* ntpServer = "0.uk.pool.ntp.org";
const long  gmtOffset_sec = 0;
const int   daylightOffset_sec = 3600;

char const *nsecHex = "bdd19cecd942ed8964c2e0ddc92d5e09838d3a09ebb230d974868be00886704b";
char const *npubHex = "d0bfc94bd4324f7df2a7601c4177209828047c4d3904d64009a3c67fb5d5e7ca";

long bootTimestamp = 0;
long bootMillis = 0;

bool whileCP(void);
void initWiFi();
void configureAccessPoint();

unsigned long getUnixTimestamp() {
  time_t now;
  struct tm timeinfo;
  if(!getLocalTime(&timeinfo)){
    Serial.println("Failed to obtain time");
    return 0;
  } else {
    Serial.println("Got timestamp of " + String(now));
  }
  time(&now);
  return now;
}

void okEvent(const std::string& key, const char* payload) {
    Serial.println("OK event");
    Serial.println("payload is: ");
    Serial.println(payload);
    // create an ok event json doc
    DynamicJsonDocument doc(222);
    doc["type"] = "NOSTR_OK_EVENT";
    doc["content"] = String(payload);
    // encode
    String serialisedMessage = "";
    encodeLoraPackage(&doc, &serialisedMessage);
    doc.clear();
    // broadcast on lora
    LoRa.beginPacket();
    LoRa.print(serialisedMessage);
    LoRa.endPacket();
}

void nip01Event(const std::string& key, const char* payload) {
    Serial.println("NIP01 event");
    Serial.println("payload is: ");
    Serial.println(payload);
    // writeToDisplay("NIP01");
    delay(1000);
    // writeToDisplay(payload);
}

void nip04Event(const std::string& key, const char* payload) {
    Serial.println("NIP04 event");
    // broadcase nip4 to lora
    String serialisedMessage = String(payload);
    broadcastNostrEvent(&serialisedMessage, NULL, "NOSTR_SUB_KIND_4");
}

void setup()
{
    initBoard();

    initWiFi();

    configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);

    bootTimestamp = getUnixTimestamp();
    bootMillis = millis();

    const char *const relays[] = {
        // "relay.damus.io",
        // "nostr.mom",
        "relay.nostr.bg"
    };
    int relayCount = sizeof(relays) / sizeof(relays[0]);
    
    nostr.setLogging(false);
    nostrRelayManager.setRelays(relays, relayCount);
    nostrRelayManager.setMinRelaysAndTimeout(2,10000);

    // Set some event specific callbacks here
    nostrRelayManager.setEventCallback("ok", okEvent);
    nostrRelayManager.setEventCallback("nip01", nip01Event);
    nostrRelayManager.setEventCallback("nip04", nip04Event);
    nostrRelayManager.connect();

    // When the power is turned on, a delay is required.
    delay(1500);

    Serial.println("LoRa Receiver");

    LoRa.setPins(RADIO_CS_PIN, RADIO_RST_PIN, RADIO_DIO0_PIN);
    if (!LoRa.begin(LoRa_frequency)) {
        Serial.println("Starting LoRa failed!");
        while (1);
    }
}

long lastReceiveTime = 0;

String getSecsSinceLastReceive()
{
    long now = millis();
    long diff = now - lastReceiveTime;
    return String(diff / 1000);
}

/** current timestamp is boot timestamp plus seconds delta of bootmillis and current millis */
long getTimestampUsingBootMillis()
{
    long now = millis();
    long diff = now - bootMillis;
    return bootTimestamp + (diff / 1000);
}

long lastBroadcastTime = 0;

/** broadcast timestamp millis every 10 seconds */
void broadcastTimestampOnLoRa()
{
    long now = millis();
    long diff = now - lastBroadcastTime;
    
    if (diff > 10000) {
        // Sometimes the timestamp might not have been set correctly on boot, try and get it again
        if(bootTimestamp < 1689618583) {
            bootTimestamp = getUnixTimestamp();
        }
        lastBroadcastTime = now;
        long timestamp = getTimestampUsingBootMillis();
        // construct a json doc with type = TIME and message = timestamp
        DynamicJsonDocument doc(1024);
        doc["type"] = "TIME";
        doc["content"] = timestamp;
        // serialise and base64 encode the doc
        String serialisedMessage = "";
        serializeJson(doc, serialisedMessage);
        Serial.println("Serialised message: " + serialisedMessage);
        doc.clear();
        String encodedMessage = base64Encode(serialisedMessage);

        LoRa.beginPacket();
        LoRa.print(encodedMessage);
        LoRa.endPacket();

#ifdef HAS_DISPLAY
        if (u8g2) {
            String timestampStr = String(timestamp);
            u8g2->clearBuffer();
            char buf[256];
            u8g2->drawStr(0, 10, "Timestamp broadcast");
            u8g2->drawStr(0, 20, timestampStr.c_str());
            u8g2->sendBuffer();
        }
#endif
    }
}

String lastTimeUpdate = "";

std::map<int, String> receivedMessageMap;
uint8_t totalParts = 0;
uint8_t lastPartNum = 0;

// a vector for storing node pubkeys
std::vector<String> nodePubkeys;

void loop()
{
    broadcastTimestampOnLoRa();
    // try to parse packet
    int packetSize = LoRa.parsePacket();
    if (packetSize) {
      lastReceiveTime = millis();
      // received a packet
      Serial.print("Received packet '");

      String recv = "";
      // read packet
      while (LoRa.available()) {
          recv += (char)LoRa.read();
      }

      Serial.println(recv);

      // the recv package will be a base64 encoded dynamicjson array, base64 decode and deserialise the json
      String decoded = base64Decode(recv);
      Serial.println("Decoded: " + decoded);
      DynamicJsonDocument doc(1024);
      deserializeJson(doc, decoded);

      if(doc["type"] == "NOSTR_NOTE_TX_REQ") {
        
        // now, set the receivedMessageMap element to the decoded json messagePart using currentPart index
        int currentPart = doc["currentPart"];

        lastPartNum = currentPart;

        // if current part is less than the last part, clear the map ready for a new message
        if (currentPart < lastPartNum) {
            receivedMessageMap.clear();
        }

        String checksum = doc["checksum"].as<String>();
        totalParts = doc["totalParts"];
        receivedMessageMap[currentPart - 1] = doc["messagePart"].as<String>(); // currentPart - 1 to make it 0 indexed for storage in the map
        // destroy the doc
        doc.clear();

        // use the doc for the ACK message
        doc["type"] = "ACK";
        doc["currentPart"] = currentPart;
        doc["totalParts"] = totalParts;
        doc["checksum"] = checksum;

        // serialise and base64 encode the doc
        String serialisedMessage = "";
        serializeJson(doc, serialisedMessage);
        Serial.println("Serialised ACK message: " + serialisedMessage);
        doc.clear();
        String encodedMessage = base64Encode(serialisedMessage);

        // send the ACK
        LoRa.beginPacket();
        LoRa.print(encodedMessage.c_str());
        LoRa.endPacket();

        Serial.println("Total parts: " + String(totalParts));
        // check if we have all the parts
        if (totalParts > 0 && currentPart < totalParts) {
            Serial.println("Not all parts received yet, waiting for more");
            return;
        }

        Serial.println("All parts received, joining message");
        // join all the parts together
        String joinedMessage = "";
        for (int i = 0; i < receivedMessageMap.size(); i++) {
            joinedMessage += receivedMessageMap[i];
        }
        Serial.println("Joined message: " + joinedMessage);

        // clear the receivedMessageMap and reset counters
        receivedMessageMap.clear();
        totalParts = 0;
        currentPart = 0;
        lastPartNum = 0;

        // base64 decode the joined message
        String decodedMessage = base64Decode(joinedMessage);
        Serial.println("Decoded message: " + decodedMessage);
        // send it over nostr
        nostrRelayManager.enqueueMessage(decodedMessage.c_str());
      }
      else if(doc["type"] == "PUBKEY") {
          // if doc["content"] exists in nodePubkeys, do nothing, otherwise add it then subscribe to kind 4
          // messages for the pubkey
          String pubkey = doc["content"].as<String>();
          if(std::find(nodePubkeys.begin(), nodePubkeys.end(), pubkey) != nodePubkeys.end()) {
              // do nothing
          } else {
              Serial.println("New pubkey received: " + pubkey + " subscribing to kind 4 messages");
              // add the pubkey to the nodePubkeys vector
              nodePubkeys.push_back(pubkey);
              // subscribe to kind 4 messages for the pubkey
              NostrRequestOptions* eventRequestOptions = new NostrRequestOptions();

              // Populate kinds
              int kinds[] = {4};
              eventRequestOptions->kinds = kinds;
              eventRequestOptions->kinds_count = sizeof(kinds) / sizeof(kinds[0]);

              // Populate #p
              String p[] = {pubkey};
              eventRequestOptions->p = p;
              eventRequestOptions->p_count = sizeof(p) / sizeof(p[0]);

              // Populate other fields
              eventRequestOptions->since = getTimestampUsingBootMillis();
              eventRequestOptions->limit = 1;
              nostrRelayManager.requestEvents(eventRequestOptions);
              delete eventRequestOptions;
          }
      }
    }

  nostrRelayManager.loop();
  nostrRelayManager.broadcastEvents();
}


void initWiFi() {
  configureAccessPoint();
    
  WiFi.mode(WIFI_STA);
  while (WiFi.status() != WL_CONNECTED) {
    delay(3000);
  }
}

/**
 * @brief Configure the WiFi AP
 * 
 */
void configureAccessPoint() {
  // handle access point traffic
  server.on("/", []() {
    String content = "<h1>Gerty</br>Your Bitcoin Assistant</h1>";
    content += AUTOCONNECT_LINK(COG_24);
    server.send(200, "text/html", content);
  });

  config.autoReconnect = true;
  config.reconnectInterval = 1; // 30s
  config.beginTimeout = 30000UL;

  config.hostName = "NostrLoRaMeshReceiver";
  config.apid = "NostrLoRaMeshReceiver-" + String((uint32_t)ESP.getEfuseMac(), HEX);
  config.apip = IPAddress(6, 15, 6, 15);      // Sets SoftAP IP address
  config.gateway = IPAddress(6, 15, 6, 15);     // Sets WLAN router IP address
  config.psk = apPassword;
  config.menuItems = AC_MENUITEM_CONFIGNEW | AC_MENUITEM_OPENSSIDS | AC_MENUITEM_RESET;
  config.title = "NostrLoRaMeshReceiver";
  config.portalTimeout = 120000;

  portal.whileCaptivePortal(whileCP);

  portal.join({});
  portal.config(config);

    // Establish a connection with an autoReconnect option.
  if (portal.begin()) {
    Serial.println("WiFi connected: " + WiFi.localIP().toString());
  }
}

/**
 * @brief While captive portal callback
 * 
 * @return true 
 * @return false 
 */
bool whileCP(void) {
// u8g2 screeen show in CP mode
#ifdef HAS_DISPLAY
    if (u8g2) {
        u8g2->clearBuffer();
        u8g2->drawStr(0, 10, "AP config mode");
        u8g2->drawStr(0, 20, WiFi.softAPIP().toString().c_str());
        u8g2->sendBuffer();
    }
#endif
  return true;
}