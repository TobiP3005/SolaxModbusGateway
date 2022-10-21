#include "modbus.h"

/*******************************************************
 * Constructor
*******************************************************/
modbus::modbus() : Baudrate(19200), LastTx(0) {
  InverterData = new std::vector<reg_t>{};
  
  // https://forum.arduino.cc/t/creating-serial-objects-within-a-library/697780/11
  //HardwareSerial mySerial(2);
  mySerial = new HardwareSerial(2);
}

/*******************************************************
 * initialize transmission
*******************************************************/
void modbus::init(uint8_t clientid, uint32_t baudrate) {
  char dbg[100] = {0}; 
  memset(dbg, 0, sizeof(dbg));
  sprintf(dbg, "Init Modbus to Client 0x%02X with %d Baud", clientid, baudrate);
  Serial.println(dbg);

  this->ClientID = clientid;
  this->Baudrate = baudrate;

  // Start the Modbus serial Port
  Serial2.begin(this->Baudrate); 

  this->QueryIdData();
  delay(100);
  this->ReceiveData(); 
}

/*******************************************************
 * set Baudrate for Modbus
*******************************************************/
void modbus::setBaudrate(int baudrate) {
  this->Baudrate = baudrate;
}


/*******************************************************
 * Enable MQTT Transmission
*******************************************************/
void modbus::enableMqtt(MQTT* object) {
  this->mqtt = object;
  Serial.println("MQTT aktiviert");
}

/*******************************************************
 * Query ID Data to Inverter
*******************************************************/
void modbus::QueryIdData() {
  if (Config->GetDebugLevel() >=4) {Serial.println("Query ID Data");}
  
  while (Serial2.available() > 0) { // read serial if any old data is available
    Serial2.read();
  }
  
  byte message[] = {this->ClientID, 
                               0x03,  // FunctionCode
                               0x00,  // StartAddress MSB
                               0x00,  // StartAddress LSB
                               0x00,  // Anzahl Register MSB
                               0x14,  // Anzahl Register LSB
                               0x00,  // CRC LSB
                               0x00   // CRC MSB
           }; // 
           
  uint16_t crc = this->Calc_CRC(message, sizeof(message)-2);
  message[sizeof(message)-1] = highByte(crc);
  message[sizeof(message)-2] = lowByte(crc);
    
  Serial2.write(message, sizeof(message));
  Serial2.flush();
}

/*******************************************************
 * Query Live Data to Inverter
*******************************************************/
void modbus::QueryLiveData() {
  if (Config->GetDebugLevel() >=4) {Serial.println("Query Live Data");}
  
  while (Serial2.available() > 0) { // read serial if any old data is available
    Serial2.read();
  }
  
  byte message[] = {this->ClientID, 
                               0x04,  // FunctionCode
                               0x00,  // StartAddress MSB
                               0x00,  // StartAddress LSB
                               0x00,  // Anzahl Register MSB
                               0x23,  // Anzahl Register LSB
                               0x00,  // CRC LSB
                               0x00   // CRC MSB
           }; // 
           
  uint16_t crc = this->Calc_CRC(message, sizeof(message)-2);
  message[sizeof(message)-1] = highByte(crc);
  message[sizeof(message)-2] = lowByte(crc);
    
  Serial2.write(message, sizeof(message));
  Serial2.flush();
}

