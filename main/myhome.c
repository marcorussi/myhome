/*
 * The MIT License (MIT)
 *
 * Copyright (c) [2017] [Marco Russi]
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
*/


#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <unistd.h>
#include <limits.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "freertos/queue.h"

#include "nvs_flash.h"

#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event_loop.h"
#include "esp_log.h"
#include "esp_vfs_fat.h"

#include "driver/sdmmc_host.h"
#include "driver/uart.h"

#include "soc/uart_struct.h"

#include "aws_iot_config.h"
#include "aws_iot_log.h"
#include "aws_iot_version.h"
#include "aws_iot_mqtt_client_interface.h"




/* Enable or disable log info over UART0 channel */
//#define ENABLE_UART0_LOG


#ifdef ENABLE_UART0_LOG
/**
 * - port: UART0
 * - rx buffer: on
 * - tx buffer: on
 * - flow control: off
 * - event queue: off
 * - pin assignment: txd(default), rxd(default)
 */
#define LOG_UART_NUM						UART_NUM_0

/* log UART buffer size */
#define LOG_BUF_SIZE 					100
#endif

/**
 * - port: UART2
 * - rx buffer: on
 * - tx buffer: on
 * - flow control: off
 * - event queue: on
 * - pin assignment: see below
 */
#define DATA_UART_NUM					UART_NUM_2

/* data UART pins numbers */
#define DATA_UART_TX_PIN				17
#define DATA_UART_RX_PIN				16

/* data UART buffer size */
#define DATA_BUF_SIZE 					100

#ifdef ENABLE_UART0_LOG
/* log tag */
static const char *TAG = "myhome";
#endif

/* WiFi SSID and password */
#define EXAMPLE_WIFI_SSID CONFIG_WIFI_SSID
#define EXAMPLE_WIFI_PASS CONFIG_WIFI_PASSWORD


/* FreeRTOS event group to signal when we are connected & ready to make a request */
static EventGroupHandle_t wifi_event_group;


/* The event group allows multiple bits for each event,
   but we only care about one event - are we connected
   to the AP with an IP? */
const int CONNECTED_BIT = BIT0;


/* CA Root certificate, device ("Thing") certificate and device
 * ("Thing") key.
*/
extern const uint8_t aws_root_ca_pem_start[] asm("_binary_aws_root_ca_pem_start");
extern const uint8_t aws_root_ca_pem_end[] asm("_binary_aws_root_ca_pem_end");
extern const uint8_t certificate_pem_crt_start[] asm("_binary_certificate_pem_crt_start");
extern const uint8_t certificate_pem_crt_end[] asm("_binary_certificate_pem_crt_end");
extern const uint8_t private_pem_key_start[] asm("_binary_private_pem_key_start");
extern const uint8_t private_pem_key_end[] asm("_binary_private_pem_key_end");


/* Default MQTT HOST URL is pulled from the aws_iot_config.h */
char HostAddress[255] = AWS_IOT_MQTT_HOST;


/* Default MQTT port is pulled from the aws_iot_config.h */
uint32_t port = AWS_IOT_MQTT_PORT;


/* UART 1 queue */
static QueueHandle_t uart1_queue;




/* function callack for WiFi events */
static esp_err_t event_handler(void *ctx, system_event_t *event)
{
	switch(event->event_id) 
	{
		case SYSTEM_EVENT_STA_START:
		{
			/* try to connect */
			esp_wifi_connect();
#ifdef ENABLE_UART0_LOG
			ESP_LOGI(TAG, "Start WiFi connection...");
#endif
		}
   	break;
		case SYSTEM_EVENT_STA_GOT_IP:
		{
			/* we are connected and got an IP address */
        	xEventGroupSetBits(wifi_event_group, CONNECTED_BIT);
#ifdef ENABLE_UART0_LOG
			ESP_LOGI(TAG, "Connected to WiFi! Got an IP address.");
#endif
		}
     	break;
    	case SYSTEM_EVENT_STA_DISCONNECTED:
		{
			/* we are disconnected */
        	/* This is a workaround as ESP32 WiFi libs don't currently
        		auto-reassociate. */
    		esp_wifi_connect();
        	xEventGroupClearBits(wifi_event_group, CONNECTED_BIT);
#ifdef ENABLE_UART0_LOG
			ESP_LOGI(TAG, "Disconnected from WiFi :(");
#endif
		}
  		break;
    	default:
		{
   		break;
		}
	}
	return ESP_OK;
}




