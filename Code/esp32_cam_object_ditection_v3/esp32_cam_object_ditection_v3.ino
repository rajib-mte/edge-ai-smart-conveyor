/*
 * ============================================================
 * ESP32-S3 + OV3660 + Edge Impulse
 * Automatic Object Sorting Conveyor
 *
 * Arduino ESP32 Core: 2.0.14
 *
 * Operation:
 *
 * 1. DC motor runs
 * 2. Ultrasonic detects object
 * 3. Motor stops
 * 4. Camera captures object
 * 5. Edge Impulse classifies object
 * 6. If PLASTIC:
 *       Servo -> 45 degrees
 *
 *    If POTATO / OTHER:
 *       Servo remains at 0 degrees
 *
 * 7. Motor starts again
 * ============================================================
 */

#include <Arduino.h>

#include <ai_inferencing.h>
#include "edge-impulse-sdk/dsp/image/image.hpp"

#include "esp_camera.h"
#include <ESP32Servo.h>


// ============================================================
// CAMERA PINOUT
// ESP32-S3 + OV3660
// ============================================================

#define PWDN_GPIO_NUM     -1
#define RESET_GPIO_NUM    -1

#define XCLK_GPIO_NUM     15

#define SIOD_GPIO_NUM      4
#define SIOC_GPIO_NUM      5

#define Y9_GPIO_NUM       16
#define Y8_GPIO_NUM       17
#define Y7_GPIO_NUM       18
#define Y6_GPIO_NUM       12
#define Y5_GPIO_NUM       10
#define Y4_GPIO_NUM        8
#define Y3_GPIO_NUM        9
#define Y2_GPIO_NUM       11

#define VSYNC_GPIO_NUM     6
#define HREF_GPIO_NUM      7
#define PCLK_GPIO_NUM     13


// ============================================================
// ULTRASONIC SENSOR
// HC-SR04
// ============================================================

#define TRIG_PIN  1
#define ECHO_PIN  2

// Object detection distance
#define OBJECT_DISTANCE_CM 13


// ============================================================
// SERVO
// ============================================================

#define SERVO_PIN 3

#define SERVO_HOME_ANGLE     100
#define SERVO_PLASTIC_ANGLE 150

Servo sortingServo;


// ============================================================
// DC MOTOR
// ============================================================
//
// Use a motor driver / MOSFET.
// Do NOT connect motor directly to ESP32.
//
// HIGH = motor ON
// LOW  = motor OFF
// ============================================================

#define MOTOR_PIN 42


// ============================================================
// EDGE IMPULSE CAMERA SETTINGS
// ============================================================

#define EI_CAMERA_RAW_FRAME_BUFFER_COLS   320
#define EI_CAMERA_RAW_FRAME_BUFFER_ROWS   240
#define EI_CAMERA_FRAME_BYTE_SIZE         3


// ============================================================
// VARIABLES
// ============================================================

static bool debug_nn = false;

static bool is_initialised = false;

uint8_t *snapshot_buf = nullptr;


// ============================================================
// CAMERA CONFIGURATION
// Arduino ESP32 Core 2.0.14
// ============================================================

static camera_config_t camera_config = {

    // Camera power
    .pin_pwdn = PWDN_GPIO_NUM,
    .pin_reset = RESET_GPIO_NUM,

    // XCLK
    .pin_xclk = XCLK_GPIO_NUM,

    // SCCB
    .pin_sscb_sda = SIOD_GPIO_NUM,
    .pin_sscb_scl = SIOC_GPIO_NUM,

    // Camera data
    .pin_d7 = Y9_GPIO_NUM,
    .pin_d6 = Y8_GPIO_NUM,
    .pin_d5 = Y7_GPIO_NUM,
    .pin_d4 = Y6_GPIO_NUM,

    .pin_d3 = Y5_GPIO_NUM,
    .pin_d2 = Y4_GPIO_NUM,
    .pin_d1 = Y3_GPIO_NUM,
    .pin_d0 = Y2_GPIO_NUM,

    // Sync
    .pin_vsync = VSYNC_GPIO_NUM,
    .pin_href = HREF_GPIO_NUM,
    .pin_pclk = PCLK_GPIO_NUM,

    // Clock
    .xclk_freq_hz = 20000000,

    // LEDC
    .ledc_timer = LEDC_TIMER_0,
    .ledc_channel = LEDC_CHANNEL_0,

    // Image
    .pixel_format = PIXFORMAT_JPEG,
    .frame_size = FRAMESIZE_QVGA,

    // JPEG
    .jpeg_quality = 12,

    // Frame buffer
    .fb_count = 1,

    .fb_location = CAMERA_FB_IN_PSRAM,

    .grab_mode = CAMERA_GRAB_WHEN_EMPTY
};