/*******************************************************
 * Receive Data after Quering
*******************************************************/
void modbus::ReceiveData() {
  std::vector<byte>DataFrame {};
  char dbg[100] = {0}; 
  memset(dbg, 0, sizeof(dbg));
  
  if (Config->GetDebugLevel() >=4) {Serial.println("Lese Daten: ");}

// TEST ***********************************************
//byte ReadBuffer[] = {0x01, 0x04, 0x18, 0x08, 0xE7, 0x00, 0x0C, 0x00, 0xEE, 0x0A, 0xD5, 0x00, 0x00, 0x00, 0x09, 0x00, 0x00, 0x13, 0x85, 0x00, 0x1C, 0x00, 0x02, 0x00, 0xF8, 0x00, 0x00, 0x2E, 0x8F};
//byte ReadBuffer[] = {0x01 0x04 0x46 0x09 0x01 0x00 0x0E 0x01 0x21 0x0A 0xC5 0x00 0x00 0x00 0x0A 0x00 0x00 0x13 0x85 0x00 0x1D 0x00 0x02 0x01 0x30 0x00 0x00 0x00 0x00 0x00 0x00 0x00 0x00 0x00 0x00 0x00 0x00 0x00 0x00 0x00 0x00 0x00 0x01 0x00 0x00 0x00 0x00 0x00 0x00 0x00 0x00 0x00 0x00 0x00 0x00 0x00 0x00 0x00 0x00 0x00 0x00 0x00 0x00 0x00 0x00 0x00 0x00 0x00 0x00 0x00 0x00 0x00 0x00 0xFE 0xF6};
//for (uint8_t i = 0; i<sizeof(ReadBuffer); i++) {
//  DataFrame.push_back(ReadBuffer[i]);
//  Serial.print(PrintHex(ReadBuffer[i])); Serial.print(" ");
//}
// ***********************************************

  if (Serial2.available()) {
    int i = 0;
    while(Serial2.available()) {
      byte d = Serial2.read();
      DataFrame.push_back(d);
      if (Config->GetDebugLevel() >=5) {Serial.print(PrintHex(d)); Serial.print(" ");}
      i++;
    }    
    if (Config->GetDebugLevel() >=5) {Serial.println();}
    
    if (DataFrame.size() > 5 && DataFrame.at(0) == this->ClientID && DataFrame.at(1) != 0x83 && DataFrame.at(1) != 0x84) {
      // Dataframe valid
      if (Config->GetDebugLevel() >=4) {Serial.println("Dataframe valid");}
      // clear old data
      InverterData->clear();

      StaticJsonDocument<2048> regjson;
      StaticJsonDocument<200> filter;

      if (DataFrame.at(1) == 0x03) {
        filter["id"] = true;
      } else if (DataFrame.at(1) == 0x04) {
        filter["livedata"] = true;
      }
    
      DeserializationError error = deserializeJson(regjson, JSON, DeserializationOption::Filter(filter));

      if (!error) {
        // Print the result
        if (Config->GetDebugLevel() >=4) {Serial.println("parsing JSON ok"); }
        if (Config->GetDebugLevel() >=5) {serializeJsonPretty(regjson, Serial);}
      } else {
        if (Config->GetDebugLevel() >=1) {Serial.print("Failed to parse JSON Register Data: "); Serial.print(error.c_str()); }
      }
      
      // https://arduinojson.org/v6/api/jsonobject/begin_end/
      JsonObject root = regjson.as<JsonObject>();
      JsonObject::iterator it = regjson.as<JsonObject>().begin();
      const char* rootname = it->key().c_str();
      
      // über alle Elemente des JSON Arrays
      for (JsonObject elem : regjson[rootname].as<JsonArray>()) {
        float val_f = 0;
        int val_i = 0;
        String val_str = "";
        float factor = 0;
        JsonArray posArray;
        reg_t d = {};
        
    //    if (((int)elem["position"] | 0) + ((int)elem["length"] | 0) > (DataFrame.size())-5) { // clientID(1), FunctionCode(1), Length(1), CRC(2)
    //      if (Config->GetDebugLevel() >=1) {Serial.println("Error:cannot read more than receiving string");}
    //      continue;
    //    }
        
        // mandantory field
        d.Name = elem["name"] | "undefined";
        
        // optional field
        if(!elem["realname"].isNull()) {
          d.RealName = elem["realname"];
        } else {
          d.RealName = d.Name;
        }
        
        // check if "position" is a well defined array
        if (elem["position"].is<JsonArray>()) {
          posArray = elem["position"].as<JsonArray>();
        } else {
          if (Config->GetDebugLevel() >=1) {
            sprintf(dbg, "Error: for Name '%s' no position array found", d.Name);
            Serial.println(dbg);
          }
          continue;
        }

        // optional field
        if (elem.containsKey("factor")) {
          factor = elem["factor"];
        } else { factor = 1; }
        
        
        if (elem["datatype"] == "float") {
          //********** handle Datatype FLOAT ***********//
          if (!posArray.isNull()){ 
            for(int v : posArray) {
              if (v < DataFrame.size()-4) { val_i = (val_i << 8) | DataFrame.at(v +3); }
            }
          } 
          val_f = (float)val_i * factor;
          d.value = &val_f;
          if (this->mqtt) { this->mqtt->Publish_Float(d.Name, val_f);}
          sprintf(dbg, "Data: %s -> %.2f", d.RealName, *(float*)d.value);
        } else if (elem["datatype"] == "integer") {
          //********** handle Datatype Integer ***********//
          if (!posArray.isNull()){ 
            for(int v : posArray) {
              if (v < DataFrame.size()-4) { val_i = (val_i << 8) | DataFrame.at(v +3); }
            }
          } 
          val_i = val_i * factor;
          d.value = &val_i;
          if (this->mqtt) { this->mqtt->Publish_Int(d.Name, val_i);}
          sprintf(dbg, "Data: %s -> %d", d.RealName, *(int*)d.value);
        } else if (elem["datatype"] == "string") {
          //********** handle Datatype String ***********//
          if (!posArray.isNull()){ 
            for(int v : posArray) {
              val_str.concat(String((char)DataFrame.at(v +3)));
            }
          } 
          d.value = &val_str;
          if (this->mqtt) { this->mqtt->Publish_String(d.Name, val_str);}
          sprintf(dbg, "Data: %s -> %s", d.RealName, (*(String*)d.value).c_str());
        } else {
          d.value = NULL;
          sprintf(dbg, "Error: for Name '%s' no valid datatype found", d.Name);
        }

        if (Config->GetDebugLevel() >=4) {Serial.println(dbg);}

        InverterData->push_back(d);
      }
    } else { 
      if (Config->GetDebugLevel() >=3) {Serial.println("unexpected response received: ");} 
      this->PrintDataFrame(&DataFrame);
    }
  } else {
    if (Config->GetDebugLevel() >=3) {Serial.println("No client response");}
  }
  
}