/* function callack for received data from subscribed topics */
void iot_subscribe_callback_handler(AWS_IoT_Client *pClient, char *topicName, uint16_t topicNameLen,
                                    IoT_Publish_Message_Params *params, void *pData) 
{
	char uart_data_out[DATA_BUF_SIZE+20];
	char payload_string[DATA_BUF_SIZE+1];

#ifdef ENABLE_UART0_LOG
	ESP_LOGI(TAG, "Subscribe callback");
	ESP_LOGI(TAG, "%.*s\t%.*s", topicNameLen, topicName, (int)params->payloadLen, (char *)params->payload);
#endif

	/* TODO: check payload length! */

	memcpy(payload_string, (char *)params->payload, (int)params->payloadLen);
	payload_string[((int)params->payloadLen-1)] = '\0';
	/* prepare JSON data string */
	sprintf(uart_data_out, "{\"command\":[%s]}", payload_string);
	/* send data through UART */
	uart_write_bytes(DATA_UART_NUM, (const char*)uart_data_out, strlen(uart_data_out));
}




/* function callback for disconnection */
void disconnectCallbackHandler(AWS_IoT_Client *pClient, void *data) 
{
	char msg[20];

#ifdef ENABLE_UART0_LOG
	ESP_LOGW(TAG, "MQTT Disconnect");
#endif
	IoT_Error_t rc = FAILURE;

	/* send "error" through data UART channel */
	sprintf(msg, "{\"config\":\"%s\"}", "error");
	uart_write_bytes(DATA_UART_NUM, (const char*)msg, strlen(msg));

	if(NULL == pClient) 
	{
		return;
	}

	/* if autoreconnect is enabled */
	if(aws_iot_is_autoreconnect_enabled(pClient)) 
	{
#ifdef ENABLE_UART0_LOG
		ESP_LOGI(TAG, "Auto Reconnect is enabled, Reconnecting attempt will start now");
#endif
	} 
	else 
	{
#ifdef ENABLE_UART0_LOG
		ESP_LOGW(TAG, "Auto Reconnect not enabled. Starting manual reconnect...");
#endif
		/* try to reconnect */
		rc = aws_iot_mqtt_attempt_reconnect(pClient);
		if(NETWORK_RECONNECTED == rc) 
		{
#ifdef ENABLE_UART0_LOG
			ESP_LOGW(TAG, "Manual Reconnect Successful");
#endif
			/* send "ready" through data UART channel */
			sprintf(msg, "{\"config\":\"%s\"}", "ready");
			uart_write_bytes(DATA_UART_NUM, (const char*)msg, strlen(msg));
		} 
		else 
		{
#ifdef ENABLE_UART0_LOG
			ESP_LOGW(TAG, "Manual Reconnect Failed - %d", rc);
#endif
		}
	}
}




