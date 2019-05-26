 /*
 * Copyright 2018 David B Brown (@maccoylton)
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 * 
 * http://www.apache.org/licenses/LICENSE-2.0
 
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * 
 * EPS8266 HomeKit WIFI Thermostat 
 *
 * Uses a DHT22 (temperature sensor)
 *
 *
 */

#define DEVICE_MANUFACTURER "David B Brown"
#define DEVICE_NAME "Wifi-Thermostat"
#define DEVICE_MODEL "Basic"
#define DEVICE_SERIAL "12345678"
#define FW_VERSION "1.0"

#include <stdio.h>
#include <espressif/esp_wifi.h>
#include <espressif/esp_sta.h>
#include <espressif/esp_system.h>
#include <esp/uart.h>
#include <esp8266.h>
#include <etstimer.h>
#include <esplibs/libmain.h>
#include <FreeRTOS.h>
#include <task.h>
#include <sysparam.h>

#include <ssd1306/ssd1306.h>

#include <homekit/homekit.h>
#include <homekit/characteristics.h>
#include <qrcode.h>
//#include "wifi.h"

#include <dht/dht.h>

#include "wifi_thermostat.h"
#include <button.h>


#include <led_codes.h>
#include <custom_characteristics.h>

#define TEMPERATURE_SENSOR_PIN 4
#define TEMPERATURE_POLL_PERIOD 10000
#define UP_BUTTON_GPIO 12
#define DOWN_BUTTON_GPIO 13
#define RESET_BUTTON_GPIO 0
#define LED_GPIO 2
#define QRCODE_VERSION 2

int button_pressed_value=0; /*set to o when botton is connect to gound, 1 when button is providing +3.3V */
int led_off_value=1; /* global varibale to support LEDs set to 0 where the LED is connected to GND, 1 where +3.3v */


// add this section to make your device OTA capable
// create the extra characteristic &ota_trigger, at the end of the primary service (before the NULL)
// it can be used in Eve, which will show it, where Home does not
// and apply the four other parameters in the accessories_information section



/// I2C

/* Remove this line if your display connected by SPI */
#define I2C_CONNECTION

#ifdef I2C_CONNECTION
    #include <i2c/i2c.h>
#endif


#include "fonts/fonts.h"

/* Change this according to you schematics and display size */
#define DISPLAY_WIDTH  128
#define DISPLAY_HEIGHT 64

#ifdef I2C_CONNECTION
    #define PROTOCOL SSD1306_PROTO_I2C
    #define ADDR     SSD1306_I2C_ADDR_0
    #define I2C_BUS  0
    #define SCL_PIN  14
    #define SDA_PIN  5
#else
    #define PROTOCOL SSD1306_PROTO_SPI4
    #define CS_PIN   5
    #define DC_PIN   4
#endif

#define DEFAULT_FONT FONT_FACE_TERMINUS_16X32_ISO8859_1


/* Declare device descriptor */
static const ssd1306_t display = {
    .protocol = PROTOCOL,
    .screen = SSD1306_SCREEN,
#ifdef I2C_CONNECTION
.i2c_dev.bus      = I2C_BUS,
.i2c_dev.addr     = ADDR,
#else
    .cs_pin   = CS_PIN,
    .dc_pin   = DC_PIN,
#endif
    .width    = DISPLAY_WIDTH,
    .height   = DISPLAY_HEIGHT
};

/* Local frame display_buffer */
static uint8_t display_buffer[DISPLAY_WIDTH * DISPLAY_HEIGHT / 8];

#define SECOND (1000 / portTICK_PERIOD_MS)

// I2C



#include <ota-api.h>
homekit_characteristic_t ota_trigger  = API_OTA_TRIGGER;
homekit_characteristic_t name         = HOMEKIT_CHARACTERISTIC_(NAME, DEVICE_NAME);
homekit_characteristic_t manufacturer = HOMEKIT_CHARACTERISTIC_(MANUFACTURER,  DEVICE_MANUFACTURER);
homekit_characteristic_t serial       = HOMEKIT_CHARACTERISTIC_(SERIAL_NUMBER, DEVICE_SERIAL);
homekit_characteristic_t model        = HOMEKIT_CHARACTERISTIC_(MODEL,         DEVICE_MODEL);
homekit_characteristic_t revision     = HOMEKIT_CHARACTERISTIC_(FIRMWARE_REVISION,  FW_VERSION);