/*******************************************************
 * Calcule CRC Checksum
*******************************************************/
uint16_t modbus::Calc_CRC(uint8_t* message, uint8_t len) {
  uint16_t crc = 0xFFFF;
  for (int pos = 0; pos < len; pos++) {
    crc ^= (uint16_t)message[pos];          // XOR byte into least sig. byte of crc
    for (int i = 8; i != 0; i--) {    // Loop over each bit
      if ((crc & 0x0001) != 0) {      // If the LSB is set
        crc >>= 1;                    // Shift right and XOR 0xA001
        crc ^= 0xA001;
      }
      else                            // Else LSB is not set
        crc >>= 1;                    // Just shift right
    }
  }
  return crc;
}    

/*******************************************************
 * friendly output of hex nums
*******************************************************/
String modbus::PrintHex(byte num) {
  char hexCar[4];
  sprintf(hexCar, "0x%02X", num);
  return hexCar;
}

/*******************************************************
 * friendly output the entire received Dataframe
*******************************************************/
String modbus::PrintDataFrame(std::vector<byte>* frame) {
  String out = "";
  for (uint8_t i = 0; i<frame->size(); i++) {
    out.concat(this->PrintHex(frame->at(i)));
    out.concat(" ");
  }
  return out;
}
/*******************************************************
 * Loop function
*******************************************************/
void modbus::loop() {
  if (millis() - this->LastTx > Config->GetTxInterval() * 1000) {
    this->LastTx = millis();
    
    this->QueryLiveData();
    delay(100);
    this->ReceiveData();
  }
}
