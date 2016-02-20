// I2Cdev and MPU6050 must be installed as libraries, or else the .cpp/.h files
// for both classes must be in the include path of your project
#include "I2Cdev.h"

#include "MPU6050_6Axis_MotionApps20.h"
//#include "MPU6050.h" // not necessary if using MotionApps include file

// Arduino Wire library is required if I2Cdev I2CDEV_ARDUINO_WIRE implementation
// is used in I2Cdev.h
#if I2CDEV_IMPLEMENTATION == I2CDEV_ARDUINO_WIRE

#include "Wire.h"

#endif

// class default I2C address is 0x68
// specific I2C addresses may be passed as a parameter here
// AD0 low = 0x68 (default for SparkFun breakout and InvenSense evaluation board)
// AD0 high = 0x69
MPU6050 mpu;
//MPU6050 mpu(0x69); // <-- use for AD0 high

/* =========================================================================
   NOTE: In addition to connection 3.3v, GND, SDA, and SCL, this sketch
   depends on the MPU-6050's INT pin being connected to the Arduino's
   external interrupt #0 pin. On the Arduino Uno and Mega 2560, this is
   digital I/O pin 2.
 * ========================================================================= */


#define LED_PIN 13
#define BTN_PIN 2
#define SPEACKER_PIN 3

struct Vector4 {
    float x;
    float y;
    float z;
    float w;
};

struct Vector3 {
    float x;
    float y;
    float z;
};

struct VectorInt3 {
    int32_t x;
    int32_t y;
    int32_t z;
};

// MPU control/status vars
bool dmpReady = false;  // set true if DMP init was successful
uint8_t mpuIntStatus;   // holds actual interrupt status byte from MPU
uint8_t devStatus;      // return status after each device operation (0 = success, !0 = error)
uint16_t packetSize;    // expected DMP packet size (default is 42 bytes)
uint16_t fifoCount;     // count of all bytes currently in FIFO
uint8_t fifoBuffer[64]; // FIFO storage buffer

// orientation/motion vars
int16_t q[4];           // [w, x, y, z]         quaternion container

// packet structure for InvenSense teapot demo
uint8_t teapotPacket[14] = {'$', 0x02, 0, 0, 0, 0, 0, 0, 0, 0, 0x00, 0x00, '\r', '\n'};

// =============================
// ====== BLINKER
// =============================
int blink_delta = 150;
bool blink_on = false;
bool led_on = false;
unsigned long blink_time = millis();
unsigned long delta = millis();
// =============================
int meridian = 2000;
bool setMeridian = false;

volatile bool mpuInterrupt = false;     // indicates whether MPU interrupt pin has gone high
void dmpDataReady() {
    mpuInterrupt = true;
}

void setup() {
    // join I2C bus (I2Cdev library doesn't do this automatically)
#if I2CDEV_IMPLEMENTATION == I2CDEV_ARDUINO_WIRE
    Wire.begin();
    TWBR = 24; // 400kHz I2C clock (200kHz if CPU is 8MHz). Comment this line if having compilation difficulties with TWBR.
#elif I2CDEV_IMPLEMENTATION == I2CDEV_BUILTIN_FASTWIRE
    Fastwire::setup(400, true);
#endif

    // initialize serial communication
    // (115200 chosen because it is required for Teapot Demo output, but it's
    // really up to you depending on your project)
#ifdef DEBUG
    Serial.begin(115200);
    while (!Serial); // wait for Leonardo enumeration, others continue immediately
#endif

    // NOTE: 8MHz or slower host processors, like the Teensy @ 3.3v or Ardunio
    // Pro Mini running at 3.3v, cannot handle this baud rate reliably due to
    // the baud timing being too misaligned with processor ticks. You must use
    // 38400 or slower in these cases, or use some kind of external separate
    // crystal solution for the UART timer.

    // initialize device
    DEBUG_PRINTLN(F("Initializing I2C devices..."));
    mpu.initialize();

    // verify connection
    DEBUG_PRINTLN(F("Testing device connections..."));
    DEBUG_PRINTLN(mpu.testConnection() ? F("MPU6050 connection successful") : F("MPU6050 connection failed"));

    // load and configure the DMP
    DEBUG_PRINTLN(F("Initializing DMP..."));
    devStatus = mpu.dmpInitialize();

    // supply your own gyro offsets here, scaled for min sensitivity
    mpu.setXGyroOffset(220);
    mpu.setYGyroOffset(76);
    mpu.setZGyroOffset(-85);
    mpu.setZAccelOffset(1688); // 1688 factory default for my test chip

    // make sure it worked (returns 0 if so)
    if (devStatus == 0) {
        // turn on the DMP, now that it's ready
        DEBUG_PRINTLN(F("Enabling DMP..."));
        mpu.setDMPEnabled(true);

        // enable Arduino interrupt detection
        DEBUG_PRINTLN(F("Enabling interrupt detection (Arduino external interrupt 0)..."));
        attachInterrupt(0, dmpDataReady, RISING);
        mpuIntStatus = mpu.getIntStatus();

        // set our DMP Ready flag so the main loop() function knows it's okay to use it
        DEBUG_PRINTLN(F("DMP ready! Waiting for first interrupt..."));
        dmpReady = true;

        // get expected DMP packet size for later comparison
        packetSize = mpu.dmpGetFIFOPacketSize();
    } else {
        // ERROR!
        // 1 = initial memory load failed
        // 2 = DMP configuration updates failed
        // (if it's going to break, usually the code will be 1)
        DEBUG_PRINT(F("DMP Initialization failed (code "));
        DEBUG_PRINT(devStatus);
        DEBUG_PRINTLN(F(")"));
    }

    // configure LED for output
    pinMode(LED_PIN, OUTPUT);
    digitalWrite(BTN_PIN, HIGH);
}


