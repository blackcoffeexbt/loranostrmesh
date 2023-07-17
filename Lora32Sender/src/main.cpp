#include <LoRa.h>
#include "boards.h"
#include "BluetoothSerial.h"
#include "WiFiClientSecure.h"
#include <NostrEvent.h>

#include "loranostrmesh.h"

BluetoothSerial SerialBT;

NostrEvent nostr;

char const *nsecHex = "bdd19cecd942ed8964c2e0ddc92d5e09838d3a09ebb230d974868be00886704b";
char const *npubHex = "d0bfc94bd4324f7df2a7601c4177209828047c4d3904d64009a3c67fb5d5e7ca";

long timestamp = 0;

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

    SerialBT.begin("loranostrmesh"); // Name of your Bluetooth Signal
    Serial.println("Bluetooth Device is Ready to Pair");

    if (SerialBT.available()) {
        SerialBT.println("Type something in your phone app...");
    }

    // Create the nostr note
    String testMessage = "I am a larger data package I am a larger data package I am a larger data package I am a larger data package";
    nostr.setLogging(false);
    String note = nostr.getNote(nsecHex, npubHex, timestamp, testMessage);
    std::string encodedNote = base64Encode(note).c_str();

    // split the message into parts
    size_t max_lora_packet_size = 100; // max lora byte size?
    std::vector<std::string> parts;
    split_string_into_parts(&encodedNote, max_lora_packet_size, &parts);

    broadcastMessage(&parts);

    // for(const auto& part : parts) {
    //     Serial.println(part.c_str());
    // }

    // Serial.println("Note: " + note);

}

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
        Serial.println("Received packet " + recv);
        SerialBT.println("Received packet " + recv);
        delay(20);

        // print RSSI of packet
        Serial.print("' with RSSI ");
        Serial.println(LoRa.packetRssi());
        SerialBT.println("with RSSI " + LoRa.packetRssi());
    }
}

uint8_t currentPart = 0;

String message = "";

void handleBluetooth() {
    if (Serial.available()) {
        SerialBT.write(Serial.read());
    }
    if (SerialBT.available()) {
        // read bluetooth message to string and then send back to the user over bluetooth and output on Serial.
        message = SerialBT.readString();
        SerialBT.println("This is what you sent: " + message);
        Serial.println("This is what you sent: " + message);
    }

    if(message == "") {
        return;
    }
    
    Serial.println("Sending message: " + message);
    SerialBT.println("Sending message: " + message);

    // send packet
    LoRa.beginPacket();
    LoRa.print(message.c_str());
    LoRa.endPacket();
    message = "";

    SerialBT.println("Packet sent");
}


void loop()
{

    loraReceive();

    handleBluetooth();

    

#ifdef HAS_DISPLAY
    if (u8g2) {
        char buf[256];
        u8g2->clearBuffer();
        u8g2->drawStr(0, 12, "Transmitting: OK!");
        snprintf(buf, sizeof(buf), "Sending: %d", message);
        u8g2->drawStr(0, 30, buf);
        u8g2->sendBuffer();
    }
#endif
}
