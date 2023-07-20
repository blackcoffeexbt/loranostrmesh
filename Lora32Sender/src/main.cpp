#include <LoRa.h>
#include "boards.h"
#include "BluetoothSerial.h"
#include "WiFiClientSecure.h"
#include <NostrEvent.h>
#include "helpers.h"
#include "loranostrmesh.h"
// autoconnect
#include <WiFi.h>
#include <WiFiMulti.h>


#define RX_QUEUE_SIZE 1024

BluetoothSerial SerialBT;

NostrEvent nostr;

char const *nsecHex = "bdd19cecd942ed8964c2e0ddc92d5e09838d3a09ebb230d974868be00886704b";
char const *npubHex = "d0bfc94bd4324f7df2a7601c4177209828047c4d3904d64009a3c67fb5d5e7ca";

long timestamp = 0;

void logToSerialAndBT(String message) {
    Serial.println(message);
    SerialBT.println(message);
}

// define bluetooth pin
const char *btPin = "1234";

void setup()
{
    initBoard();
    // When the power is turned on, a delay is required.
    delay(1500);

    Serial.println("LoRa Sender");
    LoRa.setPins(RADIO_CS_PIN, RADIO_RST_PIN, RADIO_DIO0_PIN);
    if (!LoRa.begin(LoRa_frequency)) {
        Serial.println("Starting LoRa failed!");
        while (1);
    }

    LoRa.setTxPower(20); // 20 is max power!

    SerialBT.begin("loranostrmesh"); // Name of your Bluetooth Signal
    SerialBT.setPin(btPin); // Your Pin
    oledDisplay("Bluetooth ready");
    oledDisplay("Pin: " + String(btPin), 5, 40);

    Serial.println("Bluetooth Device is Ready to Pair");

    if (SerialBT.available()) {
        logToSerialAndBT("Type something in your phone app...");
    }

}

String lastTimeUpdate = "";

std::map<int, String> receivedMessageMap;
uint8_t totalParts = 0;
uint8_t lastPartNum = 0;

void loraReceive() {
    // try to parse packet
    int packetSize = LoRa.parsePacket();
    if (packetSize) {
        // received a packet
        String recv = "";
        // read packet
        while (LoRa.available()) {
            recv += (char)LoRa.read();
        }
        // lora messages in this network are all base64 encoded json docs
        String decodedRecv = base64Decode(recv);
        Serial.println("Received packet " + decodedRecv);
        // deserialize the packet
        DynamicJsonDocument doc(1024);

        decodeLoraPackage(&recv, &doc);

        // action depending on "type"
        if(doc["type"] == "TIME") {
            // set the time
            timestamp = doc["content"];
            // if timestamp is real (i.e. between two realisitic dates)
            if(timestamp > 1689673884 && timestamp < 4845347483) {
                logToSerialAndBT("Set timestamp to " + String(timestamp));
            }
        } else if(doc["type"] == "ACK") {
            // do nothing
            Serial.println("Received ACK");
        } else if(doc["type"] == "NOSTR_OK_EVENT") {
            logToSerialAndBT("Received Nostr OK event");
            logToSerialAndBT("Content: " + doc["content"].as<String>());
            oledDisplay("Received Nostr relay event: OK");
        } else if(doc["type"] == "NOSTR_SUB_KIND_4") {
            logToSerialAndBT("Received Nostr note" + doc["messagePart"].as<String>());
            oledDisplay("Received DM.");
            // Enter  the ack loop to receive the dm
            // now, set the receivedMessageMap element to the decoded json messagePart using currentPart index
            int currentPart = doc["currentPart"];
            String checksum = doc["checksum"].as<String>();
            Serial.println("Checksum is " + checksum);

            lastPartNum = currentPart;

            // if current part is less than the last part, clear the map ready for a new message
            if (currentPart < lastPartNum) {
                receivedMessageMap.clear();
            }

            // Serial.println("doc[\"checksum\"] is " + doc["checksum"]);
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
            String decryptedDm = nostr.decryptDm(nsecHex, decodedMessage);
            logToSerialAndBT("Decrypted DM: " + decryptedDm);
            
        } else {
            Serial.println("Received unknown type " + doc["type"].as<String>());
        }

        delay(20);

        // print RSSI of packet
        logToSerialAndBT("with RSSI " + LoRa.packetRssi());
    }
}

uint8_t currentPart = 0;

String message = "";

void ackCallback(DynamicJsonDocument* doc) {
    logToSerialAndBT("Received ACK for part " + String(doc->as<JsonObject>()["currentPart"].as<uint8_t>()) + " of " + String(doc->as<JsonObject>()["totalParts"].as<uint8_t>()));
    oledDisplay("ACK " + String(doc->as<JsonObject>()["currentPart"].as<uint8_t>()) + "/" + String(doc->as<JsonObject>()["totalParts"].as<uint8_t>()));
}

void handleBluetooth() {
    if (Serial.available()) {
        SerialBT.write(Serial.read());
    }
    if (SerialBT.available()) {
        // read bluetooth message to string and then send back to the user over bluetooth and output on Serial.
        message = SerialBT.readString();
        logToSerialAndBT("This is what you sent: " + message);
        oledDisplay("Broadcasting message...");
    }

    if(message == "") {
        return;
    }

    // unable to send if timestamp is not set
    if(timestamp == 0) {
        logToSerialAndBT("Timestamp not set, not broadcasting message. Please wait for timestamp and try again.");
        message = "";
        return;
    }
    
    // Create the nostr note
    nostr.setLogging(false);
    String note = nostr.getNote(nsecHex, npubHex, timestamp, message);
    message = "";

    broadcastNostrEvent(&note, &ackCallback);

    logToSerialAndBT("Packet sent");
}

long lastPubKeyBroadcast = 0;
/**
 * @brief broadcast this node's public key to the network so IGNs will subscribe to events for this node
 * 
 */
void broadcastPubKey() {
    // every 30 seconds, broadcast pubkey
    if(lastPubKeyBroadcast == 0 || millis() - lastPubKeyBroadcast > 30000) {
        logToSerialAndBT("Broadcasting pubkey");
        lastPubKeyBroadcast = millis();
        // create json doc with type PUBKEY, content is the pubkey
        DynamicJsonDocument doc(222);
        doc["type"] = "PUBKEY";
        doc["content"] = npubHex;
        // encode the package for transmission
        String loraPackage = "";
        encodeLoraPackage(&doc, &loraPackage);
        // broadcast the package
        LoRa.beginPacket();
        Serial.println("Sending packet " + loraPackage);
        LoRa.print(loraPackage);
        LoRa.endPacket();
    }
}


void loop()
{

    // if (Serial.available()) {
    //     SerialBT.write(Serial.read());
    // }
    // if (SerialBT.available()) {
    //     // read bluetooth message to string and then send back to the user over bluetooth and output on Serial.
    //     message = SerialBT.readString();
    //     Serial.println("This is what you sent: " + message);
    // }

    loraReceive();
    handleBluetooth();
    broadcastPubKey();
}
