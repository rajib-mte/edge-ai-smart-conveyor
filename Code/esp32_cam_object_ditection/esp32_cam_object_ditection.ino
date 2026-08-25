/*
 * ============================================================
 * Edge Impulse ESP32-S3 + OV3660
 * Continuous Image Classification / Object Detection
 *
 * Arduino ESP32 Core: 2.0.14
 *
 * Board:
 * ESP32S3 Dev Module
 *
 * Camera:
 * OV3660
 *
 * Verified camera pinout:
 * ============================================================
 */

#include <Arduino.h>

#include <ai_inferencing.h>
#include "edge-impulse-sdk/dsp/image/image.hpp"

#include "esp_camera.h"


// ============================================================
// ESP32-S3 + OV3660 PINOUT
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
// ============================================================
//
// IMPORTANT:
// Arduino ESP32 Core 2.0.14 uses:
//     pin_sscb_sda
//     pin_sscb_scl
//
// Do NOT change these to pin_sccb_* when using Core 2.0.14.
// ============================================================

static camera_config_t camera_config = {

    // ----------------------------------------
    // Camera power / reset
    // ----------------------------------------

    .pin_pwdn = PWDN_GPIO_NUM,
    .pin_reset = RESET_GPIO_NUM,


    // ----------------------------------------
    // Camera clock
    // ----------------------------------------

    .pin_xclk = XCLK_GPIO_NUM,


    // ----------------------------------------
    // SCCB
    // ----------------------------------------

    .pin_sscb_sda = SIOD_GPIO_NUM,
    .pin_sscb_scl = SIOC_GPIO_NUM,


    // ----------------------------------------
    // Camera data pins
    // ----------------------------------------

    .pin_d7 = Y9_GPIO_NUM,
    .pin_d6 = Y8_GPIO_NUM,
    .pin_d5 = Y7_GPIO_NUM,
    .pin_d4 = Y6_GPIO_NUM,

    .pin_d3 = Y5_GPIO_NUM,
    .pin_d2 = Y4_GPIO_NUM,
    .pin_d1 = Y3_GPIO_NUM,
    .pin_d0 = Y2_GPIO_NUM,


    // ----------------------------------------
    // Sync / pixel clock
    // ----------------------------------------

    .pin_vsync = VSYNC_GPIO_NUM,
    .pin_href = HREF_GPIO_NUM,
    .pin_pclk = PCLK_GPIO_NUM,


    // ----------------------------------------
    // XCLK
    // ----------------------------------------

    .xclk_freq_hz = 20000000,


    // ----------------------------------------
    // LEDC
    // ----------------------------------------

    .ledc_timer = LEDC_TIMER_0,
    .ledc_channel = LEDC_CHANNEL_0,


    // ----------------------------------------
    // Image format
    // ----------------------------------------

    .pixel_format = PIXFORMAT_JPEG,

    .frame_size = FRAMESIZE_QVGA,


    // ----------------------------------------
    // JPEG quality
    // ----------------------------------------

    .jpeg_quality = 12,


    // ----------------------------------------
    // Frame buffers
    // ----------------------------------------

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
    Serial.println(" Edge Impulse Inference");
    Serial.println(" ESP32 Core 2.0.14");
    Serial.println("========================================");


    // ========================================================
    // Memory information
    // ========================================================

    Serial.printf(
        "Free Heap: %u bytes\n",
        ESP.getFreeHeap()
    );

    Serial.printf(
        "Free PSRAM: %u bytes\n",
        ESP.getFreePsram()
    );


    // ========================================================
    // Check PSRAM
    // ========================================================

    if (psramFound())
    {

        Serial.println(
            "PSRAM: FOUND"
        );

    }
    else
    {

        Serial.println(
            "PSRAM: NOT FOUND"
        );

    }


    // ========================================================
    // Initialize camera
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
        "Camera initialized SUCCESSFULLY"
    );


    // ========================================================
    // Memory after camera initialization
    // ========================================================

    Serial.printf(
        "Free Heap after camera: %u bytes\n",
        ESP.getFreeHeap()
    );

    Serial.printf(
        "Free PSRAM after camera: %u bytes\n",
        ESP.getFreePsram()
    );


    // ========================================================
    // Start inference
    // ========================================================

    ei_printf(
        "\nStarting continuous inference in 2 seconds...\n"
    );

    ei_sleep(2000);
}


