/* Hello World YYK

   This YYK code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/
#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_system.h"
#include "esp_log.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "esp_timer.h"

#include "mqtt_client.h"
#include "tcpip_adapter.h"
#include "lwip/sockets.h"
#include "lwip/dns.h"
#include "lwip/netdb.h"

#include "driver/gpio.h"
#include "include/ir_decode.h"
#include "include/cJSON.h"

/* The event group allows multiple bits for each event, but we only care about two events:
 * - we are connected to the AP with an IP
 * - we failed to connect after the maximum amount of retries */

#define GPIO_LED_NUM 2

#define ESP8266
#define ON 1
#define OFF 0

static int LED_STATE = OFF;

static esp_err_t mqtt_event_handler(esp_mqtt_event_handle_t event);

void wifi_init(void);
void ir_init(void);
void ac_ctrl(cJSON *state);

void RGB_LED_Ctrl(int LED_Ctrl)
{
    LED_STATE = LED_Ctrl;
    gpio_set_level(GPIO_LED_NUM, LED_Ctrl);
}

static esp_err_t mqtt_event_handler(esp_mqtt_event_handle_t event)
{
    char mag_data[100];
    // 获取MQTT客户端结构体指针
    esp_mqtt_client_handle_t client = event->client;

    // 通过事件ID来分别处理对应的事件
    switch (event->event_id)
    {
    // 建立连接成功
    case MQTT_EVENT_CONNECTED:
        printf("MQTT_client cnnnect to EMQ ok. \n");
        // 发布主题，主题消息为“I am ESP32.”,自动计算有效载荷，qos=1
        esp_mqtt_client_publish(client, "ir_8266_msg", "I am ESP8266.", 0, 0, 0);
        // 订阅主题，qos=0
        esp_mqtt_client_subscribe(client, "ir_8266_ctrl", 0);
        break;
    // 客户端断开连接
    case MQTT_EVENT_DISCONNECTED:
        printf("MQTT_client have disconnected. \n");
        break;
    // 主题订阅成功
    case MQTT_EVENT_SUBSCRIBED:
        printf("mqtt subscribe ok. msg_id = %d \n", event->msg_id);
        break;
    // 取消订阅
    case MQTT_EVENT_UNSUBSCRIBED:
        printf("mqtt unsubscribe ok. msg_id = %d \n", event->msg_id);
        break;
    // 主题发布成功
    case MQTT_EVENT_PUBLISHED:
        printf("mqtt published ok. msg_id = %d \n", event->msg_id);
        break;
    // 已收到订阅的主题消息
    case MQTT_EVENT_DATA:
        printf("mqtt received topic: %.*s \n", event->topic_len, event->topic);
        printf("topic data: [%.*s]\r\n", event->data_len, event->data);
        sprintf(mag_data, "%.*s", event->data_len, event->data);
    /*  {"item":1    //0: esp8266上的灯，1: 空调，2: 灯，3: 门
        "state":
        {   "power":1    //0: 关，1: 开
            "mode":0     //模式
            "temp":26    //温度
            "wind_dir":0 //扫风
            "wid_speed":0//风速
        } 
        }*/
        printf("json_msg\n");
        cJSON *json_mag = cJSON_Parse(mag_data);
        if (cJSON_GetArraySize(json_mag)==0)
        {
            esp_mqtt_client_publish(client, "ir_8266_msg", "{\"err\":100,\"message\":\"data can not be NULL\"}", 0, 0, 0);
        }
        else
        {
            int item = cJSON_GetObjectItem(json_mag, "item")->valueint;
            cJSON *json_state = cJSON_GetObjectItem(json_mag, "state");
            int power = cJSON_GetObjectItem(json_state, "power")->valueint;
            if(item==0){if (power) {RGB_LED_Ctrl(OFF);}  else {RGB_LED_Ctrl(ON);}}
            else if(item==1){ac_ctrl(json_state);}
            else if(item==2){printf("2\n");}
            else if(item==3){printf("3\n");}
            cJSON_Delete(json_state);
        }
        cJSON_Delete(json_mag);
        break;
    // 客户端遇到错误
    case MQTT_EVENT_ERROR:
        printf("MQTT_EVENT_ERROR \n");
        break;
    case MQTT_EVENT_ANY:
        printf("MQTT_EVENT_ANY \n");
        break;
    case MQTT_EVENT_BEFORE_CONNECT:
        printf("MQTT_EVENT_BEFORE_CONNECT\n");
        break;
    }
    return ESP_OK;
}

void LED_init(void)
{
    /* 定义一个gpio配置结构体 */
    gpio_config_t gpio_config_structure;

    /* 初始化gpio配置结构体*/
    gpio_config_structure.pin_bit_mask = (1ULL << GPIO_LED_NUM); /* 选择gpio15 */
    gpio_config_structure.mode = GPIO_MODE_OUTPUT;               /* 输出模式 */
    gpio_config_structure.pull_up_en = 0;                        /* 不上拉 */
    gpio_config_structure.pull_down_en = 0;                      /* 不下拉 */
    gpio_config_structure.intr_type = GPIO_INTR_DISABLE;         /* 禁止中断 */

    /* 根据设定参数初始化并使能 */
    gpio_config(&gpio_config_structure);
}

void app_main()
{
    wifi_init();
    LED_init();
    ir_init();
    // 1、定义一个MQTT客户端配置结构体，输入MQTT的url和MQTT事件处理函数
    esp_mqtt_client_config_t mqtt_cfg = {
        .host = "host.mqtt.aliyuncs.com",
        .client_id = "",
        .username = "",
        .password = "",
        .event_handle = mqtt_event_handler,
    };

    // 2、通过esp_mqtt_client_init获取一个MQTT客户端结构体指针，参数是MQTT客户端配置结构体
    esp_mqtt_client_handle_t client = esp_mqtt_client_init(&mqtt_cfg);

    // 3、开启MQTT
    esp_mqtt_client_start(client);
}