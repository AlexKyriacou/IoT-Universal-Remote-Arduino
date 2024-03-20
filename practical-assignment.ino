#include <Arduino.h>
#include <ArduinoJson.h>
#include "Led.h"
#include <IRremote.hpp>
#include <Button.h>

// MARK_EXCESS_MICROS is subtracted from all marks and added to all spaces before decoding,
// to compensate for the signal forming of different IR receiver modules. See also IRremote.hpp line 142.
#define MARK_EXCESS_MICROS 20 // 20 is recommended for the cheap VS1838 modules.
// #define DEBUG // Activate this for lots of lovely debug output from the decoders.
#define EXCLUDE_EXOTIC_PROTOCOLS

#define NUM_LEDS 3
#define IR_FREQUENCY 38


int DELAY_BETWEEN_REPEAT = 100;

// Storage for the recorded code
struct storedIRDataStruct
{
  IRData receivedIRData;
  // extensions for sendRaw
  uint8_t rawCode[RAW_BUFFER_LENGTH]; // The durations if raw
  uint8_t rawCodeLength;              // The length of the code
} sStoredIRData;

bool sSendButtonWasActive;

void storeCode();
void sendCode(storedIRDataStruct *aIRDataToSend);

const int GREEN_LED_PIN = 5;
const int YELLOW_LED_PIN = 6;
const int RED_LED_PIN = 7;
const int BUTTON_PIN = 3;
const int IR_RECEIVE_PIN = 4;
const int IR_SEND_PIN = 2;

enum LedState
{
  GREEN,
  YELLOW,
  RED
};
Led greenLed(GREEN_LED_PIN);
Led yellowLed(YELLOW_LED_PIN);
Led redLed(RED_LED_PIN);
Led *leds[] = {&greenLed, &yellowLed, &redLed};

Button button(BUTTON_PIN);

void setup()
{
  Serial.begin(115200);
  delay(4000); // To be able to connect Serial monitor after reset or power up and before first print out. Do not wait for an attached Serial Monitor!

  IrReceiver.begin(IR_RECEIVE_PIN, ENABLE_LED_FEEDBACK);
#ifdef DEBUG
  Serial.print("Ready to receive IR signals of protocols: ");
  printActiveIRProtocols(&Serial);
  Serial.println("at pin " + String(IR_RECEIVE_PIN));
#endif

  IrSender.begin(IR_SEND_PIN);
#ifdef DEBUG
  Serial.print("Ready to send IR signals at pin " + String(IR_SEND_PIN) + " on press of button at pin ");
  Serial.println(BUTTON_PIN);
#endif

  // Sets the button pin to be input with pullup resistor
  button.begin();
  for (int i = 0; i < NUM_LEDS; i++)
  {
    leds[i]->off();
  }
  IrReceiver.start();
}

void toggleLed(LedState state)
{
  for (int i = 0; i < NUM_LEDS; i++)
  {
    if (i == state)
    {
      leds[i]->on();
    }
    else
    {
      leds[i]->off();
    }
  }
}

int menuOption = 0;
unsigned long lastRepeat = 0;
void loop()
{
  if (menuOption == 0)
  {
    redLed.on();
    // Begin to record new IR codes, if one is recieved and it is unknown then store it and turn
    // on the yellow LED to indicate that it may be corrupt. If the user presses the button then
    // send the code to the serial monitor.
    // else if the code is not unknown then turn on the green led to indicate that its good and
    // if the user presses the button then send the code to the serial monitor.
    if (IrReceiver.decode())
    {
      storeCode();
      IrReceiver.resume(); // resume receiver
      if (sStoredIRData.receivedIRData.protocol == UNKNOWN)
      {
        yellowLed.on();
        greenLed.off();
      }
      else
      {
        greenLed.on();
        yellowLed.off();
      }
    }
    if (button.pressed())
    {
      serialiseAndPrintIRData(&sStoredIRData);
      menuOption = 1;
      IrReceiver.stop();
    }
  }
  if (menuOption == 1)
  {
    toggleLed(YELLOW);
    // Listen for Json messages within the serial monitor and then send the IR code to the
    // IR sender.
    if (Serial.available() > 0)
    {
      // Read the incoming byte
      String message = Serial.readStringUntil('\n');
      // strip \n at end
      if (message.endsWith("\n"))
      {
        // Remove the newline character from the end
        message.remove(message.length() - 1);
      }
      Serial.println(message);
      JsonDocument doc;
      DeserializationError error = deserializeJson(doc, message);
      toggleLed(YELLOW);
      if (error)
      {
        Serial.print(F("deserializeJson() failed: "));
        Serial.println(error.c_str());
        return;
      }
      if (doc.containsKey("PROTOCOL") && doc.containsKey("ADDRESS") && doc.containsKey("COMMAND") && doc.containsKey("RAWDATA") && doc.containsKey("RAWDATALENGTH"))
      {
        if (doc["PROTOCOL"] == 0)
        {
          uint8_t recievedRawCode[RAW_BUFFER_LENGTH];
          uint8_t recievedRawCodeLength = doc["RAWDATALENGTH"];
          for (int i = 0; i < recievedRawCodeLength; i++)
          {
            recievedRawCode[i] = doc["RAWDATA"][i];
          }
          IrSender.sendRaw(recievedRawCode, recievedRawCodeLength, IR_FREQUENCY);
          Serial.println(F("Sent raw IR data"));
          toggleLed(GREEN);
        }
        else
        {
          IRData receivedIRData;
          receivedIRData.protocol = doc["PROTOCOL"];
          receivedIRData.address = doc["ADDRESS"];
          receivedIRData.command = doc["COMMAND"];
          IrSender.write(&receivedIRData);
          Serial.println(F("Sent IR data"));
          toggleLed(GREEN);
        }
      }
      else
      {
        Serial.println(F("Invalid Json message"));
      }
    }
    if (button.pressed())
    {
      menuOption = 0;
      IrReceiver.start();
    }
  }
}

