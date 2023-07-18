#include <Arduino.h>
#include <LoRa.h>
#include "boards.h"
#include "WiFiClientSecure.h"
#include "time.h"
#include <NostrEvent.h>
#include <NostrRelayManager.h>
#include "loranostrmesh.h"

const char* ssid     = "PLUSNET-MWC9Q2"; // wifi SSID here
const char* password = "4NyMeXtNcQ6rqP"; // wifi password here

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
    doc["type"] = "NOSTR_EVENT";
    doc["event"] = "OK";
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

void setup()
{
    initBoard();

    WiFi.begin(ssid, password);

    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
    }
    Serial.println("");
    Serial.println("WiFi connected");
    Serial.println("IP address: ");
    Serial.println(WiFi.localIP());

    configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);

    bootTimestamp = getUnixTimestamp();
    bootMillis = millis();

    const char *const relays[] = {
        "relay.damus.io",
        "nostr.mom",
        "relay.nostr.bg"
    };
    int relayCount = sizeof(relays) / sizeof(relays[0]);
    
    nostr.setLogging(false);
    nostrRelayManager.setRelays(relays, relayCount);
    nostrRelayManager.setMinRelaysAndTimeout(2,10000);

    // Set some event specific callbacks here
    nostrRelayManager.setEventCallback("ok", okEvent);
    nostrRelayManager.setEventCallback("nip01", nip01Event);
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
        // now, set the receivedMessageMap element to the decoded json messagePart using currentPart index
        int currentPart = doc["currentPart"];

        // if(lastPartNum > 0 && currentPart != lastPartNum + 1) {
        //     Serial.println("Missing part, clearing map");
        //     receivedMessageMap.clear();
        // }

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

        // print RSSI of packet
        // Serial.print("' with RSSI ");
        // Serial.println(LoRa.packetRssi());

        // long timestamp = getUnixTimestamp();
        // String note = nostr.getNote(nsecHex, npubHex, timestamp, recv.c_str());
        // Serial.println("Sending note to nostr" + note);
        // nostrRelayManager.enqueueMessage(note.c_str());

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

#ifdef HAS_DISPLAY
        if (u8g2) {
            u8g2->clearBuffer();
            char buf[256];
            u8g2->drawStr(0, 10, "Received OK!");
            u8g2->drawStr(0, 20, recv.c_str());
            snprintf(buf, sizeof(buf), "RSSI:%i", LoRa.packetRssi());
            u8g2->drawStr(0, 30, buf);
            snprintf(buf, sizeof(buf), "SNR:%.1f", LoRa.packetSnr());
            u8g2->drawStr(0, 40, buf);
            u8g2->sendBuffer();
        }
#endif


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

  nostrRelayManager.loop();
  nostrRelayManager.broadcastEvents();
}