/* AWS task */
void aws_iot_task(void *param) 
{
	char cPayload[DATA_BUF_SIZE+20];	/* consider 20 more characters for string formatting as below */
	uint8_t uart_data_in[DATA_BUF_SIZE];
	char msg[20];

	const char *TOPIC_SUB = "myhome/command";
	const int TOPIC_SUB_LEN = strlen(TOPIC_SUB);
	const char *TOPIC_PUB = "myhome/monitor";
	const int TOPIC_PUB_LEN = strlen(TOPIC_PUB);
	const char *TOPIC_KEEP_ALIVE = "myhome/keepalive";
	const int TOPIC_KEEP_ALIVE_LEN = strlen(TOPIC_KEEP_ALIVE);

	IoT_Error_t rc = FAILURE;

	AWS_IoT_Client client;
	IoT_Client_Init_Params mqttInitParams = iotClientInitParamsDefault;
	IoT_Client_Connect_Params connectParams = iotClientConnectParamsDefault;

	IoT_Publish_Message_Params paramsQOS0;
#if 0
	IoT_Publish_Message_Params paramsQOS1;
#endif
#ifdef ENABLE_UART0_LOG
	ESP_LOGI(TAG, "AWS IoT SDK Version %d.%d.%d-%s", VERSION_MAJOR, VERSION_MINOR, VERSION_PATCH, VERSION_TAG);
#endif
	mqttInitParams.enableAutoReconnect = false; /* We enable this later below */
	mqttInitParams.pHostURL = HostAddress;
	mqttInitParams.port = port;

	mqttInitParams.pRootCALocation = (const char *)aws_root_ca_pem_start;
	mqttInitParams.pDeviceCertLocation = (const char *)certificate_pem_crt_start;
	mqttInitParams.pDevicePrivateKeyLocation = (const char *)private_pem_key_start;

	mqttInitParams.mqttCommandTimeout_ms = 60000;	/* 1 minute */
	mqttInitParams.tlsHandshakeTimeout_ms = 5000;
	mqttInitParams.isSSLHostnameVerify = true;
	mqttInitParams.disconnectHandler = disconnectCallbackHandler;
	mqttInitParams.disconnectHandlerData = NULL;

	rc = aws_iot_mqtt_init(&client, &mqttInitParams);
	if(SUCCESS != rc) 
	{
#ifdef ENABLE_UART0_LOG
		ESP_LOGE(TAG, "aws_iot_mqtt_init returned error : %d ", rc);
#endif
		abort();
	}

	/* Wait for WiFI to show as connected */
	xEventGroupWaitBits(wifi_event_group, CONNECTED_BIT, false, true, portMAX_DELAY);

	connectParams.keepAliveIntervalInSec = 10;
	connectParams.isCleanSession = true;
	connectParams.MQTTVersion = MQTT_3_1_1;
	/* Client ID is set in the menuconfig of the example */
	connectParams.pClientID = CONFIG_AWS_EXAMPLE_CLIENT_ID;
	connectParams.clientIDLen = (uint16_t) strlen(CONFIG_AWS_EXAMPLE_CLIENT_ID);
	connectParams.isWillMsgPresent = false;

	/* try to connect to AWS until success */
#ifdef ENABLE_UART0_LOG
	ESP_LOGI(TAG, "Connecting to AWS...");
#endif
	do 
	{
		rc = aws_iot_mqtt_connect(&client, &connectParams);
		if(SUCCESS != rc) 
		{
#ifdef ENABLE_UART0_LOG
			ESP_LOGE(TAG, "Error(%d) connecting to %s:%d. Trying again...", rc, mqttInitParams.pHostURL, mqttInitParams.port);
#endif
			/* wait for a while before trying again... */
			vTaskDelay(1000 / portTICK_RATE_MS);
		}
	} while(SUCCESS != rc);

	/*
	* Enable Auto Reconnect functionality. Minimum and Maximum time of Exponential backoff are set in aws_iot_config.h
	*  #AWS_IOT_MQTT_MIN_RECONNECT_WAIT_INTERVAL
	*  #AWS_IOT_MQTT_MAX_RECONNECT_WAIT_INTERVAL
	*/
	rc = aws_iot_mqtt_autoreconnect_set_status(&client, true);
	if(SUCCESS != rc) 
	{
#ifdef ENABLE_UART0_LOG
		ESP_LOGE(TAG, "Unable to set Auto Reconnect to true - %d", rc);
#endif
		abort();
	}

#ifdef ENABLE_UART0_LOG
	ESP_LOGI(TAG, "Subscribing...");
#endif
	rc = aws_iot_mqtt_subscribe(&client, TOPIC_SUB, TOPIC_SUB_LEN, QOS0, iot_subscribe_callback_handler, NULL);
	if(SUCCESS == rc) 
	{
#ifdef ENABLE_UART0_LOG
		ESP_LOGI(TAG, "Successfully subscribed!");
#endif
	}
	else
	{
#ifdef ENABLE_UART0_LOG
		ESP_LOGE(TAG, "Error subscribing : %d ", rc);
#endif
		abort();
	}

	paramsQOS0.qos = QOS0;
	paramsQOS0.payload = (void *)cPayload;
	paramsQOS0.isRetained = 0;

#if 0
	paramsQOS1.qos = QOS1;
	paramsQOS1.payload = (void *) cPayload;
	paramsQOS1.isRetained = 0;
#endif

	/* send "ready" through data UART channel */
	sprintf(msg, "{\"config\":\"%s\"}", "ready");
	uart_write_bytes(DATA_UART_NUM, (const char*)msg, strlen(msg));

	/* loop for sending data until following conditions are met; it should never exit from here... */
	while((NETWORK_ATTEMPTING_RECONNECT == rc || NETWORK_RECONNECTED == rc || SUCCESS == rc)) 
	{
		/* Max time the yield function will wait for read messages */
		rc = aws_iot_mqtt_yield(&client, 100);
		if(NETWORK_ATTEMPTING_RECONNECT == rc) 
		{
   		/* If the client is attempting to reconnect we will skip the rest of the loop */
   		continue;
		}
#ifdef ENABLE_UART0_LOG
		ESP_LOGI(TAG, "-->sleep");
#endif

		/* wait for a while */
		vTaskDelay(5000 / portTICK_RATE_MS);

		/* clear UART data buffer */
		memset(uart_data_in, 0, 20);
		/* get any received data */
		int len = uart_read_bytes(DATA_UART_NUM, uart_data_in, DATA_BUF_SIZE, 1000 / portTICK_RATE_MS);//);
		/* if data are received and special character is found at the end */
		if((len > 0) && (uart_data_in[len-1] == '.')) 
		{
#ifdef ENABLE_UART0_LOG
			ESP_LOGI(TAG, "--> len: %d --> data: %c", len, uart_data_in[len-1]);
#endif
			/* terminate string */
			uart_data_in[len-1] = '\0';
			/* prepare JSON data string */
			sprintf(cPayload, "{\"monitor\":[%s]}", uart_data_in);
			/* prepare message length */
			paramsQOS0.payloadLen = strlen(cPayload);	
			/* publish the message! */
			rc = aws_iot_mqtt_publish(&client, TOPIC_PUB, TOPIC_PUB_LEN, &paramsQOS0);

#ifdef ENABLE_UART0_LOG
			uart_write_bytes(LOG_UART_NUM, (const char*)cPayload, paramsQOS0.payloadLen);
#endif
		}
		else
		{
			/* send a dummy message as keep alive payload */
			sprintf(cPayload, "keep_alive");
			paramsQOS0.payloadLen = strlen(cPayload);
			rc = aws_iot_mqtt_publish(&client, TOPIC_KEEP_ALIVE, TOPIC_KEEP_ALIVE_LEN, &paramsQOS0);
		}
#if 0
		paramsQOS1.payloadLen = strlen(cPayload);
		rc = aws_iot_mqtt_publish(&client, TOPIC_PUB, TOPIC_PUB_LEN, &paramsQOS1);
		if (rc == MQTT_REQUEST_TIMEOUT_ERROR) 
		{
			ESP_LOGW(TAG, "QOS1 publish ack not received.");
			rc = SUCCESS;
		}
#endif
    }
#ifdef ENABLE_UART0_LOG
	ESP_LOGE(TAG, "An error occurred in the main loop.");
#endif

	/* send "error" through data UART channel */
	sprintf(msg, "{\"config\":\"%s\"}", "error");
	uart_write_bytes(DATA_UART_NUM, (const char*)msg, strlen(msg));

	abort();
}




