/*
  RFID Smart card reader by way of Espressif ESP-WROOM-32
  and ST CR95HF NFC module

  Developed by Josh Behlen

  CR95HF           ESP32
  MISO: 16         MISO: 12
  MOSI: 17         MOSI: 13
  SCLK: 18         SCLK: 14
  SS: 15           SS: 15
  IRQ_IN: 12       UART_RX: 16
  IRQ_OUT: 14      UART_TX: 17

*/

#include <SPI.h>
#include "Arduino.h"
#include "soc/spi_struct.h"
#define uS_TO_S_FACTOR 1000000
#define MAX_NFC_READTRIES 100

struct spi_struct_t {
  spi_dev_t * dev;
  #if !CONFIG_DISABLE_HAL_LOCKS
    xSemaphoreHandle lock;
  #endif
    uint8_t num;
};

byte RXBuffer[100];    // receive buffer
const int SSPin = 15;
const int IRQPin = 16;
bool configStatus = true;
// LED pins
const int RED = 21;
const int GREEN = 22;
const int BLUE = 23;


//---------------------------------------------------------------------------------------------------

void setup() {
  // Set interrupt pin to high to have CR95HF select SPI communication
  // Set SS pin to high to select CR95HF as device to communicate with
  pinMode(IRQPin, OUTPUT);
  digitalWrite(IRQPin, HIGH);
  pinMode(SSPin, OUTPUT);
  digitalWrite(SSPin, HIGH);

  // Start serial for debugging
  Serial.begin(9600);

  // Do SPI configuration
  SPI.begin(14, 12, 13, 15);
  spi_t * _spi;
  _spi = SPI.bus();
  _spi->dev->ctrl2.miso_delay_mode = 2;
  SPI.setDataMode(SPI_MODE0);
  SPI.setBitOrder(MSBFIRST);
  SPI.setClockDivider(SPI_CLOCK_DIV128);

  // Config the CR95HF
  Serial.println("Reset CR95HF");
  CR95HF_Reset();
  delay(100);

  if (!IDN_Command())
    configStatus = false;
  if (!FieldOff_Command())
    configStatus = false;
  if (!SetProtocol_Command())
    configStatus = false;
  if (!WrReg_Command())
    configStatus = false;
  if (!Update_ARC_B())
    configStatus = false;
}


// ESP32 Methods

void goToSleep(int timeToSleep) {
  Serial.println("Going to sleep now");
  delay(100);
  Hibernate_Command();
  esp_sleep_enable_timer_wakeup(timeToSleep * uS_TO_S_FACTOR);
  esp_deep_sleep_start();
}

// CR95HF Methods

void CR95HF_Reset() {
  delay(10);
  digitalWrite(IRQPin, LOW);
  delayMicroseconds(100);
  digitalWrite(IRQPin, HIGH);
  delay(10);
}

void CR95HF_Send(byte cmnd, byte len, byte params[]) {
  digitalWrite(SSPin, LOW);
  delay(1);
  // SPI control byte to send command to CR95HF
  SPI.transfer(0x00);
  SPI.transfer(cmnd);
  SPI.transfer(len);

  // Send each config in list of params
  for (int i = 0; i < len; i++) {
    SPI.transfer(params[i]);
  }
  
  digitalWrite(SSPin, HIGH);
  delay(1);
}

bool CR95HF_Receive() {
  // Poll for data ready
  // Data is ready when a read byte
  // has bit 3 set (ex:  B'0000 1000')
  digitalWrite(SSPin, LOW);
  delay(1);
  RXBuffer[1] = 64;
  while ((RXBuffer[0] != 8) && (RXBuffer[1] != 0)) {
    RXBuffer[0] = SPI.transfer(0x03);  // Write 3 until    
    RXBuffer[0] = RXBuffer[0] & 0x08;  // bit 3 is set
    delay(10);
    RXBuffer[1] --;
  }
  digitalWrite(SSPin, HIGH);
  delay(1);
  if (RXBuffer[1] == 0)
    return 0;
    
  // Read the data
  digitalWrite(SSPin, LOW);
  delay(1);
  SPI.transfer(0x02);   // SPI control byte for read
  RXBuffer[0] = SPI.transfer(0) ;  // Response code
  RXBuffer[1] = SPI.transfer(0) ;  // Length of data
  
//  Serial.print("Response Code ");
//  Serial.print(RXBuffer[0], HEX);
//  Serial.print(" - Data len ");
//  Serial.println(RXBuffer[1], HEX);
  
  if (RXBuffer[1] >= sizeof(RXBuffer) - 2)
    RXBuffer[1] = 0;
    Serial.print("Data: ");
  for (int i = 0; i < RXBuffer[1]; i++) {
    RXBuffer[i + 2] = SPI.transfer(0);
    Serial.print(RXBuffer[i+2],HEX);
  }
  Serial.println();
  digitalWrite(SSPin, HIGH);
  delay(1);
  return true;
}