// ============================================================
// FUNCTION DECLARATIONS
// ============================================================

bool ei_camera_init(void);

void ei_camera_deinit(void);

bool ei_camera_capture(
    uint32_t img_width,
    uint32_t img_height,
    uint8_t *out_buf
);

static int ei_camera_get_data(
    size_t offset,
    size_t length,
    float *out_ptr
);


// Ultrasonic
float getDistanceCM();


// Motor
void motorStart();
void motorStop();


// Servo
void servoWriteAngle(int angle);


// Classification
void classifyObject();


// ============================================================
// SETUP
// ============================================================

void setup()
{

    Serial.begin(115200);

    delay(2000);

    Serial.println();
    Serial.println();
    Serial.println("========================================");
    Serial.println(" ESP32-S3 + OV3660");
    Serial.println(" EDGE AI SORTING SYSTEM");
    Serial.println("========================================");


    // ========================================================
    // MEMORY
    // ========================================================

    Serial.printf(
        "Free Heap: %u bytes\n",
        ESP.getFreeHeap()
    );

    Serial.printf(
        "Free PSRAM: %u bytes\n",
        ESP.getFreePsram()
    );


    if (psramFound())
    {
        Serial.println("PSRAM: FOUND");
    }
    else
    {
        Serial.println("PSRAM: NOT FOUND");
    }


    // ========================================================
    // ULTRASONIC
    // ========================================================

    pinMode(TRIG_PIN, OUTPUT);

    pinMode(ECHO_PIN, INPUT);

    digitalWrite(
        TRIG_PIN,
        LOW
    );


    // ========================================================
    // DC MOTOR
    // ========================================================

    pinMode(
        MOTOR_PIN,
        OUTPUT
    );

    motorStop();


    // ========================================================
    // SERVO
    // ========================================================

    Serial.println();
    Serial.println("Initializing servo...");

    // ESP32Servo library
    sortingServo.setPeriodHertz(50);
    sortingServo.attach(SERVO_PIN, 500, 2500);

    delay(500);

    if (sortingServo.attached())
    {
        Serial.println("Servo attached successfully.");
    }
    else
    {
        Serial.println("ERROR: Servo attach failed!");
    }

    // ========================================================
    // SERVO TEST
    // ========================================================

    Serial.println();
    Serial.println("========================================");
    Serial.println(" SERVO TEST");
    Serial.println("========================================");

    Serial.println("Servo -> 40 degrees");
    sortingServo.write(SERVO_PLASTIC_ANGLE);
    delay(1000);

    Serial.println("Servo -> 9 degrees");
    sortingServo.write(SERVO_HOME_ANGLE);
    delay(1000);



    // ========================================================
    // CAMERA
    // ========================================================

    Serial.println();
    Serial.println(
        "Initializing OV3660 camera..."
    );


    if (!ei_camera_init())
    {

        Serial.println(
            "ERROR: Camera initialization FAILED!"
        );

        while (true)
        {
            delay(1000);
        }

    }


    Serial.println(
        "Camera initialized successfully"
    );


    // ========================================================
    // EDGE IMPULSE
    // ========================================================

    ei_printf(
        "\nEdge Impulse initialized\n"
    );


    // ========================================================
    // START CONVEYOR
    // ========================================================

    Serial.println();
    Serial.println(
        "Starting conveyor motor..."
    );

    motorStart();


    Serial.println();
    Serial.println(
        "========================================"
    );

    Serial.println(
        " SYSTEM READY"
    );

    Serial.println(
        " Waiting for object..."
    );

    Serial.println(
        "========================================"
    );
}


// ============================================================
// MAIN LOOP
// ============================================================

void loop()
{

    // ========================================================
    // Measure distance
    // ========================================================

    float distance =
        getDistanceCM();


    // ========================================================
    // Debug distance
    // ========================================================

    if (distance > 0)
    {

        Serial.print(
            "Distance: "
        );

        Serial.print(
            distance
        );

        Serial.println(
            " cm"
        );

    }


    // ========================================================
    // Object detected
    // ========================================================

    if (
        distance > 0 &&
        distance <= OBJECT_DISTANCE_CM
    )
    {

        Serial.println();
        Serial.println(
            "********************************"
        );

        Serial.println(
            "OBJECT DETECTED!"
        );

        Serial.println(
            "Stopping conveyor..."
        );


        // ====================================================
        // STOP MOTOR
        // ====================================================
        delay(600);
        motorStop();


        // ====================================================
        // Give conveyor/object time to stop
        // ====================================================

        delay(500);


        // ====================================================
        // Classify object
        // ====================================================

        classifyObject();
        classifyObject();


        // ====================================================
        // Prevent immediate re-trigger
        // ====================================================

        Serial.println();
        Serial.println(
            "Waiting for object to leave..."
        );


        delay(500);


        // ====================================================
        // Restart conveyor
        // ====================================================

        Serial.println(
            "Starting conveyor..."
        );

        motorStart();


        Serial.println(
            "********************************"
        );

        Serial.println();
    }


    // Small delay
    delay(100);
}


