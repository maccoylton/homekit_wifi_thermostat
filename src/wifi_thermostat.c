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

#include <ssd1306/ssd1306.h>

#include <homekit/homekit.h>
#include <homekit/characteristics.h>
//#include "wifi.h"

#include <dht/dht.h>

#include "wifi_thermostat.h"
#include "button.h"

#define TEMPERATURE_SENSOR_PIN 4
#define TEMPERATURE_POLL_PERIOD 10000
#define BUTTON_UP_GPIO 12
#define BUTTON_DOWN_GPIO 13

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
static const ssd1306_t dev = {
    .protocol = PROTOCOL,
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

/* Local frame buffer */
static uint8_t buffer[DISPLAY_WIDTH * DISPLAY_HEIGHT / 8];

#define SECOND (1000 / portTICK_PERIOD_MS)

// I2C



#include "ota-api.h"
homekit_characteristic_t ota_trigger  = API_OTA_TRIGGER;
homekit_characteristic_t name         = HOMEKIT_CHARACTERISTIC_(NAME, DEVICE_NAME);
homekit_characteristic_t manufacturer = HOMEKIT_CHARACTERISTIC_(MANUFACTURER,  DEVICE_MANUFACTURER);
homekit_characteristic_t serial       = HOMEKIT_CHARACTERISTIC_(SERIAL_NUMBER, DEVICE_SERIAL);
homekit_characteristic_t model        = HOMEKIT_CHARACTERISTIC_(MODEL,         DEVICE_MODEL);
homekit_characteristic_t revision     = HOMEKIT_CHARACTERISTIC_(FIRMWARE_REVISION,  FW_VERSION);


void thermostat_identify(homekit_value_t _value) {
    printf("Thermostat identify\n");
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
    ssd1306_set_whole_display_lighting(&dev, false);

    ssd1306_load_xbm(&dev, homekit_logo, buffer);
    if (ssd1306_load_frame_buffer(&dev, buffer))
            goto error_loop;
    vTaskDelay(SECOND*5);

    ssd1306_clear_screen(&dev);

    while (1) {
        if (ssd1306_fill_rectangle(&dev, buffer, 0, 0, DISPLAY_WIDTH, DISPLAY_HEIGHT, OLED_COLOR_BLACK)){
		printf("Error printing rectangle\bn");
	}


        sprintf(target_temp_string, "Target: %g", (float)target_temperature.value.float_value);
        sprintf(mode_string, "Mode: %i", (int)current_state.value.int_value);
//	ssd1306_draw_string(&dev, buffer, font_builtin_fonts[0], 0, 0, "Hello", OLED_COLOR_WHITE, OLED_COLOR_BLACK)



        if (ssd1306_draw_string(&dev, buffer, font_builtin_fonts[0], 0, 30, target_temp_string, OLED_COLOR_WHITE, OLED_COLOR_BLACK) < 1){
            printf("Error printing target temp\n");
	}

        if (ssd1306_draw_string(&dev, buffer, font_builtin_fonts[0], 0, 45, mode_string, OLED_COLOR_WHITE, OLED_COLOR_BLACK) < 1 ){
            printf("Error printing mode\n");
	}

        sprintf(temperature_string, "Temperature: %g", (float)current_temperature.value.float_value);
        sprintf(humidity_string, "Humidity: %g", (float)current_humidity.value.float_value);
        if (ssd1306_draw_string(&dev, buffer, font_builtin_fonts[0], 0, 0, temperature_string, OLED_COLOR_WHITE, OLED_COLOR_BLACK) < 1){
            printf("Error printing temperature\n");
	}
        if (ssd1306_draw_string(&dev, buffer, font_builtin_fonts[0], 0, 15, humidity_string, OLED_COLOR_WHITE, OLED_COLOR_BLACK) < 1){
            printf("Error printing humidity\n");
	}

	count ++;
	if (count == 60){

		count = 0;

                ssd1306_clear_screen(&dev);
		ssd1306_load_xbm(&dev, homekit_logo, buffer);
    		if (ssd1306_load_frame_buffer(&dev, buffer))
            		goto error_loop;
    		vTaskDelay(SECOND*5);
    		ssd1306_clear_screen(&dev);
	}

		
        if (ssd1306_load_frame_buffer(&dev, buffer))
            goto error_loop;
        
        vTaskDelay(SECOND);


    }

    error_loop:
        printf("%s: error while loading framebuffer into SSD1306\n", __func__);
        for (;;) {
            vTaskDelay(2 * SECOND);
            printf("%s: error loop\n", __FUNCTION__);
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

    while (ssd1306_init(&dev) != 0) {
        printf("%s: failed to init SSD1306 lcd\n", __func__);
        vTaskDelay(SECOND);
    }
    ssd1306_set_whole_display_lighting(&dev, true);

    xTaskCreate(ssd1306_task, "ssd1306_task", 512, NULL, 2, NULL);

}

// LCD ssd1306

void on_update(homekit_characteristic_t *ch, homekit_value_t value, void *context) {
    process_setting_update();
}


void button_up_callback(uint8_t gpio, button_event_t event) {
    switch (event) {
        case button_event_single_press:
            printf("Button UP\n");
	    target_temperature.value.float_value += 0.5;
            homekit_characteristic_notify(&target_temperature, target_temperature.value);
            break;
        case button_event_long_press:
            printf("Button UP\n");
            target_temperature.value.float_value += 1;
            homekit_characteristic_notify(&target_temperature, target_temperature.value);
            break;
        default:
            printf("Unknown button event: %d\n", event);
    }
}


void button_down_callback(uint8_t gpio, button_event_t event) {
    switch (event) {
        case button_event_single_press:
            printf("Button DOWN\n");
            target_temperature.value.float_value -= 0.5;
            homekit_characteristic_notify(&target_temperature, target_temperature.value);
            break;
        case button_event_long_press:
            printf("Button UP\n");
            target_temperature.value.float_value -= 1;
            homekit_characteristic_notify(&target_temperature, target_temperature.value);
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
            homekit_characteristic_notify(&current_state, current_state.value);

        }
    } else if ((state == 2 && current_temperature.value.float_value > target_temperature.value.float_value) ||
            (state == 3 && current_temperature.value.float_value > cooling_threshold.value.float_value)) {
        if (current_state.value.int_value != 2) {
            current_state.value = HOMEKIT_UINT8(2);
            homekit_characteristic_notify(&current_state, current_state.value);

        }
    } else {
        if (current_state.value.int_value != 0) {
            current_state.value = HOMEKIT_UINT8(0);
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
            current_temperature.value = HOMEKIT_FLOAT(temperature_value);
            current_humidity.value = HOMEKIT_FLOAT(humidity_value);

            homekit_characteristic_notify(&current_temperature, current_temperature.value);
            homekit_characteristic_notify(&current_humidity, current_humidity.value);

            process_setting_update();

        } else {
            printf("Couldnt read data from sensor\n");
        }
        homekit_characteristic_notify(&current_state, current_state.value);
        vTaskDelay(TEMPERATURE_POLL_PERIOD / portTICK_PERIOD_MS);
    }
}

void thermostat_init() {

    xTaskCreate(temperature_sensor_task, "Thermostat", 256, NULL, 2, NULL);
    gpio_enable(BUTTON_UP_GPIO, GPIO_INPUT);
    gpio_enable(BUTTON_DOWN_GPIO, GPIO_INPUT);
    if (button_create(BUTTON_UP_GPIO, 0, 4000, button_up_callback)) {
        printf("Failed to initialize button Up\n");
    }

   if (button_create(BUTTON_DOWN_GPIO, 0, 4000, button_down_callback)) {
        printf("Failed to initialize button down\n");
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


homekit_server_config_t config = {
    .accessories = accessories,
    .password = "111-11-111"
};

void user_init(void) {
    uart_set_baud(0, 115200);

//    wifi_init();
    create_accessory_name(); 

    thermostat_init();

    printf ("Calling screen init\n");
    printf ("fonts count %i\n", font_builtin_fonts_count);
    screen_init();
    printf ("Screen init called\n");
    int c_hash=ota_read_sysparam(&manufacturer.value.string_value,&serial.value.string_value,
                                      &model.value.string_value,&revision.value.string_value);
    if (c_hash==0) c_hash=1;
        config.accessories[0]->config_number=c_hash;

    homekit_server_init(&config);
}