void identify_task(void *_args) {

    led_code(LED_GPIO, IDENTIFY_ACCESSORY);

    vTaskDelete(NULL);
}

void thermostat_identify(homekit_value_t _value) {
    printf("Thermostat identify\n");
    xTaskCreate(identify_task, "identify", 128, NULL, 2, NULL);
}


void on_update(homekit_characteristic_t *ch, homekit_value_t value, void *context);
void process_setting_update();

homekit_characteristic_t current_temperature = HOMEKIT_CHARACTERISTIC_( CURRENT_TEMPERATURE, 0 );
homekit_characteristic_t target_temperature  = HOMEKIT_CHARACTERISTIC_( TARGET_TEMPERATURE, 22, .callback=HOMEKIT_CHARACTERISTIC_CALLBACK(on_update));
homekit_characteristic_t units               = HOMEKIT_CHARACTERISTIC_( TEMPERATURE_DISPLAY_UNITS, 0 );
homekit_characteristic_t current_state       = HOMEKIT_CHARACTERISTIC_( CURRENT_HEATING_COOLING_STATE, 0 );
homekit_characteristic_t target_state        = HOMEKIT_CHARACTERISTIC_( TARGET_HEATING_COOLING_STATE, 0, .callback=HOMEKIT_CHARACTERISTIC_CALLBACK(on_update) );
homekit_characteristic_t cooling_threshold   = HOMEKIT_CHARACTERISTIC_( COOLING_THRESHOLD_TEMPERATURE, 25, .callback=HOMEKIT_CHARACTERISTIC_CALLBACK(on_update) );
homekit_characteristic_t heating_threshold   = HOMEKIT_CHARACTERISTIC_( HEATING_THRESHOLD_TEMPERATURE, 15, .callback=HOMEKIT_CHARACTERISTIC_CALLBACK(on_update) );
homekit_characteristic_t current_humidity    = HOMEKIT_CHARACTERISTIC_( CURRENT_RELATIVE_HUMIDITY, 0 );



// LCD ssd1306 