// ============================================================
// ULTRASONIC DISTANCE
// ============================================================

float getDistanceCM()
{

    // Send trigger pulse
    digitalWrite(
        TRIG_PIN,
        LOW
    );

    delayMicroseconds(2);


    digitalWrite(
        TRIG_PIN,
        HIGH
    );

    delayMicroseconds(10);


    digitalWrite(
        TRIG_PIN,
        LOW
    );


    // Measure echo
    unsigned long duration =
        pulseIn(
            ECHO_PIN,
            HIGH,
            30000
        );


    if (duration == 0)
    {
        return -1;
    }


    // Speed of sound:
    // distance = time × 0.0343 / 2

    float distance =
        duration * 0.0343 / 2.0;


    return distance;
}


// ============================================================
// MOTOR START
// ============================================================

void motorStart()
{

    digitalWrite(
        MOTOR_PIN,
        HIGH
    );

}


// ============================================================
// MOTOR STOP
// ============================================================

void motorStop()
{

    digitalWrite(
        MOTOR_PIN,
        LOW
    );

}


// ============================================================
// SERVO ANGLE
// ============================================================

void servoWriteAngle(
    int angle
)
{
    angle = constrain(
        angle,
        0,
        180
    );

    sortingServo.write(angle);

    Serial.print(
        "Servo angle: "
    );

    Serial.print(
        angle
    );

    Serial.println(
        " degrees"
    );
}


// ============================================================
// CLASSIFY OBJECT
// ============================================================