// ============================================================
// LOOP
// ============================================================

void loop()
{

    // ========================================================
    // Wait
    // ========================================================

    if (ei_sleep(5) != EI_IMPULSE_OK)
    {
        return;
    }


    // ========================================================
    // Allocate RGB888 buffer
    //
    // 320 × 240 × 3
    // = 230400 bytes
    // ========================================================

    snapshot_buf =
        (uint8_t *) malloc(
            EI_CAMERA_RAW_FRAME_BUFFER_COLS *
            EI_CAMERA_RAW_FRAME_BUFFER_ROWS *
            EI_CAMERA_FRAME_BYTE_SIZE
        );


    // ========================================================
    // Check allocation
    // ========================================================

    if (snapshot_buf == nullptr)
    {

        ei_printf(
            "ERR: Failed to allocate snapshot buffer!\n"
        );

        return;
    }


    // ========================================================
    // Create Edge Impulse signal
    // ========================================================

    ei::signal_t signal;

    signal.total_length =
        EI_CLASSIFIER_INPUT_WIDTH *
        EI_CLASSIFIER_INPUT_HEIGHT;

    signal.get_data =
        &ei_camera_get_data;


    // ========================================================
    // Capture image
    // ========================================================

    if (
        ei_camera_capture(
            (size_t) EI_CLASSIFIER_INPUT_WIDTH,
            (size_t) EI_CLASSIFIER_INPUT_HEIGHT,
            snapshot_buf
        ) == false
    )
    {

        ei_printf(
            "Failed to capture image\r\n"
        );

        free(snapshot_buf);

        snapshot_buf = nullptr;

        return;
    }


    // ========================================================
    // Run Edge Impulse classifier
    // ========================================================

    ei_impulse_result_t result = { 0 };


    EI_IMPULSE_ERROR err =
        run_classifier(
            &signal,
            &result,
            debug_nn
        );


    if (err != EI_IMPULSE_OK)
    {

        ei_printf(
            "ERR: Failed to run classifier (%d)\n",
            err
        );

        free(snapshot_buf);

        snapshot_buf = nullptr;

        return;
    }


    // ========================================================
    // Timing
    // ========================================================

    ei_printf(
        "\nPredictions "
        "(DSP: %d ms, "
        "Classification: %d ms, "
        "Anomaly: %d ms.)\n",

        result.timing.dsp,

        result.timing.classification,

        result.timing.anomaly
    );


    // ========================================================
    // OBJECT DETECTION
    // ========================================================

#if EI_CLASSIFIER_OBJECT_DETECTION == 1

    ei_printf(
        "Object detection bounding boxes:\r\n"
    );


    for (
        uint32_t i = 0;
        i < result.bounding_boxes_count;
        i++
    )
    {

        ei_impulse_result_bounding_box_t bb =
            result.bounding_boxes[i];


        if (bb.value == 0)
        {
            continue;
        }


        ei_printf(
            "  %s "
            "(%f) "
            "[x: %u, "
            "y: %u, "
            "width: %u, "
            "height: %u]\r\n",

            bb.label,

            bb.value,

            bb.x,

            bb.y,

            bb.width,

            bb.height
        );

    }


// ========================================================
// CLASSIFICATION
// ========================================================

#else

    ei_printf(
        "Predictions:\r\n"
    );


    for (
        uint16_t i = 0;
        i < EI_CLASSIFIER_LABEL_COUNT;
        i++
    )
    {

        ei_printf(
            "  %s: ",

            ei_classifier_inferencing_categories[i]
        );


        ei_printf(
            "%.5f\r\n",

            result.classification[i].value
        );

    }

#endif


    // ========================================================
    // ANOMALY DETECTION
    // ========================================================

#if EI_CLASSIFIER_HAS_ANOMALY

    ei_printf(
        "Anomaly prediction: %.3f\r\n",

        result.anomaly
    );

#endif


    // ========================================================
    // VISUAL ANOMALY
    // ========================================================

#if EI_CLASSIFIER_HAS_VISUAL_ANOMALY

    ei_printf(
        "Visual anomalies:\r\n"
    );


    for (
        uint32_t i = 0;
        i < result.visual_ad_count;
        i++
    )
    {

        ei_impulse_result_bounding_box_t bb =
            result.visual_ad_grid_cells[i];


        if (bb.value == 0)
        {
            continue;
        }


        ei_printf(
            "  %s "
            "(%f) "
            "[x: %u, "
            "y: %u, "
            "width: %u, "
            "height: %u]\r\n",

            bb.label,

            bb.value,

            bb.x,

            bb.y,

            bb.width,

            bb.height
        );

    }

#endif


    // ========================================================
    // Free memory
    // ========================================================

    free(snapshot_buf);

    snapshot_buf = nullptr;


    // Small delay
    delay(100);
}


