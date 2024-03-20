#include <Arduino.h>
#include "Led.h"
#include <IRremote.hpp>
#include <Button.h>

// MARK_EXCESS_MICROS is subtracted from all marks and added to all spaces before decoding,
// to compensate for the signal forming of different IR receiver modules. See also IRremote.hpp line 142.
#define MARK_EXCESS_MICROS 20 // 20 is recommended for the cheap VS1838 modules.
// #define DEBUG // Activate this for lots of lovely debug output from the decoders.
#define EXCLUDE_EXOTIC_PROTOCOLS
#define IR_FREQUENCY 38

#define GREEN_LED_PIN 5
#define YELLOW_LED_PIN 6
#define RED_LED_PIN 7
#define BUTTON_PIN 3
#define IR_RECEIVE_PIN 4
#define IR_SEND_PIN 2

Led greenLed(GREEN_LED_PIN);
Led yellowLed(YELLOW_LED_PIN);
Led redLed(RED_LED_PIN);
Button button(BUTTON_PIN);

struct DeserializedData
{
  uint16_t protocol;
  uint16_t address;
  uint16_t command;
  uint8_t rawData[RAW_BUFFER_LENGTH];
  uint8_t rawDataLength;
};

enum Menu
{
  RECORD,
  LISTEN
};

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
  redLed.off();
  yellowLed.off();
  greenLed.off();
  IrReceiver.start();
}

Menu menuOption = RECORD;
void loop()
{
  if (menuOption == RECORD)
  {
    redLed.on();
    // Begin to record new IR codes, if one is recieved and it is unknown then store it and turn
    // on the yellow LED to indicate that it may be corrupt. If the user presses the button then
    // send the code to the serial monitor.
    // else if the code is not unknown then turn on the green led to indicate that its good and
    // if the user presses the button then send the code to the serial monitor.
    if (IrReceiver.decode())
    {
      // storeCode();
      IrReceiver.resume(); // resume receiver
      if (IrReceiver.decodedIRData.protocol == UNKNOWN)
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
      serialiseIRData(&IrReceiver.decodedIRData);
      menuOption = LISTEN;
      IrReceiver.stop();
    }
  }
  if (menuOption == LISTEN)
  {
    redLed.off();
    yellowLed.on();
    greenLed.off();
    // Listen for Json messages within the serial monitor and then send the IR code to the
    // IR sender.
    if (Serial.available() > 0)
    {
      redLed.off();
      yellowLed.off();
      greenLed.on();
      // Read the incoming byte
      String message = Serial.readStringUntil('\n');
      // strip \n at end
      if (message.endsWith("\n"))
      {
        // Remove the newline character from the end
        message.remove(message.length() - 1);
      }
      Serial.println(message);
      DeserializedData receivedData = deserializeJson(message);
      if (receivedData.protocol == 0)
      {
        IrSender.sendRaw(receivedData.rawData, receivedData.rawDataLength, IR_FREQUENCY);
        Serial.println(F("Sent raw IR data"));
      }
      else
      {
        IRData receivedIRData;
        receivedIRData.protocol = static_cast<decode_type_t>(receivedData.protocol);
        receivedIRData.address = receivedData.address;
        receivedIRData.command = receivedData.command;
        IrSender.write(&receivedIRData);
        Serial.println(F("Sent IR data"));
      }
    }
    if (button.pressed())
    {
      menuOption = RECORD;
      IrReceiver.start();
    }
  }
}

/**
 * Serialise the currently stored IR data
 * This function is used to serialise the currently stored IR data in the sStoredIRData struct
 * and print it to the Serial output in JSON format
 * @param aIRData The stored IR data to serialise and print
 */
void serialiseIRData(IRData *aIRDataToStore)
{
  uint8_t rawCode[RAW_BUFFER_LENGTH];
  uint8_t rawCodeLength = IrReceiver.decodedIRData.rawDataPtr->rawlen - 1;
  Serial.print(F("{"));
  Serial.print(F("\"PROTOCOL\":"));
  Serial.print(aIRDataToStore->protocol, DEC);
  Serial.print(F(",\"ADDRESS\":"));
  Serial.print(aIRDataToStore->address, DEC);
  Serial.print(F(",\"COMMAND\":"));
  Serial.print(aIRDataToStore->command, DEC);
  Serial.print(F(",\"RAWDATA\":["));
  IrReceiver.compensateAndStoreIRResultInArray(rawCode);
  for (int i = 0; i < rawCodeLength; i++)
  {
    Serial.print(rawCode[i], DEC);
    if (i != rawCodeLength - 1)
    {
      Serial.print(",");
    }
  }
  Serial.print(F("]"));
  Serial.print(F(",\"RAWDATALENGTH\":"));
  Serial.print(rawCodeLength, DEC);
  Serial.print(F("}"));
  Serial.println();
}

DeserializedData deserializeJson(String jsonString)
{
  DeserializedData deserializedData;
  int start, end;

  // Parse protocol
  start = jsonString.indexOf("PROTOCOL") + 10;
  end = jsonString.indexOf(",", start);
  deserializedData.protocol = jsonString.substring(start, end).toInt();

  // Parse address
  start = jsonString.indexOf("ADDRESS") + 9;
  end = jsonString.indexOf(",", start);
  deserializedData.address = jsonString.substring(start, end).toInt();

  // Parse command
  start = jsonString.indexOf("COMMAND") + 9;
  end = jsonString.indexOf(",", start);
  deserializedData.command = jsonString.substring(start, end).toInt();

  // Parse rawDataLength
  start = jsonString.indexOf("RAWDATALENGTH") + 15;
  end = jsonString.indexOf("]", start);
  deserializedData.rawDataLength = jsonString.substring(start, end).toInt();

  if (deserializedData.rawDataLength > RAW_BUFFER_LENGTH)
  {
    Serial.println(F("Raw data length exceeds maximum allowed"));
    return deserializedData;
  }

  // Parse rawData
  start = jsonString.indexOf("[") + 1;
  end = jsonString.indexOf("]", start);
  String rawDataString = jsonString.substring(start, end);
  String numberStr;
  int numCount = 0;
  for (int i = 0; i < rawDataString.length(); i++)
  {
    char c = rawDataString.charAt(i);
    if (c != ',' && c != ' ') // If the character is not a comma or space, add it to the number string
    {
      numberStr += c;
    }
    else
    {
      if (numberStr.length() > 0 && numCount < RAW_BUFFER_LENGTH)
      {
        deserializedData.rawData[numCount++] = numberStr.toInt(); // Store the parsed number in the array
        numberStr = "";
      }
    }
  }
  if (numberStr.length() > 0 && numCount < RAW_BUFFER_LENGTH)
  {
    deserializedData.rawData[numCount++] = numberStr.toInt(); // Store the last parsed number in the array
  }

#ifdef DEBUG
  // Print the deserialized values for verification
  Serial.print(F("Protocol: "));
  Serial.println(deserializedData.protocol);
  Serial.print(F("Address: "));
  Serial.println(deserializedData.address);
  Serial.print(F("Command: "));
  Serial.println(deserializedData.command);
  Serial.print(F("Raw Data Length: "));
  Serial.println(deserializedData.rawDataLength);
  Serial.print(F("Raw Data: "));
  for (int i = 0; i < deserializedData.rawDataLength; i++)
  {
    Serial.print(deserializedData.rawData[i]);
    Serial.print(" ");
  }
  Serial.println();
#endif
  return deserializedData;
}