void classifyObject()
{

    Serial.println();
    Serial.println(
        "================================"
    );

    Serial.println(
        "CAPTURING OBJECT..."
    );


    // ========================================================
    // Allocate RGB buffer
    // ========================================================

    snapshot_buf =
        (uint8_t *) malloc(

            EI_CAMERA_RAW_FRAME_BUFFER_COLS *
            EI_CAMERA_RAW_FRAME_BUFFER_ROWS *
            EI_CAMERA_FRAME_BYTE_SIZE

        );


    if (snapshot_buf == nullptr)
    {

        Serial.println(
            "ERROR: Snapshot buffer allocation failed!"
        );

        motorStart();

        return;
    }


    // ========================================================
    // Create signal
    // ========================================================

    ei::signal_t signal;


    signal.total_length =
        EI_CLASSIFIER_INPUT_WIDTH *
        EI_CLASSIFIER_INPUT_HEIGHT;


    signal.get_data =
        &ei_camera_get_data;


    // ========================================================
    // Capture
    // ========================================================

    if (
        ei_camera_capture(

            (size_t)
            EI_CLASSIFIER_INPUT_WIDTH,

            (size_t)
            EI_CLASSIFIER_INPUT_HEIGHT,

            snapshot_buf

        ) == false
    )
    {

        Serial.println(
            "ERROR: Camera capture failed!"
        );


        free(
            snapshot_buf
        );


        snapshot_buf =
            nullptr;


        return;
    }


    Serial.println(
        "Image captured."
    );


    // ========================================================
    // Run classifier
    // ========================================================

    ei_impulse_result_t result =
        { 0 };


    EI_IMPULSE_ERROR err =
        run_classifier(

            &signal,

            &result,

            debug_nn

        );


    if (
        err != EI_IMPULSE_OK
    )
    {

        Serial.print(
            "Classifier error: "
        );

        Serial.println(
            err
        );


        free(
            snapshot_buf
        );


        snapshot_buf =
            nullptr;


        return;
    }


    // ========================================================
    // Print timing
    // ========================================================

    Serial.println();

    Serial.print(
        "DSP: "
    );

    Serial.print(
        result.timing.dsp
    );

    Serial.println(
        " ms"
    );


    Serial.print(
        "Classification: "
    );

    Serial.print(
        result.timing.classification
    );

    Serial.println(
        " ms"
    );


    // ========================================================
    // CLASSIFICATION MODEL
    // ========================================================

#if EI_CLASSIFIER_OBJECT_DETECTION == 0

    float highestConfidence =
        0.0;


    int bestClass =
        -1;


    Serial.println();
    Serial.println(
        "Predictions:"
    );


    for (
        uint16_t i = 0;
        i < EI_CLASSIFIER_LABEL_COUNT;
        i++
    )
    {

        float confidence =
            result.classification[i].value;


        Serial.print(
            "  "
        );

        Serial.print(
            ei_classifier_inferencing_categories[i]
        );

        Serial.print(
            ": "
        );

        Serial.println(
            confidence,
            5
        );


        if (
            confidence >
            highestConfidence
        )
        {

            highestConfidence =
                confidence;

            bestClass =
                i;
        }

    }


    // ========================================================
    // Check result
    // ========================================================

    if (bestClass < 0)
    {

        Serial.println(
            "No valid classification!"
        );

    }
    else
    {

        const char *label =
            ei_classifier_inferencing_categories[
                bestClass
            ];


        Serial.println();
        Serial.println(
            "--------------------------------"
        );


        Serial.print(
            "Detected: "
        );

        Serial.println(
            label
        );


        Serial.print(
            "Confidence: "
        );

        Serial.print(
            highestConfidence * 100.0
        );

        Serial.println(
            "%"
        );


        // ====================================================
        // PLASTIC
        // ====================================================

        if ( strcmp(label, "plastic") == 0 || strcmp(label, "cap") == 0){

            Serial.println(
                "RESULT: PLASTIC"
            );


            // -----------------------------------------------
            // Servo 45 degrees
            // -----------------------------------------------
            servoWriteAngle(SERVO_PLASTIC_ANGLE);
            Serial.println("Servo moved to 45 degrees.");
            // Give servo time to move
            delay(800);

        }


        // ====================================================
        // POTATO
        // ====================================================

        else if (strcmp(label, "potato" ) == 0){
             Serial.println("RESULT: POTATO");


            // -----------------------------------------------
            // Servo stays at 0 degrees
            // -----------------------------------------------

            servoWriteAngle(
                SERVO_HOME_ANGLE
            );


            Serial.println(
                "Servo remains at 0 degrees."
            );

        }


        // ====================================================
        // OTHER
        // ====================================================

        else
        {

            Serial.println(
                "RESULT: OTHER"
            );


            // Keep servo at home
            servoWriteAngle(
                SERVO_HOME_ANGLE
            );

        }


        Serial.println(
            "--------------------------------"
        );

    }


// ============================================================
// OBJECT DETECTION MODEL
// ============================================================

#else

    Serial.println();
    Serial.println(
        "Object Detection Results:"
    );


    bool plasticDetected =
        false;


    for (
        uint32_t i = 0;
        i < result.bounding_boxes_count;
        i++
    )
    {

        ei_impulse_result_bounding_box_t bb =
            result.bounding_boxes[i];


        if (
            bb.value == 0
        )
        {
            continue;
        }


        Serial.print(
            "Object: "
        );

        Serial.print(
            bb.label
        );


        Serial.print(
            " | Confidence: "
        );

        Serial.print(
            bb.value * 100.0
        );

        Serial.println(
            "%"
        );


        Serial.print(
            "x="
        );

        Serial.print(
            bb.x
        );

        Serial.print(
            " y="
        );

        Serial.print(
            bb.y
        );

        Serial.print(
            " w="
        );

        Serial.print(
            bb.width
        );

        Serial.print(
            " h="
        );

        Serial.println(
            bb.height
        );


        if (
            strcmp(
                bb.label,
                "plastic"
            ) == 0
        )
        {

            plasticDetected =
                true;
        }

    }


    // ========================================================
    // Object detection decision
    // ========================================================

    if (plasticDetected)
    {

        Serial.println(
            "RESULT: PLASTIC"
        );


        servoWriteAngle(
            SERVO_PLASTIC_ANGLE
        );


        delay(800);

    }
    else
    {

        Serial.println(
            "RESULT: POTATO / OTHER"
        );


        servoWriteAngle(
            SERVO_HOME_ANGLE
        );

    }

#endif


    // ========================================================
    // Free memory
    // ========================================================

    free(
        snapshot_buf
    );


    snapshot_buf =
        nullptr;


    Serial.println(
        "Classification complete."
    );


    Serial.println(
        "================================"
    );
}


// ============================================================
// CAMERA INITIALIZATION
// ============================================================