/* UART event task */
static void uart_event_task(void *pvParameters)
{
	uart_event_t event;
#ifdef ENABLE_UART0_LOG
	size_t buffered_size;
#endif

	/* infinite task loop */
	for(;;) 
	{
		/* Waiting for UART event */
		if(xQueueReceive(uart1_queue, (void * )&event, (portTickType)portMAX_DELAY)) 
		{
#ifdef ENABLE_UART0_LOG
			ESP_LOGI(TAG, "uart[%d] event:", DATA_UART_NUM);
#endif
			switch(event.type) 
			{
				/* Event of UART receving data */
				case UART_DATA:
				{
#ifdef ENABLE_UART0_LOG
					/* data are processed in the mqqtt task. log purpose only */
					uart_get_buffered_data_len(DATA_UART_NUM, &buffered_size);
					ESP_LOGI(TAG, "data, len: %d; buffered len: %d", event.size, buffered_size);
#endif
				}
				break;
				/* Event of HW FIFO overflow detected */
				case UART_FIFO_OVF:
				{
#ifdef ENABLE_UART0_LOG
					ESP_LOGI(TAG, "hw fifo overflow\n");
#endif
					/* this should never happen in this application; flush data */
					uart_flush(DATA_UART_NUM);
				}
				break;
				/* Event of UART ring buffer full */
				case UART_BUFFER_FULL:
				{
#ifdef ENABLE_UART0_LOG
					ESP_LOGI(TAG, "ring buffer full\n");
#endif
					/* this should never happen in this application; flush data */
					uart_flush(DATA_UART_NUM);
				}
				break;
#ifdef ENABLE_UART0_LOG
				/* Event of UART RX break detected */
				case UART_BREAK:
				{
					ESP_LOGI(TAG, "uart rx break\n");
				}
				break;
				/* Event of UART parity check error */
				case UART_PARITY_ERR:
				{
					ESP_LOGI(TAG, "uart parity error\n");
				}
				break;
				/* Event of UART frame error */
				case UART_FRAME_ERR:
				{
					ESP_LOGI(TAG, "uart frame error\n");
				}
				break;
				/* UART_PATTERN_DET */
				case UART_PATTERN_DET:
				{
					ESP_LOGI(TAG, "uart pattern detected\n");	
				}
				break;
#endif
				/* Others */
				default:
				{
#ifdef ENABLE_UART0_LOG
					ESP_LOGI(TAG, "uart event type: %d\n", event.type);
#endif
				}
				break;
			}
		}
	}

	/* delete task */
	vTaskDelete(NULL);
}




