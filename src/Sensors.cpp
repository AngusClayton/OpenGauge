#include "Sensors.h"
#include "Display/QMI8658.h"

// Analog sensor inputs (GPIO17 spare, GPIO18 reserved for active analog boost)
static constexpr uint8_t kAnalogBoostPin = 18;
static constexpr uint8_t kAnalogSparePin = 17;
static constexpr float kAdcReferenceVolts = 3.3f;
static constexpr float kAdcDividerCompensation = 2.0f; // 50/50 divider -> actual sensor voltage is 2x ADC input

// IMU Constants
static constexpr float kOneGMs2 = 9.807f;           // Standard gravity constant in m/s^2
static constexpr float kImuFilterAlpha = 0.25f;     // Low-pass filter smoothing coefficient (0.0 < alpha <= 1.0)

// Axis configurations mapping the hardware sensor coordinate system to vehicle dynamics
static constexpr int kLongitudinalAxis = 2;         // Axis mapping to forward/backward G-force
static constexpr int kLateralAxis = 1;              // Axis mapping to side-to-side G-force
static constexpr float kLongitudinalSign = 1.0f;    // Sign modifier for longitudinal direction
static constexpr float kLateralSign = 1.0f;         // Sign modifier for lateral direction

static volatile float gLongitudinalG = 0.0f;       // Cached low-pass filtered longitudinal G-force
static volatile float gLateralG = 0.0f;            // Cached low-pass filtered lateral G-force
static volatile float gLongitudinalOffsetG = 0.0f; // Ambient calibration offset for longitudinal G
static volatile float gLateralOffsetG = 0.0f;      // Ambient calibration offset for lateral G
static volatile bool gImuReady = false;            // System flag indicating whether QMI8658 initialized successfully

static GForcePeak gGforcePeakBuffer[kGforcePeakBufferSize] = {}; // Ring buffer for trailing G-force peak history
static size_t gGforcePeakIndex = 0;                              // Active index in G-force peak ring buffer
static uint32_t gLastPeakSampleMs = 0;                           // Timestamp tracking the last peak buffer sample
static constexpr uint32_t kGforcePeakSampleIntervalMs = 100;    // Peak buffer sampling interval (10Hz)
static constexpr uint32_t kGforceTrailWindowMs = 5000;          // Lifetime of a sample inside the peak history window (5s)

/**
 * @brief Initializes board Analog-to-Digital Converter pins.
 * 
 * Sets the ADC sampling bit width to 12-bit (resolution of 4096 levels).
 * Attenuates the analog pins to 11dB, raising the measurable range at the ESP32 
 * pin from 1.1V up to 3.1V/3.3V full-scale, enabling compatibility with voltage divider circuits.
 */
void initAnalogInputs() {
  analogReadResolution(12);
  analogSetPinAttenuation(kAnalogBoostPin, ADC_11db); // Raise range past 2.5V reference at ADC pin
  analogSetPinAttenuation(kAnalogSparePin, ADC_11db); // Attenuate secondary spare pin to match
  // To do - add the third analog pin

  pinMode(kAnalogBoostPin, INPUT);
  pinMode(kAnalogSparePin, INPUT);

  Serial.println("[ANALOG] ADC initialized on GPIO17/GPIO18");
}

/**
 * @brief Calibrates the IMU zero-point biases at stationary startup.
 * 
 * Collects 160 sensor samples over a settle period, averaging the readings to establish 
 * the baseline ambient G-force. These ambient offsets are subtracted from active readings 
 * during runtime to eliminate vehicle placement tilt biases.
 */