// Other Commands

bool Hibernate_Command() {
  byte things[] = {0x08, 0x04, 0x00 , 0x04 , 0x00, 0x18 , 0x00 , 0x00 , 0x00 , 0x00 , 0x00 , 0x00 , 0x00 , 0x00};
  CR95HF_Send(0x07, 14, things);
  delay(10);
  CR95HF_Receive();
}

// Polls the CR95HF for device information - this verifies
// connection between the Host and NFC module
bool IDN_Command() {
  byte i = 0;

  // Send the command
  byte things[0];
  CR95HF_Send(0x01, 0x00, things);
  delay(10);
  CR95HF_Receive();

  if ((RXBuffer[0] == 0) && (RXBuffer[1] != 0)) {
    Serial.print("DEVICE ID: ");
    for (i = 2; (RXBuffer[i] != '\0') && (i < (RXBuffer[1])); i++) {
      Serial.print(char(RXBuffer[i] ));
    }
    i++;
    Serial.print(" ");
    Serial.print("ROM CRC: ");
    Serial.print(RXBuffer[i], HEX);
    Serial.println(RXBuffer[i + 1], HEX);
    delay(1000);
    return true;
  }
  else {
    Serial.print("BAD RESPONSE TO GETTING DEVICE INFO - RESPONSE: ");
    Serial.println(RXBuffer[0], HEX);
    return false;
  }
}

// Turn off the field
bool FieldOff_Command() {
  byte things[] = {0x00, 0x00};
  CR95HF_Send(0x02, 0x02, things);
  delay(10);
  CR95HF_Receive();

  if ((RXBuffer[0] == 0) & (RXBuffer[1] == 0)) {
    Serial.println("FIELD TURNED OFF!");
    return true;
  }
  else {
    Serial.print("BAD RESPONSE TO TURNING FIELD OFF - RESPONSE:  ");
    Serial.println(RXBuffer[0], HEX);
    return false;
  }
}

// Set the protocol with additional configs
bool SetProtocol_Command() {
  // step 1 send the command
  byte things[] = {0x02, 0x00};
//  byte things[] = {0x02, 0x00, 0x02, 0x52, 0x00};
  CR95HF_Send(0x02, 0x02, things);
  delay(10);
  CR95HF_Receive();

  if ((RXBuffer[0] == 0) & (RXBuffer[1] == 0)) {
    Serial.println("PROTOCOL SET!");
    return true;
  }
  else {
    Serial.print("BAD RESPONSE TO SET PROTOCOL - RESPONSE: ");
    Serial.println(RXBuffer[0], HEX);
    return false;
  }
}

// Try to improve the frame reception
bool WrReg_Command() {
  byte things[] = {0x3A, 0x00, 0x58, 0x04};
  CR95HF_Send(0x09, 0x04, things);
  delay(10);
  CR95HF_Receive();

  if ((RXBuffer[0] == 0) & (RXBuffer[1] == 0)) {
    Serial.println("CHANGED TIMER WINDOW!");
    return true;
  }
  else {
    Serial.print("BAD RESPONSE TO CHANGING TIMER WINDOW - RESPONSE: ");
    Serial.println(RXBuffer[0], HEX);
    return false;
  }
}

// Read the Analog Register Config regs
bool Read_AnalogRegs() {
  byte things[] = {0x68, 0x00, 0x01};
  CR95HF_Send(0x09, 0x03, things);
  delay(10);
  CR95HF_Receive();

  if ((RXBuffer[0] == 0) & (RXBuffer[1] == 0)) {
    Serial.println("READ ANALOG CONFIG REGS!");
    return true;
  }
  else {
    Serial.print("BAD RESPONSE TO READING TEH ANALOG CONFIG REGS - RESPONSE: ");
    Serial.println(RXBuffer[0], HEX);
    return false;
  }
}