bool ei_camera_init(void)
{

    if (is_initialised)
    {
        return true;
    }


    Serial.println(
        "Calling esp_camera_init()..."
    );


    esp_err_t err =
        esp_camera_init(
            &camera_config
        );


    if (err != ESP_OK)
    {

        Serial.printf(
            "Camera init failed with error 0x%x\n",
            err
        );

        return false;
    }


    // ========================================================
    // Sensor
    // ========================================================

    sensor_t *s =
        esp_camera_sensor_get();


    if (s == nullptr)
    {

        Serial.println(
            "Sensor pointer is NULL"
        );

        return false;
    }


    // ========================================================
    // Print camera ID
    // ========================================================

    Serial.printf(
        "Camera PID: 0x%02X\n",
        s->id.PID
    );


    Serial.printf(
        "Camera VER: 0x%02X\n",
        s->id.VER
    );


    // ========================================================
    // OV3660 configuration
    // ========================================================

    if (
        s->id.PID ==
        OV3660_PID
    )
    {

        Serial.println(
            "OV3660 detected."
        );


        s->set_vflip(
            s,
            1
        );


        s->set_brightness(
            s,
            1
        );


        s->set_saturation(
            s,
            0
        );

    }


    is_initialised =
        true;


    return true;
}


// ============================================================
// CAMERA DEINITIALIZATION
// ============================================================

void ei_camera_deinit(void)
{

    esp_err_t err =
        esp_camera_deinit();


    if (
        err != ESP_OK
    )
    {

        ei_printf(
            "Camera deinit failed\n"
        );

        return;
    }


    is_initialised =
        false;
}


// ============================================================
// CAMERA CAPTURE
// ============================================================

bool ei_camera_capture(
    uint32_t img_width,
    uint32_t img_height,
    uint8_t *out_buf
)
{

    bool do_resize =
        false;


    if (!is_initialised)
    {

        ei_printf(
            "ERR: Camera is not initialized\r\n"
        );

        return false;
    }


    // ========================================================
    // Capture JPEG
    // ========================================================

    camera_fb_t *fb =
        esp_camera_fb_get();


    if (!fb)
    {

        ei_printf(
            "Camera capture failed\n"
        );

        return false;
    }


    // ========================================================
    // JPEG → RGB888
    // ========================================================

    bool converted =
        fmt2rgb888(

            fb->buf,

            fb->len,

            PIXFORMAT_JPEG,

            snapshot_buf

        );


    esp_camera_fb_return(
        fb
    );


    if (!converted)
    {

        ei_printf(
            "JPEG to RGB888 conversion failed\n"
        );

        return false;
    }


    // ========================================================
    // Check resize
    // ========================================================

    if (
        img_width !=
        EI_CAMERA_RAW_FRAME_BUFFER_COLS ||

        img_height !=
        EI_CAMERA_RAW_FRAME_BUFFER_ROWS
    )
    {

        do_resize =
            true;
    }


    // ========================================================
    // Resize
    // ========================================================

    if (do_resize)
    {

        ei::image::processing::
        crop_and_interpolate_rgb888(

            snapshot_buf,

            EI_CAMERA_RAW_FRAME_BUFFER_COLS,

            EI_CAMERA_RAW_FRAME_BUFFER_ROWS,

            out_buf,

            img_width,

            img_height

        );

    }
    else
    {

        memcpy(

            out_buf,

            snapshot_buf,

            img_width *
            img_height *
            EI_CAMERA_FRAME_BYTE_SIZE

        );

    }


    return true;
}


// ============================================================
// EDGE IMPULSE DATA CALLBACK
// ============================================================

static int ei_camera_get_data(
    size_t offset,
    size_t length,
    float *out_ptr
)
{

    size_t pixel_ix =
        offset * 3;


    size_t pixels_left =
        length;


    size_t out_ptr_ix =
        0;


    while (
        pixels_left != 0
    )
    {

        // BGR → RGB

        out_ptr[out_ptr_ix] =

            (
                snapshot_buf[
                    pixel_ix + 2
                ]
                << 16
            )

            +

            (
                snapshot_buf[
                    pixel_ix + 1
                ]
                << 8
            )

            +

            snapshot_buf[
                pixel_ix
            ];


        out_ptr_ix++;

        pixel_ix += 3;

        pixels_left--;

    }


    return 0;
}


// ============================================================
// CHECK EDGE IMPULSE MODEL
// ============================================================

#if !defined(EI_CLASSIFIER_SENSOR) || \
    EI_CLASSIFIER_SENSOR != EI_CLASSIFIER_SENSOR_CAMERA

#error "Invalid model for current sensor"

#endif