static void ssd1306_task(void *pvParameters)
{
    char target_temp_string[20];
    char mode_string[20];
    char temperature_string[20];
    char humidity_string[20];
    int count =0;
    
    
    vTaskDelay(SECOND);
    ssd1306_set_whole_display_lighting(&display, false);
    
    ssd1306_load_xbm(&display, homekit_logo, display_buffer);
    if (ssd1306_load_frame_buffer(&display, display_buffer))
        goto error_loop;
    vTaskDelay(SECOND*5);
    
    ssd1306_clear_screen(&display);
    
    while (1) {
        if (count==0){
            if (ssd1306_fill_rectangle(&display, display_buffer, 0, 0, DISPLAY_WIDTH, DISPLAY_HEIGHT, OLED_COLOR_BLACK)){
                printf("Error printing rectangle\bn");
            }
            
            ssd1306_load_xbm(&display, thermostat_xbm, display_buffer);
            if (ssd1306_load_frame_buffer(&display, display_buffer))
                goto error_loop;
        }
        
        sprintf(target_temp_string, "%g", (float)target_temperature.value.float_value);
        switch( (int)current_state.value.int_value)
        {
            case 0:
                sprintf(mode_string, "OFF ");
                break;
            case 1:
                sprintf(mode_string, "HEAT");
                break;
            case 2:
                sprintf(mode_string, "COOL");
                break;
            case 3:
                sprintf(mode_string, "AUTO");
                break;
            default:
                sprintf(mode_string, "?   ");
        }
        
        //        sprintf(mode_string, "%i", (int)current_state.value.int_value);
        
        
        if (ssd1306_draw_string(&display, display_buffer, font_builtin_fonts[FONT_FACE_TERMINUS_BOLD_14X28_ISO8859_1], 5, 2, target_temp_string, OLED_COLOR_WHITE, OLED_COLOR_BLACK) < 1){
            printf("Error printing target temp\n");
        }
        
        if (ssd1306_draw_string(&display, display_buffer, font_builtin_fonts[FONT_FACE_TERMINUS_BOLD_14X28_ISO8859_1], 70, 2, mode_string, OLED_COLOR_WHITE, OLED_COLOR_BLACK) < 1 ){
            printf("Error printing mode\n");
        }
        
        sprintf(temperature_string, "%g", (float)current_temperature.value.float_value);
        sprintf(humidity_string, "%g", (float)current_humidity.value.float_value);
        if (ssd1306_draw_string(&display, display_buffer, font_builtin_fonts[FONT_FACE_TERMINUS_BOLD_8X14_ISO8859_1], 30, 41 , temperature_string, OLED_COLOR_WHITE, OLED_COLOR_BLACK) < 1){
            printf("Error printing temperature\n");
        }
        if (ssd1306_draw_string(&display, display_buffer, font_builtin_fonts[FONT_FACE_TERMINUS_BOLD_8X14_ISO8859_1], 92, 41 , humidity_string, OLED_COLOR_WHITE, OLED_COLOR_BLACK) < 1){
            printf("Error printing humidity\n");
        }
        
        count ++;
        if (count == 60){
            
            count = 0;
            if (ssd1306_fill_rectangle(&display, display_buffer, 0, 0, DISPLAY_WIDTH, DISPLAY_HEIGHT, OLED_COLOR_BLACK)){
                printf("Error printing rectangle\bn");
            }
            
            ssd1306_clear_screen(&display);
            ssd1306_load_xbm(&display, homekit_logo, display_buffer);
            if (ssd1306_load_frame_buffer(&display, display_buffer))
                goto error_loop;
            vTaskDelay(SECOND*5);
            ssd1306_clear_screen(&display);
        }
        
        
        if (ssd1306_load_frame_buffer(&display, display_buffer))
            goto error_loop;
        
        vTaskDelay(SECOND);
        
        
    }
    
error_loop:
    printf("%s: error while loading framebuffer into SSD1306\n", __func__);
    for (;;) {
        vTaskDelay(2 * SECOND);
        printf("%s: error loop\n", __FUNCTION__);
        led_code(LED_GPIO, FUNCTION_A);
    }
}


void screen_init(void)
{
    //uncomment to test with CPU overclocked
    //sdk_system_update_cpu_freq(160);


    printf("Screen Init SDK version:%s\n", sdk_system_get_sdk_version());

#ifdef I2C_CONNECTION
    i2c_init(I2C_BUS, SCL_PIN, SDA_PIN, I2C_FREQ_400K);
#endif

    while (ssd1306_init(&display) != 0) {
        printf("%s: failed to init SSD1306 lcd\n", __func__);
        vTaskDelay(SECOND);
    }
    ssd1306_set_whole_display_lighting(&display, false);
    ssd1306_set_scan_direction_fwd(&display, false);
    ssd1306_set_segment_remapping_enabled(&display, true);
}

// LCD ssd1306



// QR CODE
void display_draw_pixel(uint8_t x, uint8_t y, bool white) {
    ssd1306_color_t color = white ? OLED_COLOR_WHITE : OLED_COLOR_BLACK;
    ssd1306_draw_pixel(&display, display_buffer, x, y, color);
}

void display_draw_pixel_2x2(uint8_t x, uint8_t y, bool white) {
    ssd1306_color_t color = white ? OLED_COLOR_WHITE : OLED_COLOR_BLACK;
    
    ssd1306_draw_pixel(&display, display_buffer, x, y, color);
    ssd1306_draw_pixel(&display, display_buffer, x+1, y, color);
    ssd1306_draw_pixel(&display, display_buffer, x, y+1, color);
    ssd1306_draw_pixel(&display, display_buffer, x+1, y+1, color);
}