// Updating the ARC_B value
bool Update_ARC_B() {
  byte things[] = {0x68, 0x01, 0x01, 0xD1};
  CR95HF_Send(0x09, 0x04, things);
  delay(10);
  CR95HF_Receive();

  if ((RXBuffer[0] == 0) && (RXBuffer[1] == 0)) {
    Serial.println("UPDATED THE ARC_B VALUE!");
    return true;
  }
  else {
    Serial.print("BAD RESPONSE TO UDPATING THE ARC_B VALUE - RESPONSE: ");
    Serial.println(RXBuffer[0], HEX);
    return false;
  }
}

// Read ARC_B value
void Read_ARC_B() {
  byte things[] = {0x69, 0x01, 0x00};
  CR95HF_Send(0x08, 0x03, things);
  delay(10);
  CR95HF_Receive();

  if (RXBuffer[0] == 0) {
    Serial.print("ARC_B: ");
    Serial.println(RXBuffer[1], HEX);
  }
  else {
    Serial.print("BAD RESPONSE TO TRYING TO READ ARC_B VALUE - RESPONSE: ");
    Serial.println(RXBuffer[0], HEX);
  }
}

// Send REQAreply ATQA
bool REQA_Command() {
  byte things[] = {0x26, 0x07};
  CR95HF_Send(0x04, 0x02, things);
  delay(10);
  CR95HF_Receive();

  if ((RXBuffer[0] == 0x80) || (RXBuffer[0] == 0x90)) {
    Serial.println("SENT REQA COMMAND!");
    return true;
  }
  else {
    Serial.print("BAD RESPONSE TO SENDING REQA COMMAND - RESPONSE: ");
    Serial.println(RXBuffer[0], HEX);
    return false;
  }
}

// Send ANTICOL 1
bool ANTICOL_Command() {
  // step 1 send the command
  byte things[] = {0x93, 0x20, 0x08};
  CR95HF_Send(0x04, 0x03, things);
  delay(10);
  CR95HF_Receive();

  if ((RXBuffer[0] == 0x80) || (RXBuffer[0] == 0x90)) {
    Serial.println("SENT ANTICOL COMMAND!");
    return true;
  }
  else {
    Serial.print("BAD RESPONSE TO SENDING ANTICOL COMMAND - RESPONSE: ");
    Serial.println(RXBuffer[0], HEX);
    return false;
  }
}

// Send RID command
bool RID_Command() {
  byte things[] = {0x78, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xA8};
  CR95HF_Send(0x04, 0x08, things);
  delay(10);
  CR95HF_Receive();

  if ((RXBuffer[0] == 0x80) || (RXBuffer[0] == 0x90)) {
    Serial.println("SENT RID COMMAND!");
    return true;
  }
  else {
    Serial.print("BAD RESPONSE TO SENDING RID COMMAND - RESPONSE: ");
    Serial.println(RXBuffer[0], HEX);
    return false;
  }
}

// Send ANTICOL with Split framing
bool ANTICOL_Split_Command() {
  byte things[] = {0x93, 0x45, 0x88, 0x04, 0x0B, 0x45};
  CR95HF_Send(0x04, 0x06, things);
  delay(10);
  CR95HF_Receive();

  if ((RXBuffer[0] == 0x80) || (RXBuffer[0] == 0x90)) {
    Serial.println("SENT ANTICOL SPLIT COMMAND!");
    return true;
  }
  else {
    Serial.print("BAD RESPONSE TO SENDING ANTICOL SPLIT COMMAND - RESPONSE: ");
    Serial.println(RXBuffer[0], HEX);
    return false;
  }
}

bool Detect_Card() {
  if (REQA_Command()) {
    return true;
  }
  else if (ANTICOL_Command()) {
    return true;
  }
  else if (RID_Command()) {
    return true;
  }
  else if (ANTICOL_Split_Command()) {
    return true;
  }
  else {
    return false;
  }
}

void loop() {
  if (configStatus) {
    if (Detect_Card()) {
      Serial.print("DETECTED TAG!");
    }
    else {
      Serial.println("NO TAG DETECTED!");
    } 
  }
  else {
    Serial.println("SOMETHING FAILED IN SETUP ... CHECK DEBUG LINES ABOVE!");
    delay(5000);
  }
}