// Stores the code for later playback in sStoredIRData
// Most of this code is just logging
void storeCode()
{
  if (IrReceiver.decodedIRData.rawDataPtr->rawlen < 4)
  {
#ifdef DEBUG
    Serial.print(F("Ignore data with rawlen="));
    Serial.println(IrReceiver.decodedIRData.rawDataPtr->rawlen);
#endif
    return;
  }
  if (IrReceiver.decodedIRData.flags & IRDATA_FLAGS_IS_REPEAT)
  {
#ifdef DEBUG
    Serial.println(F("Ignore repeat"));
#endif
    return;
  }
  if (IrReceiver.decodedIRData.flags & IRDATA_FLAGS_IS_AUTO_REPEAT)
  {
#ifdef DEBUG
    Serial.println(F("Ignore autorepeat"));
#endif
    return;
  }
  if (IrReceiver.decodedIRData.flags & IRDATA_FLAGS_PARITY_FAILED)
  {
#ifdef DEBUG
    Serial.println(F("Ignore parity error"));
#endif
    return;
  }
  /*
   * Copy decoded data
   */
  sStoredIRData.receivedIRData = IrReceiver.decodedIRData;
  IrReceiver.printIRResultShort(&Serial);
  IrReceiver.printIRSendUsage(&Serial);
  if (sStoredIRData.receivedIRData.protocol == UNKNOWN)
  {
#ifdef DEBUG
    Serial.print(F("Received unknown code and store "));
    Serial.print(IrReceiver.decodedIRData.rawDataPtr->rawlen - 1);
    Serial.println(F(" timing entries as raw "));
    IrReceiver.printIRResultRawFormatted(&Serial, true); // Output the results in RAW format
#endif
    sStoredIRData.rawCodeLength = IrReceiver.decodedIRData.rawDataPtr->rawlen - 1;
    // Store the current raw data in a dedicated array for later usage
    IrReceiver.compensateAndStoreIRResultInArray(sStoredIRData.rawCode);
  }
  else
  {
#ifdef DEBUG
    IrReceiver.printIRResultShort(&Serial);
    IrReceiver.printIRSendUsage(&Serial);
    IrReceiver.printIRResultAsCVariables(&Serial);
#endif
    sStoredIRData.receivedIRData.flags = 0; // clear flags -esp. repeat- for later sending
    // clear raw data
    sStoredIRData.rawCodeLength = 0;
#ifdef DEBUG
    Serial.println();
#endif
  }
}

void sendCode(storedIRDataStruct *aIRDataToSend)
{
  if (aIRDataToSend->receivedIRData.protocol == UNKNOWN /* i.e. raw */)
  {
    // Assume 38 KHz
    IrSender.sendRaw(aIRDataToSend->rawCode, aIRDataToSend->rawCodeLength, IR_FREQUENCY);
#ifdef DEBUG
    Serial.print(F("raw "));
    Serial.print(aIRDataToSend->rawCodeLength);
    Serial.println(F(" marks or spaces"));
#endif
  }
  else
  {
    // Use the write function, which does the switch for different protocols
    IrSender.write(&aIRDataToSend->receivedIRData);
#ifdef DEBUG
    printIRResultShort(&Serial, &aIRDataToSend->receivedIRData, false);
#endif
  }
}

/**
 * Serialise the currently stored IR data
 * This function is used to serialise the currently stored IR data in the sStoredIRData struct
 * and print it to the Serial output in JSON format
 * @param aIRData The stored IR data to serialise and print
 */
void serialiseAndPrintIRData(storedIRDataStruct *aIRDataToStore)
{
  Serial.print("{");
  Serial.print("\"PROTOCOL\":");
  Serial.print(aIRDataToStore->receivedIRData.protocol, DEC);
  Serial.print(",\"ADDRESS\":");
  Serial.print(aIRDataToStore->receivedIRData.address, DEC);
  Serial.print(",\"COMMAND\":");
  Serial.print(aIRDataToStore->receivedIRData.command, DEC);
  Serial.print(",\"RAWDATA\":[");
  for (int i = 0; i < sStoredIRData.rawCodeLength; i++)
  {
    Serial.print(aIRDataToStore->rawCode[i], DEC);
    if (i != aIRDataToStore->rawCodeLength - 1)
    {
      Serial.print(", ");
    }
  }
  Serial.print("]");
  Serial.print(",\"RAWDATALENGTH\":");
  Serial.print(aIRDataToStore->rawCodeLength, DEC);
  Serial.print("}");
  Serial.println();
}