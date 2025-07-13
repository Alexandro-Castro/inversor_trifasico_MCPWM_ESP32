#include "esp_err.h"
#include "esp_log.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/i2c.h"
#include "rom/ets_sys.h"

static esp_err_t i2c_master_init(void);

void lcd_init (void);   // initialize lcd

void lcd_send_cmd (char cmd);  // send command to the lcd

void lcd_send_data (char data);  // send data to the lcd

void lcd_send_string (char *str);  // send string to the lcd

void lcd_set_cursor(int row, int col);  // put cursor at the entered position row (0 or 1), col (0-15);

void lcd_clear (void);