// ============================================================
// CAMERA INITIALIZATION
// ============================================================

bool ei_camera_init(void)
{

    // Already initialized?
    if (is_initialised)
    {
        return true;
    }


    Serial.println(
        "Calling esp_camera_init()..."
    );


    // ========================================================
    // Initialize camera
    // ========================================================

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
    // Get sensor
    // ========================================================

    sensor_t *s =
        esp_camera_sensor_get();


    if (s == nullptr)
    {

        Serial.println(
            "ERROR: Sensor pointer is NULL"
        );

        return false;
    }


    // ========================================================
    // Print sensor information
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
    // OV3660 settings
    // ========================================================

    if (s->id.PID == OV3660_PID)
    {

        Serial.println(
            "OV3660 detected"
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


    // ========================================================
    // Mark initialized
    // ========================================================

    is_initialised = true;


    return true;
}


// ============================================================
// CAMERA DEINITIALIZATION
// ============================================================

void ei_camera_deinit(void)
{

    esp_err_t err =
        esp_camera_deinit();


    if (err != ESP_OK)
    {

        ei_printf(
            "Camera deinit failed\n"
        );

        return;
    }


    is_initialised = false;
}


// ============================================================
// CAPTURE IMAGE
// ============================================================

bool ei_camera_capture(
    uint32_t img_width,
    uint32_t img_height,
    uint8_t *out_buf
)
{

    bool do_resize = false;


    // ========================================================
    // Check camera
    // ========================================================

    if (!is_initialised)
    {

        ei_printf(
            "ERR: Camera is not initialized\r\n"
        );

        return false;
    }


    // ========================================================
    // Capture JPEG frame
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
    // Convert JPEG → RGB888
    // ========================================================

    bool converted =
        fmt2rgb888(
            fb->buf,
            fb->len,
            PIXFORMAT_JPEG,
            snapshot_buf
        );


    // Return camera frame immediately
    esp_camera_fb_return(fb);


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

        do_resize = true;
    }


    // ========================================================
    // Resize / crop
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

        // ====================================================
        // No resize required
        // ====================================================

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

    // ========================================================
    // RGB888
    // ========================================================

    size_t pixel_ix =
        offset * 3;


    size_t pixels_left =
        length;


    size_t out_ptr_ix = 0;


    while (
        pixels_left != 0
    )
    {

        // ====================================================
        // Convert BGR → RGB
        // ====================================================

        out_ptr[out_ptr_ix] =

            (
                snapshot_buf[pixel_ix + 2]
                << 16
            )

            +

            (
                snapshot_buf[pixel_ix + 1]
                << 8
            )

            +

            snapshot_buf[pixel_ix];


        out_ptr_ix++;

        pixel_ix += 3;

        pixels_left--;

    }


    return 0;
}


// ============================================================
// SENSOR CHECK
// ============================================================

#if !defined(EI_CLASSIFIER_SENSOR) || \
    EI_CLASSIFIER_SENSOR != EI_CLASSIFIER_SENSOR_CAMERA

#error "Invalid model for current sensor"

#endif