void loop() {
    // if programming failed, don't try to do anything
    if (!dmpReady) return;

    if (digitalRead(BTN_PIN) == LOW) {
        digitalWrite(LED_PIN, HIGH);
        delay(3000);
        digitalWrite(LED_PIN, LOW);
        setMeridian = true;
    }

    // reset interrupt flag and get INT_STATUS byte
    mpuInterrupt = false;
    mpuIntStatus = mpu.getIntStatus();

    // get current FIFO count
    fifoCount = mpu.getFIFOCount();

    // check for overflow (this should never happen unless our code is too inefficient)
    if ((mpuIntStatus & 0x10) || fifoCount == 1024) {
        // reset so we can continue cleanly
        mpu.resetFIFO();
        DEBUG_PRINTLN(F("FIFO overflow!"));

        // otherwise, check for DMP data ready interrupt (this should happen frequently)
    } else if (mpuIntStatus & 0x02) {
        // wait for correct available data length, should be a VERY short wait
        while (fifoCount < packetSize) fifoCount = mpu.getFIFOCount();

        // read a packet from FIFO
        mpu.getFIFOBytes(fifoBuffer, packetSize);

        // track FIFO count here in case there is > 1 packet available
        // (this lets us immediately read more without waiting for an interrupt)
        fifoCount -= packetSize;

        mpu.dmpGetQuaternion(q, fifoBuffer);

        DEBUG_PRINT(q[0]);
        DEBUG_PRINT("\t");
        DEBUG_PRINT(q[1]);
        DEBUG_PRINT("\t");
        DEBUG_PRINT(q[2]);
        DEBUG_PRINT("\t");
        DEBUG_PRINT(q[3]);
        DEBUG_PRINTLN("\t");

        VectorInt3 v;
        v.x = 0;
        v.y = 0;
        v.z = 1;
        applyQuaternion(&v, q);

        DEBUG_PRINT(v.x);
        DEBUG_PRINT("\t");
        DEBUG_PRINT(v.y);
        DEBUG_PRINT("\t");
        DEBUG_PRINT(v.z);
        DEBUG_PRINTLN("\t");

        if (setMeridian) {
            meridian = v.z;
            setMeridian = false;
        } else {
            if (abs(v.z - meridian) > 2000) {
                if (abs(millis() - delta) > 1000) {
                    blink_on = true;
                }
            } else {
                delta = millis();
                blink_on = false;
            }
        }

        blink();
    }
}

void blink() {
    if (blink_on) {
        if (led_on) {
            if (millis() - blink_time > blink_delta) {
                led_on = false;
                digitalWrite(LED_PIN, LOW);
                analogWrite(SPEACKER_PIN, 0);
                blink_time = millis();
            }
        } else {
            if (millis() - blink_time > blink_delta) {
                led_on = true;
                digitalWrite(LED_PIN, HIGH);
                analogWrite(SPEACKER_PIN, 10);
                blink_time = millis();
            }
        }
    } else {
        if (led_on) {
            led_on = false;
            digitalWrite(LED_PIN, LOW);
            analogWrite(SPEACKER_PIN, 0);
        }
    }
}

void applyQuaternion(VectorInt3 *v, int16_t *q) {
    int32_t x = v->x;
    int32_t y = v->y;
    int32_t z = v->z;

    int16_t qx = q[1];
    int16_t qy = q[2];
    int16_t qz = q[3];
    int16_t qw = q[0];

    int32_t ix =  qw * x + qy * z - qz * y;
    int32_t iy =  qw * y + qz * x - qx * z;
    int32_t iz =  qw * z + qx * y - qy * x;
    int32_t iw = -qx * x - qy * y - qz * z;

    v->x = (ix * qw + iw * -qx + iy * -qz - iz * -qy) / 16384;
    v->y = (iy * qw + iw * -qy + iz * -qx - ix * -qz) / 16384;
    v->z = (iz * qw + iw * -qz + ix * -qy - iy * -qx) / 16384;
}