/* function to init WiFi */
static void initialise_wifi(void)
{
    tcpip_adapter_init();

    wifi_event_group = xEventGroupCreate();

    ESP_ERROR_CHECK( esp_event_loop_init(event_handler, NULL) );

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();

    ESP_ERROR_CHECK( esp_wifi_init(&cfg) );

    ESP_ERROR_CHECK( esp_wifi_set_storage(WIFI_STORAGE_RAM) );

    wifi_config_t wifi_config = {
        .sta = {
            .ssid = EXAMPLE_WIFI_SSID,
            .password = EXAMPLE_WIFI_PASS,
        },
    };
#ifdef ENABLE_UART0_LOG
    ESP_LOGI(TAG, "Setting WiFi configuration SSID %s...", wifi_config.sta.ssid);
#endif
    ESP_ERROR_CHECK( esp_wifi_set_mode(WIFI_MODE_STA) );

    ESP_ERROR_CHECK( esp_wifi_set_config(WIFI_IF_STA, &wifi_config) );

    ESP_ERROR_CHECK( esp_wifi_start() );
}




/* main */
void app_main()
{
#ifdef ENABLE_UART0_LOG
	/* log uart config parameters */
	uart_config_t uart_log_config = 
	{
		.baud_rate = 115200,
		.data_bits = UART_DATA_8_BITS,
		.parity = UART_PARITY_DISABLE,
		.stop_bits = UART_STOP_BITS_1,
		.flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
		.rx_flow_ctrl_thresh = 122,
	};
#endif

	/* data uart config parameters */
	uart_config_t data_uart_config = 
	{
		.baud_rate = 115200,
		.data_bits = UART_DATA_8_BITS,
		.parity = UART_PARITY_DISABLE,
		.stop_bits = UART_STOP_BITS_1,
		.flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
		.rx_flow_ctrl_thresh = 122,
	};

#ifdef ENABLE_UART0_LOG
	/* set log UART parameters */
	uart_param_config(LOG_UART_NUM, &uart_log_config);

	/* set log level */
	esp_log_level_set(TAG, ESP_LOG_INFO);

	/* install log UART driver, no queue, no event */
	uart_driver_install(LOG_UART_NUM, (LOG_BUF_SIZE * 2), (LOG_BUF_SIZE * 2), 0, NULL, 0);

	/* set log UART pins (using UART0 default pins ie no changes.) */
	uart_set_pin(LOG_UART_NUM, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
#endif

	/* set data UART parameters */
	uart_param_config(DATA_UART_NUM, &data_uart_config);

	/* install data UART driver, and get the queue */
	uart_driver_install(DATA_UART_NUM, (DATA_BUF_SIZE * 2), (DATA_BUF_SIZE * 2), 10, &uart1_queue, 0);

	/* set data UART pins */
	uart_set_pin(DATA_UART_NUM, DATA_UART_TX_PIN, DATA_UART_RX_PIN, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);

	/* init WiFi */
	initialise_wifi();

	/* create UART event task */
	xTaskCreate(uart_event_task, "uart_event_task", 2048, NULL, 12, NULL);

	/* create MQTT task */
	xTaskCreatePinnedToCore(&aws_iot_task, "aws_iot_task", (36*1024), NULL, 5, NULL, 1);
}




/* End of file */