void display_draw_qrcode(QRCode *qrcode, uint8_t x, uint8_t y, uint8_t size) {
    void (*draw_pixel)(uint8_t x, uint8_t y, bool white) = display_draw_pixel;
    if (size >= 2) {
        draw_pixel = display_draw_pixel_2x2;
    }
    
    uint8_t cx;
    uint8_t cy = y;
    
    cx = x + size;
    draw_pixel(x, cy, 1);
    for (uint8_t i = 0; i < qrcode->size; i++, cx+=size)
        draw_pixel(cx, cy, 1);
    draw_pixel(cx, cy, 1);
    
    cy += size;
    
    for (uint8_t j = 0; j < qrcode->size; j++, cy+=size) {
        cx = x + size;
        draw_pixel(x, cy, 1);
        for (uint8_t i = 0; i < qrcode->size; i++, cx+=size) {
            draw_pixel(cx, cy, qrcode_getModule(qrcode, i, j)==0);
        }
        draw_pixel(cx, cy, 1);
    }
    
    cx = x + size;
    draw_pixel(x, cy, 1);
    for (uint8_t i = 0; i < qrcode->size; i++, cx+=size)
        draw_pixel(cx, cy, 1);
    draw_pixel(cx, cy, 1);
}

bool qrcode_shown = false;
void qrcode_show(homekit_server_config_t *config) {
    char setupURI[20];
    homekit_get_setup_uri(config, setupURI, sizeof(setupURI));

    printf ("setupURI: %s, password %s, setupId: %s\n",setupURI, config->password, config->setupId);
    QRCode qrcode;
    
    uint8_t *qrcodeBytes = malloc(qrcode_getBufferSize(QRCODE_VERSION));
    qrcode_initText(&qrcode, qrcodeBytes, QRCODE_VERSION, ECC_MEDIUM, setupURI);
    
    qrcode_print(&qrcode);  // print on console
    
    ssd1306_display_on(&display, true);
    
    ssd1306_fill_rectangle(&display, display_buffer, 0, 0, DISPLAY_WIDTH, DISPLAY_HEIGHT, OLED_COLOR_BLACK);
    ssd1306_draw_string(&display, display_buffer, font_builtin_fonts[FONT_FACE_TERMINUS_6X12_ISO8859_1], 0, 26, config->password, OLED_COLOR_WHITE, OLED_COLOR_BLACK);
    display_draw_qrcode(&qrcode, 64, 5, 2);
    
    ssd1306_load_frame_buffer(&display, display_buffer);
    
    free(qrcodeBytes);
    qrcode_shown = true;
}

void qrcode_hide() {
    if (!qrcode_shown)
        return;
    
    ssd1306_clear_screen(&display);
    ssd1306_display_on(&display, false);
    
    qrcode_shown = false;
}


// QR CODE END


void on_update(homekit_characteristic_t *ch, homekit_value_t value, void *context) {
    save_characteristic_to_flash (ch, value);
    process_setting_update();
}


void up_button_callback(button_event_t event, void* context) {
    switch (event) {
        case button_event_single_press:
            printf("Button UP\n");
            if (target_temperature.value.float_value <= (target_temperature.max_value[0] - 0.5))
            {
                target_temperature.value.float_value += 0.5;
                save_characteristic_to_flash (&target_temperature, target_temperature.value);
                homekit_characteristic_notify(&target_temperature, target_temperature.value);
            }
            break;
        case button_event_long_press:
            printf("Button UP\n");
            if (target_temperature.value.float_value <= (target_temperature.max_value[0] - 1))
            {
                target_temperature.value.float_value += 1;
                save_characteristic_to_flash (&target_temperature, target_temperature.value);
                homekit_characteristic_notify(&target_temperature, target_temperature.value);
            }
            break;
        default:
            printf("Unknown button event: %d\n", event);
    }
}


void down_button_callback(button_event_t event, void* context) {
    switch (event) {
        case button_event_single_press:
            printf("Button DOWN\n");
            if (target_temperature.value.float_value >= (target_temperature.min_value[0] + 0.5))
            {
                target_temperature.value.float_value -= 0.5;
                save_characteristic_to_flash (&target_temperature, target_temperature.value);
                homekit_characteristic_notify(&target_temperature, target_temperature.value);
            }
            break;
        case button_event_long_press:
            printf("Button UP\n");
            if (target_temperature.value.float_value >= (target_temperature.min_value[0] + 1))
            {
                target_temperature.value.float_value -= 1;
                save_characteristic_to_flash (&target_temperature, target_temperature.value);
                homekit_characteristic_notify(&target_temperature, target_temperature.value);
            }
            break;
        default:
            printf("Unknown button event: %d\n", event);
    }
}