void calibrateImuZero() {
  if (!gImuReady) {
    return;
  }

  constexpr uint32_t kSettleDelayMs = 400;
  constexpr int kSamples = 160;

  delay(kSettleDelayMs);

  float longitudinalSum = 0.0f;
  float lateralSum = 0.0f;
  for (int i = 0; i < kSamples; i++) {
    float accMs2[3] = {0.0f, 0.0f, 0.0f};
    QMI8658_read_acc_xyz(accMs2);

    longitudinalSum += (accMs2[kLongitudinalAxis] / kOneGMs2) * kLongitudinalSign;
    lateralSum += (accMs2[kLateralAxis] / kOneGMs2) * kLateralSign;
    delay(4);
  }

  gLongitudinalOffsetG = longitudinalSum / (float)kSamples;
  gLateralOffsetG = lateralSum / (float)kSamples;
  gLongitudinalG = 0.0f;
  gLateralG = 0.0f;

  Serial.printf("[IMU] Zero calibrated lat=%0.3f long=%0.3f\n",
                (double)gLateralOffsetG,
                (double)gLongitudinalOffsetG);
}

/**
 * @brief Boots the onboard QMI8658 Inertial Measurement Unit (IMU) chip over I2C.
 * 
 * If connection succeeds, it automatically kicks off the zero-point baseline 
 * calibration routine.
 */
void initImuSensor() {
  gImuReady = (QMI8658_init() != 0);
  if (gImuReady) {
    Serial.println("[IMU] QMI8658 initialized");
    calibrateImuZero();
  } else {
    Serial.println("[IMU] QMI8658 init failed");
  }
}

/**
 * @brief Polls and filters spatial acceleration data from the IMU chip.
 * 
 * Extracts raw acceleration metrics, scales them from m/s^2 into G-force, and applies 
 * calibration offsets. Uses an exponential moving average (EMA) low-pass filter to 
 * eliminate high-frequency engine vibration noise:
 * 
 *     Filtered_G = Filtered_G + Alpha * (Raw_G - Filtered_G)
 * 
 * Tracks G-force peaks every 100ms inside a rolling ring-buffer to draw trailing dots 
 * on the visual G-force meter.
 */
void updateImuSensors() {
  if (!gImuReady) {
    return;
  }

  float accMs2[3] = {0.0f, 0.0f, 0.0f};
  QMI8658_read_acc_xyz(accMs2);

  // Convert raw acceleration from m/s^2 into G-force units and subtract baseline calibration
  const float longitudinalRawG = ((accMs2[kLongitudinalAxis] / kOneGMs2) * kLongitudinalSign) - gLongitudinalOffsetG;
  const float lateralRawG = ((accMs2[kLateralAxis] / kOneGMs2) * kLateralSign) - gLateralOffsetG;

  // Apply Exponential Moving Average (EMA) low-pass filter
  gLongitudinalG += kImuFilterAlpha * (longitudinalRawG - gLongitudinalG);
  gLateralG += kImuFilterAlpha * (lateralRawG - gLateralG);

  // Buffer dynamic peak-dot trail tracking inside ring buffer
  const uint32_t now = millis();
  if ((now - gLastPeakSampleMs) >= kGforcePeakSampleIntervalMs) {
    gLastPeakSampleMs = now;
    GForcePeak& peak = gGforcePeakBuffer[gGforcePeakIndex];
    peak.lateralG = gLateralG;
    peak.longitudinalG = gLongitudinalG;
    peak.timestampMs = now;
    gGforcePeakIndex = (gGforcePeakIndex + 1) % kGforcePeakBufferSize;
  }
}

/**
 * @brief Gets whether the IMU sensor successfully booted at startup.
 * @return true if initialized, false otherwise.
 */
bool isImuReady() { return gImuReady; }

/**
 * @brief Gets the low-pass filtered Side-to-Side (lateral) G-force.
 * @return float side-to-side Gs (negative is left, positive is right).
 */
float getLateralG() { return gLateralG; }

/**
 * @brief Gets the low-pass filtered Acceleration/Braking (longitudinal) G-force.
 * @return float forward-to-back Gs (negative is braking, positive is acceleration).
 */
float getLongitudinalG() { return gLongitudinalG; }

/**
 * @brief Gets the pointer to the trailing G-force peak ring buffer.
 * @return const GForcePeak* Pointer to ring buffer start.
 */
const GForcePeak* getGforcePeakBuffer() { return gGforcePeakBuffer; }
