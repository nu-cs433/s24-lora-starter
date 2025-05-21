#include <Arduino.h>
#include <RadioLib.h>
#include <SPI.h>


/*~~~~~Hardware Definitions~~~~~*/

// These are hardware specific to the Heltec WiFi LoRa 32 V3
// Cite: https://resource.heltec.cn/download/WiFi_LoRa32_V3/HTIT-WB32LA(F)_V3_Schematic_Diagram.pdf
#define PRG_BUTTON 0
#define LORA_NSS_PIN 8
#define LORA_SCK_PIN 9
#define LORA_MOSI_PIN 10
#define LORA_MISO_PIN 11
#define LORA_RST_PIN 12
#define LORA_BUSY_PIN 13
#define LORA_DIO1_PIN 14


/*~~~~~Radio Configuration~~~~~*/

// Initialize SX1262 radio
// Make a custom SPI device because *of course* Heltec didn't use the default SPI pins
SPIClass spi(FSPI);
SPISettings spiSettings(2000000, MSBFIRST, SPI_MODE0); // Defaults, works fine
SX1262 radio = new Module(LORA_NSS_PIN, LORA_DIO1_PIN, LORA_RST_PIN, LORA_BUSY_PIN, spi, spiSettings);


/*~~~~~Function Prototypes~~~~~*/
void error_message(const char* message, int16_t state);


/*~~~~~Interrupt Handlers~~~~~*/
volatile bool receivedFlag = false;
volatile bool buttonFlag = false;

// This function should be called when a complete packet is received.
//  It is placed in RAM to avoid Flash usage errors
#if defined(ESP8266) || defined(ESP32)
  ICACHE_RAM_ATTR
#endif
void receiveISR(void) {
  // WARNING:  No Flash memory may be accessed from the IRQ handler: https://stackoverflow.com/a/58131720
  //  So don't call any functions or really do anything except change the flag
  receivedFlag = true;
}

// This function should be called when a complete packet is received.
//  It is placed in RAM to avoid Flash usage errors
#if defined(ESP8266) || defined(ESP32)
  ICACHE_RAM_ATTR
#endif
void buttonISR(void) {
  // WARNING:  No Flash memory may be accessed from the IRQ handler: https://stackoverflow.com/a/58131720
  //  So don't call any functions or really do anything except change the flag
  buttonFlag = true;
}


/*~~~~~Helper Functions~~~~~*/
void error_message(const char* message, int16_t state) {
  Serial.printf("ERROR!!! %s with error code %d\n", message, state);
  while(true); // loop forever
}


/*~~~~~Application~~~~~*/
void setup() {
  Serial.begin(115200);

  // Set up GPIO pin for "PRG" button and enable interrupts for it
  pinMode(PRG_BUTTON, INPUT);
  attachInterrupt(PRG_BUTTON, buttonISR, FALLING);

  // Set up SPI with our specific pins 
  spi.begin(LORA_SCK_PIN, LORA_MISO_PIN, LORA_MOSI_PIN, LORA_NSS_PIN);

  // TODO: Update this configuration
  // When setting up the radio, you can configure the parameters below:
  // carrier frequency:           TODO: pick frequency based on your channel
  // bandwidth:                   TODO: pick bandwidth based on your channel
  // spreading factor:            TODO: pick a SF based on your data rate
  // coding rate:                 5 (CR 4/5 for LoRaWAN)
  // sync word:                   0x34 (LoRaWAN sync word)
  // output power:                0 dBm
  // preamble length:             8 symbols (LoRaWAN preamble length)
  Serial.print("Initializing radio...");
  int16_t state = radio.begin(TODO_FREQ, TODO_BW, TODO_SF, 5, 0x34, 0, 8);
  if (state != RADIOLIB_ERR_NONE) {
      error_message("Radio initializion failed", state);
  }

  // Current limit of 140 mA (max)
  state = radio.setCurrentLimit(140.0);
  if (state != RADIOLIB_ERR_NONE) {
      error_message("Current limit intialization failed", state);
  }

  // Hardware uses DIO2 on the SX1262 as an RF switch
  state = radio.setDio2AsRfSwitch(true);
  if (state != RADIOLIB_ERR_NONE) {
      error_message("DIO2 as RF switch intialization failed", state);
  }

  // LoRa explicit header mode is used for LoRaWAN
  state = radio.explicitHeader();
  if (state != RADIOLIB_ERR_NONE) {
      error_message("Explicit header intialization failed", state);
  }

  // LoRaWAN uses a two-byte CRC
  state = radio.setCRC(2);
  if (state != RADIOLIB_ERR_NONE) {
      error_message("CRC intialization failed", state);
  }
  Serial.println("Complete!");

  // set the function that will be called when a new packet is received
  radio.setDio1Action(receiveISR);

  // start continuous reception
  Serial.print("Beginning continuous reception...");
  state = radio.startReceive();
  if (state != RADIOLIB_ERR_NONE) {
    error_message("Starting reception failed", state);
  }
  Serial.println("Complete!");
}

void loop() {

  // Handle packet receptions
  if (receivedFlag) {
    receivedFlag = false;

    // you can receive data as an Arduino String
    String packet_data;
    int state = radio.readData(packet_data);

    if (state == RADIOLIB_ERR_NONE) {
      // packet was successfully received
      Serial.println("Received packet!");

      // print the data of the packet
      Serial.print("[SX1262] Data:  ");
      Serial.println(packet_data);
      Serial.print("\t[");
      const char* data = packet_data.c_str();
      for (int i = 0; i < packet_data.length(); i++) {
        Serial.printf("%02X ", data[i]);
      }
      Serial.println("]");

      // print the RSSI (Received Signal Strength Indicator)
      // of the last received packet
      Serial.print("\tRSSI:\t\t");
      Serial.print(radio.getRSSI());
      Serial.println(" dBm");

    } else if (state == RADIOLIB_ERR_RX_TIMEOUT) {
      // timeout occurred while waiting for a packet
      Serial.println("timeout!");
    } else if (state == RADIOLIB_ERR_CRC_MISMATCH) {
      // packet was received, but is malformed
      Serial.println("CRC error!");
    } else {
      // some other error occurred
      Serial.print("failed, code ");
      Serial.println(state);
    }

    // resume listening
    state = radio.startReceive();
    if (state != RADIOLIB_ERR_NONE) {
      error_message("Resuming reception failed", state);
    }
  }

  // Handle button presses
  if (buttonFlag) {
    buttonFlag = false;
    
    // transmit packet
    Serial.print("Button pressed! Transmitting...");
    int16_t state = radio.transmit("CS433 - Hello World!");
    if (state == RADIOLIB_ERR_NONE) {
      Serial.println("Complete!");
    } else if (state == RADIOLIB_ERR_PACKET_TOO_LONG) {
      // packet was longer than max size
      Serial.println("Packet too long to transmit");
    } else if (state == RADIOLIB_ERR_TX_TIMEOUT) {
      // timeout occurred while transmitting packet
      Serial.println("TX timeout occurred?");
    } else {
      // Some other error occurred
      Serial.printf("Error while transmitting! Error code: %d\n", state);
    }

    // transmitting drops us out of receiving mode as if we received a packet
    // reset the receivedFlag status and resume receiving
    receivedFlag = false;
    state = radio.startReceive();
    if (state != RADIOLIB_ERR_NONE) {
      error_message("Resuming reception failed", state);
    }
  }

  // If you want some actions to happen with a time delay, use this
  static unsigned long next_time = millis();
  if (millis() > next_time) {
    next_time += 1000;

    // periodic actions here
  }
}