void process_setting_update() {

    uint8_t state = target_state.value.int_value;
    if ((state == 1 && current_temperature.value.float_value < target_temperature.value.float_value) ||
            (state == 3 && current_temperature.value.float_value < heating_threshold.value.float_value)) {
        if (current_state.value.int_value != 1) {
            current_state.value = HOMEKIT_UINT8(1);
            save_characteristic_to_flash (&current_state, current_state.value);
            homekit_characteristic_notify(&current_state, current_state.value);

        }
    } else if ((state == 2 && current_temperature.value.float_value > target_temperature.value.float_value) ||
            (state == 3 && current_temperature.value.float_value > cooling_threshold.value.float_value)) {
        if (current_state.value.int_value != 2) {
            current_state.value = HOMEKIT_UINT8(2);
            save_characteristic_to_flash (&current_state, current_state.value);
            homekit_characteristic_notify(&current_state, current_state.value);
        }
    } else {
        if (current_state.value.int_value != 0) {
            current_state.value = HOMEKIT_UINT8(0);
            save_characteristic_to_flash (&current_state, current_state.value);
            homekit_characteristic_notify(&current_state, current_state.value);
        }
    }
}


void temperature_sensor_task(void *_args) {
    
    gpio_set_pullup(TEMPERATURE_SENSOR_PIN, false, false);
    
    float humidity_value, temperature_value;
    while (1) {
        bool success = dht_read_float_data(
                                           DHT_TYPE_DHT22, TEMPERATURE_SENSOR_PIN,
                                           &humidity_value, &temperature_value
                                           );
        
        if (success) {
            printf("Got readings: temperature %g, humidity %g\n", temperature_value, humidity_value);
            if (temperature_value >= current_temperature.min_value[0] && temperature_value <= current_temperature.max_value[0])
            {
                current_temperature.value = HOMEKIT_FLOAT(temperature_value);
                homekit_characteristic_notify(&current_temperature, current_temperature.value);
            }
            if (humidity_value >= current_humidity.min_value[0] && humidity_value <= current_humidity.max_value[0])
            {
                
                current_humidity.value = HOMEKIT_FLOAT(humidity_value);
                
                homekit_characteristic_notify(&current_humidity, current_humidity.value);
            }
            
            process_setting_update();
            
        } else {
            printf("Couldnt read data from sensor\n");
            led_code(LED_GPIO, SENSOR_ERROR);
        }
        homekit_characteristic_notify(&current_state, current_state.value);
        vTaskDelay(TEMPERATURE_POLL_PERIOD / portTICK_PERIOD_MS);
    }
}


homekit_accessory_t *accessories[] = {
    HOMEKIT_ACCESSORY(.id=1, .category=homekit_accessory_category_thermostat, .services=(homekit_service_t*[]) {
        HOMEKIT_SERVICE(ACCESSORY_INFORMATION, .characteristics=(homekit_characteristic_t*[]) {
            &name,
            &manufacturer,
            &serial,
            &model,
            &revision,
            HOMEKIT_CHARACTERISTIC(IDENTIFY, thermostat_identify),
            NULL
        }),
        HOMEKIT_SERVICE(THERMOSTAT, .primary=true, .characteristics=(homekit_characteristic_t*[]) {
            HOMEKIT_CHARACTERISTIC(NAME, "Thermostat"),
            &current_temperature,
            &target_temperature,
            &current_state,
            &target_state,
            &cooling_threshold,
            &heating_threshold,
            &units,
            &current_humidity,
            &ota_trigger,
            NULL
        }),
        NULL
    }),
    NULL
};

void reset_configuration_task() {

    led_code(LED_GPIO, WIFI_CONFIG_RESET);

//    printf("Resetting Wifi Config\n");

//    wifi_config_reset();

//    vTaskDelay(1000 / portTICK_PERIOD_MS);

    printf("Resetting HomeKit Config\n");

    homekit_server_reset();

    vTaskDelay(1000 / portTICK_PERIOD_MS);

    printf("Restarting\n");

    sdk_system_restart();

    vTaskDelete(NULL);
}

