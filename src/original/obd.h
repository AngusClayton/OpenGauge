#include <ESP32-TWAI-CAN.hpp>
//note for serial disable CDC on boot
// Simple sketch that querries OBD2 over CAN for coolant temperature
// Showcasing simple use of ESP32-TWAI-CAN library driver.

// Default for ESP32
#define CAN_TX    15
#define CAN_RX    16
CanFrame rxFrame;
// Cache for raw OBD values (indices 0-255 correspond to Standard PIDs)
// Storing raw integer values (A, or A*256+B, etc)
int obdCache[256];

void sendObdFrame(uint8_t obdId) {
  CanFrame obdFrame = { 0 };
  obdFrame.identifier = 0x7DF; // Default OBD2 address;
  obdFrame.extd = 0;
  obdFrame.data_length_code = 8;
  obdFrame.data[0] = 2;
  obdFrame.data[1] = 1;
  obdFrame.data[2] = obdId;
  obdFrame.data[3] = 0xAA;    
  obdFrame.data[4] = 0xAA;    
  obdFrame.data[5] = 0xAA;    
  obdFrame.data[6] = 0xAA;
  obdFrame.data[7] = 0xAA;
  ESP32Can.writeFrame(obdFrame);
}

void processFrame()
{
  if(ESP32Can.readFrame(rxFrame, 1)) 
  {
      if(rxFrame.identifier == 0x7E8) 
      {   // IF is OBD Frame:
          // Byte 2 is the PID (e.g. 0x05 for Coolant Temp)
          uint8_t pid = rxFrame.data[2];
          
          // Store raw bytes. 
          int A = rxFrame.data[3];
          int B = rxFrame.data[4];
          
          // Store 16-bit raw integer (A*256 + B).
          obdCache[pid] = (A << 8) | B;
          
          // Debug: Log received PID and value
          Serial.printf("OBD PID 0x%02X received: A=%d, B=%d, cached=0x%04X\n", 
                        pid, A, B, obdCache[pid]);
      }
    }
}

// Basic init for cache
void setupOBD() {
    for(int i=0; i<256; i++) obdCache[i] = 0;

    Serial.println("OBD Init...");
    ESP32Can.setPins(CAN_TX, CAN_RX);
    ESP32Can.setRxQueueSize(20); 
    ESP32Can.setTxQueueSize(20);
    ESP32Can.setSpeed(ESP32Can.convertSpeed(500));
    ESP32Can.begin();
}

void pollOBD(int pid)
{
  // Send request for specific PID
  sendObdFrame(pid);
  
  // Also process any pending frames
  processFrame();
}

/**
 * Get cached raw value for a PID.
 * @param pid OBD PID
 * @param bytes Number of bytes expected (1 or 2)
 * @return Raw float value (A or A*256+B) for scaling
 */
float getOBDValue(int pid, int bytes) {
    processFrame();
    if (pid < 0 || pid > 255) return 0;
    int raw = obdCache[pid];
    int A = (raw >> 8) & 0xFF;
    int B = raw & 0xFF;
    
    if (bytes == 2) {
        return (float)((A * 256) + B);
    } else {
        return (float)A;
    }
}
