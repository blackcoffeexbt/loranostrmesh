#include <Arduino.h>
#include <LoRa.h>
#include "boards.h"
#include "WiFiClientSecure.h"
#include "time.h"
#include <NostrEvent.h>
#include <NostrRelayManager.h>

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

long bootMillis = 0;
long bootTimestamp = 0;

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
    // writeToDisplay("OK event");
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

    bootMillis = millis();
    bootTimestamp = getUnixTimestamp();


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
        lastBroadcastTime = now;
        long timestamp = getTimestampUsingBootMillis();
        String timestampStr = "TIMESTAMP|" + String(timestamp);
        Serial.println("Broadcasting " + timestampStr);
        LoRa.beginPacket();
        LoRa.print(timestampStr);
        LoRa.endPacket();

#ifdef HAS_DISPLAY
        if (u8g2) {
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
        String completeMessage = "";
        String currentPacket = "";
        String endOfPacketMarker = "<EOP>";

        while (LoRa.available()) {
            char incoming = (char)LoRa.read();
            if (incoming == '<') { // start of the end of packet marker
                currentPacket += incoming;

                // read the next 4 characters
                for (int i = 0; i < 4; i++) {
                if (LoRa.available()) {
                    currentPacket += (char)LoRa.read();
                }
                }

                // check if we've read the end of packet marker
                if (currentPacket.endsWith(endOfPacketMarker)) {
                // remove the end of packet marker
                currentPacket.replace(endOfPacketMarker, "");
                completeMessage += currentPacket;
                currentPacket = "";
                }
            } else {
                currentPacket += incoming;
            }
        }

        // Now `completeMessage` contains the full message
        Serial.println(completeMessage);

        recv = completeMessage;

        Serial.println(recv);

        // print RSSI of packet
        Serial.print("' with RSSI ");
        Serial.println(LoRa.packetRssi());

        // long timestamp = getUnixTimestamp();
        // String note = nostr.getNote(nsecHex, npubHex, timestamp, recv.c_str());
        Serial.println("Sending note to nostr" + recv);
        nostrRelayManager.enqueueMessage(recv.c_str());

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
    }

  nostrRelayManager.loop();
  nostrRelayManager.broadcastEvents();
}