void reset_configuration() {
    printf("Resetting Sonoff configuration\n");
    xTaskCreate(reset_configuration_task, "Reset configuration", 256, NULL, 2, NULL);
}


void reset_button_callback(button_event_t event, void* context) {
    switch (event) {
        case button_event_single_press:
            printf("Button event: %d, doing nothin\n", event);
            break;
        case button_event_long_press:
            printf("Button event: %d, resetting homekit config\n", event);
            reset_configuration();
            break;
        default:
            printf("Unknown button event: %d\n", event);
    }
}

void thermostat_init() {
    
    int result;

    while (ssd1306_init(&display) != 0) {
        printf("%s: failed to init SSD1306 lcd\n", __func__);
        vTaskDelay(SECOND);
    }
    button_config_t up_button_config = BUTTON_CONFIG(
                                                  .active_level=button_active_low,
                                                  );
    button_config_t down_button_config = BUTTON_CONFIG(
                                                     .active_level=button_active_low,
                                                     );
    button_config_t reset_button_config = BUTTON_CONFIG(
                                                     .active_level=button_active_low,
                                                     );
    
    result = button_create(UP_BUTTON_GPIO, up_button_config, up_button_callback, NULL);
    if (result) {
        printf("Failed to initialize button Up, code %d\n", result);
    }
    
    result = button_create(DOWN_BUTTON_GPIO, down_button_config, down_button_callback, NULL);
    if (result) {
        printf("Failed to initialize button Down, code %d\n", result);
    }
    
    result = button_create(RESET_BUTTON_GPIO, reset_button_config, reset_button_callback, NULL);
    if (result) {
        printf("Failed to initialize button Reset, code %d\n", result);
    }
    
    xTaskCreate(temperature_sensor_task, "Thermostat", 256, NULL, 2, NULL);

    gpio_enable(LED_GPIO, GPIO_OUTPUT);
    
    xTaskCreate(ssd1306_task, "ssd1306_task", 512, NULL, 2, NULL);

}

void create_accessory_name() {

    int serialLength = snprintf(NULL, 0, "%d", sdk_system_get_chip_id());

    char *serialNumberValue = malloc(serialLength + 1);

    snprintf(serialNumberValue, serialLength + 1, "%d", sdk_system_get_chip_id());
    
    int name_len = snprintf(NULL, 0, "%s-%s-%s",
				DEVICE_NAME,
				DEVICE_MODEL,
				serialNumberValue);

    if (name_len > 63) {
        name_len = 63;
    }

    char *name_value = malloc(name_len + 1);

    snprintf(name_value, name_len + 1, "%s-%s-%s",
		 DEVICE_NAME, DEVICE_MODEL, serialNumberValue);

   
    name.value = HOMEKIT_STRING(name_value);
    serial.value = name.value;
}

void load_settings_from_flash (){
    
    printf("load_settings_from_flash - load setting from flash\n");
    load_characteristic_from_flash (&target_state);
    load_characteristic_from_flash (&target_temperature);
}


void on_homekit_event(homekit_event_t event) {
    if (event == HOMEKIT_EVENT_PAIRING_ADDED) {
        qrcode_hide();
        thermostat_init();
    } else if (event == HOMEKIT_EVENT_PAIRING_REMOVED) {
        if (!homekit_is_paired())
            sdk_system_restart();
    }
}
homekit_server_config_t config = {
    .accessories = accessories,
    .password = "111-11-111",
    .setupId = "1234",
    .on_event = on_homekit_event
};

void user_init(void) {
    uart_set_baud(0, 115200);

//    wifi_init();

    get_sysparam_info ();
    load_settings_from_flash ();

    create_accessory_name(); 



    printf ("Calling screen init\n");
    printf ("fonts count %i\n", font_builtin_fonts_count);
    screen_init();
    printf ("Screen init called\n");
    int c_hash=ota_read_sysparam(&manufacturer.value.string_value,&serial.value.string_value,
                                      &model.value.string_value,&revision.value.string_value);
    if (c_hash==0) c_hash=1;
        config.accessories[0]->config_number=c_hash;
 
    if (!homekit_is_paired()) {
        qrcode_show(&config);
    } else
    {
        thermostat_init();
    }
    homekit_server_init(&config);
}


