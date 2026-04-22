#include "obd.h"

// Cache for raw OBD values (indices 0-255 correspond to Standard PIDs)
int obdCache[256] = {0};

// Original working implementation state
static volatile bool g_obdReady = false;
static uint32_t g_lastRecoveryAttemptMs = 0;
static CanFrame rxFrame = {0};
static volatile OBDLinkStatus g_obdStatus = OBD_STATUS_STARTING;
static uint32_t g_lastResponseMs = 0;

OBDLinkStatus getOBDLinkStatus() {
  return g_obdStatus;
}

const char* getOBDStatusText() {
  switch (g_obdStatus) {
    case OBD_STATUS_STARTING:
      return "CAN starting";
    case OBD_STATUS_READY:
      return "CAN ready";
    case OBD_STATUS_WAITING_RESPONSE:
      return "Waiting OBD";
    case OBD_STATUS_RECEIVING:
      return "OBD receiving";
    case OBD_STATUS_NO_BUS:
      return "No CAN reply";
    case OBD_STATUS_ERROR:
      return "CAN error";
    default:
      return "Unknown";
  }
}

bool isOBDReady() {
  return g_obdReady;
}

bool tryRecoverOBD() {
  if (g_obdReady) {
    return true;
  }

  const uint32_t now = millis();
  const uint32_t cooldownMs = 3000;
  if ((now - g_lastRecoveryAttemptMs) < cooldownMs) {
    return false;
  }

  g_lastRecoveryAttemptMs = now;
  Serial.println("[OBD] Attempting CAN recovery...");
  setupOBD();
  return g_obdReady;
}

bool sendObdFrame(uint8_t pid) {
  if (!g_obdReady) {
    return false;
  }

  CanFrame obdFrame = {0};
  obdFrame.identifier = OBD_REQUEST_ID;
  obdFrame.extd = 0;
  obdFrame.data_length_code = 8;
  obdFrame.data[0] = 2;
  obdFrame.data[1] = 1;
  obdFrame.data[2] = pid;
  obdFrame.data[3] = 0xAA;
  obdFrame.data[4] = 0xAA;
  obdFrame.data[5] = 0xAA;
  obdFrame.data[6] = 0xAA;
  obdFrame.data[7] = 0xAA;

  const bool queued = ESP32Can.writeFrame(obdFrame);
  if (!queued) {
    static uint32_t queueFailCount = 0;
    queueFailCount++;
    g_obdStatus = OBD_STATUS_NO_BUS;
    if ((queueFailCount % 20) == 1) {
      Serial.println("[OBD] OBD/CAN bus not responding or not connected");
    }
  } else {
    g_obdStatus = OBD_STATUS_WAITING_RESPONSE;
  }
  return queued;
}

void processOBDFrame() {
  if (!g_obdReady) {
    return;
  }

  if (ESP32Can.readFrame(rxFrame, 1)) {
    if (rxFrame.identifier == OBD_RESPONSE_ID) {
      uint8_t pid = rxFrame.data[2];

      int A = rxFrame.data[3];
      int B = rxFrame.data[4];

      obdCache[pid] = (A << 8) | B;
      g_lastResponseMs = millis();
      g_obdStatus = OBD_STATUS_RECEIVING;

      Serial.printf("[OBD] PID 0x%02X received: A=%d, B=%d, cached=0x%04X\n",
                    pid, A, B, obdCache[pid]);
    }
  }

  if (g_lastResponseMs > 0 && (millis() - g_lastResponseMs) > 2000) {
    g_obdStatus = OBD_STATUS_READY;
  }
}

void setupOBD() {
  g_obdReady = false;
  g_obdStatus = OBD_STATUS_STARTING;
  g_lastResponseMs = 0;

  for (int i = 0; i < 256; i++) {
    obdCache[i] = 0;
  }

  Serial.println("OBD Init...");
  ESP32Can.setPins(CAN_TX, CAN_RX);
  ESP32Can.setRxQueueSize(20);
  ESP32Can.setTxQueueSize(20);
  ESP32Can.setSpeed(ESP32Can.convertSpeed(500));
  g_obdReady = ESP32Can.begin();
  g_lastRecoveryAttemptMs = millis();
  if (g_obdReady) {
    Serial.println("[OBD] CAN initialized successfully");
    g_obdStatus = OBD_STATUS_READY;
  } else {
    Serial.println("[OBD] CAN initialization failed");
    g_obdStatus = OBD_STATUS_ERROR;
  }
}

void pollOBD(uint8_t pid) {
  if (!g_obdReady) {
    return;
  }

  if (!sendObdFrame(pid)) {
    return;
  }

  processOBDFrame();
}

float getOBDRawValue(uint8_t pid, uint8_t bytes) {
  if (pid > 255) return 0;
  
  int raw = obdCache[pid];
  int A = (raw >> 8) & 0xFF;
  int B = raw & 0xFF;
  
  if (bytes == 2) {
    return (float)((A << 8) | B);
  } else {
    return (float)A;
  }
}

float getOBDValue(int pid, int bytes) {
  processOBDFrame();
  if (pid < 0 || pid > 255) {
    return 0;
  }
  return getOBDRawValue((uint8_t)pid, (uint8_t)bytes);
}
