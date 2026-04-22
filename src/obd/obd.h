#ifndef OBD_H
#define OBD_H

#include <Arduino.h>
#include <ESP32-TWAI-CAN.hpp>

// CAN pins
#define CAN_TX    15
#define CAN_RX    16

// OBD2 CAN IDs
#define OBD_REQUEST_ID   0x7DF
#define OBD_RESPONSE_ID  0x7E8

/**
 * Raw OBD cache - stores last received values for standard PIDs
 * Indices 0-255 correspond to OBD PID numbers
 */
extern int obdCache[256];

enum OBDLinkStatus : uint8_t {
	OBD_STATUS_STARTING = 0,
	OBD_STATUS_READY,
	OBD_STATUS_WAITING_RESPONSE,
	OBD_STATUS_RECEIVING,
	OBD_STATUS_NO_BUS,
	OBD_STATUS_ERROR
};

/**
 * True when CAN/OBD is initialized and safe to use
 */
bool isOBDReady();

/**
 * Attempt CAN recovery with a cooldown. Returns true if ready after attempt.
 */
bool tryRecoverOBD();

/**
 * Initialize CAN bus and OBD system
 */
void setupOBD();

/**
 * Send OBD request for a specific PID
 * @param pid OBD PID to request
 * @return True if the frame was queued for transmit
 */
bool sendObdFrame(uint8_t pid);

/**
 * Process any pending CAN frames and update cache
 */
void processOBDFrame();

/**
 * Poll a specific PID (request + process response)
 * @param pid OBD PID to poll
 */
void pollOBD(uint8_t pid);

/**
 * Get cached raw value for a PID
 * @param pid OBD PID
 * @param bytes Number of bytes to read (1 or 2)
 * @return Raw value (A or A*256+B)
 */
float getOBDRawValue(uint8_t pid, uint8_t bytes);

/**
 * Backward-compatible accessor matching the original working project.
 */
float getOBDValue(int pid, int bytes);

/**
 * Current high-level OBD/CAN link status for UI/debugging.
 */
OBDLinkStatus getOBDLinkStatus();

/**
 * Short human-readable OBD/CAN status string.
 */
const char* getOBDStatusText();

#endif // OBD_H
