#include <float.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "esp_crt_bundle.h"
#include "esp_err.h"
#include "esp_netif_types.h"
#include "esp_tls.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"

#include "hal/gpio_types.h"
#include "nvs_flash.h"

#include "esp_mac.h"
#include "rom/ets_sys.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_netif.h"
#include "esp_now.h"

#include "lwip/inet.h"
#include "lwip/netdb.h"
#include "lwip/sockets.h"
#include "ping/ping_sock.h"
#include "esp_http_client.h"
#include "driver/gpio.h"
#include "soc/gpio_num.h"
#include "esp_timer.h"

static EventGroupHandle_t s_wifi_event_group;
#define CONFIG_SEND_FREQUENCY      10000

static const char *TAG = "csi_recv_router";
#define EXAMPLE_ESP_WIFI_SSID      "2.4GHz"
#define EXAMPLE_ESP_WIFI_PASS      "iJ88WJQrkkbM"
//#define EXAMPLE_ESP_WIFI_SSID      "BigChungus"
//#define EXAMPLE_ESP_WIFI_PASS      "Monthedee"
#define databaseURL "http://192.168.0.85/post-csi-data.php"
//#define databaseURL "http://192.168.137.51/post-csi-data.php"
#define apiKey "tPmAT5Ab3j7F9"
static int s_retry_num = 0;
static bool wifi_connected;
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1
int csi_buffer[1][500];
int buffer_index = 0;
esp_ping_handle_t ping_handle;
bool data_to_send = false;
esp_http_client_handle_t handle;
int esp_id = 0;
int led_level = 0;
static esp_err_t wifi_ping_router_start()
{
    static esp_ping_handle_t ping_handle = NULL;

    esp_ping_config_t ping_config = ESP_PING_DEFAULT_CONFIG();
    ping_config.count             = ESP_PING_COUNT_INFINITE;
    ping_config.interval_ms       = 1000 / CONFIG_SEND_FREQUENCY;
    ping_config.task_stack_size   = 3072;
    ping_config.data_size         = 1;

    esp_netif_ip_info_t local_ip;
    esp_netif_get_ip_info(esp_netif_get_handle_from_ifkey("WIFI_STA_DEF"), &local_ip);
    ESP_LOGI(TAG, "got ip:" IPSTR ", gw: " IPSTR, IP2STR(&local_ip.ip), IP2STR(&local_ip.gw));
    ping_config.target_addr.u_addr.ip4.addr = ip4_addr_get_u32(&local_ip.gw);
//    ip4_addr_t gw;
//    gw.addr = ipaddr_addr("192.168.137.62");
//    ping_config.target_addr.u_addr.ip4.addr =  ip4_addr_get_u32(&gw);
    ping_config.target_addr.type = ESP_IPADDR_TYPE_V4;

    esp_ping_callbacks_t cbs = { 0 };
    ESP_ERROR_CHECK(esp_ping_new_session(&ping_config, &cbs, &ping_handle));
    ESP_ERROR_CHECK(esp_ping_start(ping_handle));

    return ESP_OK;
}

//static esp_err_t wifi_ping_router_stop()
//{
//	esp_ping_stop(ping_handle);	
//	esp_ping_delete_session(ping_handle);
//	return ESP_OK;
//}

esp_err_t _http_event_handler(esp_http_client_event_t *evt)
{
    static char *output_buffer;
    static int output_len;
    switch(evt->event_id) {
        case HTTP_EVENT_ERROR:
            ESP_LOGD(TAG, "HTTP_EVENT_ERROR");
            break;
        case HTTP_EVENT_ON_CONNECTED:
            ESP_LOGD(TAG, "HTTP_EVENT_ON_CONNECTED");
            break;
        case HTTP_EVENT_HEADER_SENT:
            ESP_LOGD(TAG, "HTTP_EVENT_HEADER_SENT");
            break;
        case HTTP_EVENT_ON_HEADER:
            ESP_LOGD(TAG, "HTTP_EVENT_ON_HEADER, key=%s, value=%s", evt->header_key, evt->header_value);
            break;
        case HTTP_EVENT_ON_DATA:
            ESP_LOGD(TAG, "HTTP_EVENT_ON_DATA, len=%d", evt->data_len);
            if (output_len == 0 && evt->user_data) {
                memset(evt->user_data, 0, 2048);
            }
            if (!esp_http_client_is_chunked_response(evt->client)) {
                int copy_len = 0;
                if (evt->user_data) {
                    copy_len = evt->data_len;
                    if (copy_len) {
                        memcpy(evt->user_data + output_len, evt->data, copy_len);
                    }
                } else {
                    int content_len = esp_http_client_get_content_length(evt->client);
                    if (output_buffer == NULL) {
                        output_buffer = (char *) calloc(content_len + 1, sizeof(char));
                        output_len = 0;
                        if (output_buffer == NULL) {
                            ESP_LOGE(TAG, "Failed to allocate memory for output buffer");
                            return ESP_FAIL;
                        }
                    }
                    copy_len = evt->data_len;
                    if (copy_len) {
                        memcpy(output_buffer + output_len, evt->data, copy_len);
                    }
                }
                output_len += copy_len;
            }

            break;
        case HTTP_EVENT_ON_FINISH:
            ESP_LOGD(TAG, "HTTP_EVENT_ON_FINISH");
            if (output_buffer != NULL) {
                free(output_buffer);
                output_buffer = NULL;
            }
            output_len = 0;
            break;
        case HTTP_EVENT_DISCONNECTED:
            ESP_LOGD(TAG, "HTTP_EVENT_DISCONNECTED");
            int mbedtls_err = 0;
            esp_err_t err = esp_tls_get_and_clear_last_error((esp_tls_error_handle_t)evt->data, &mbedtls_err, NULL);
            if (err != 0) {
                ESP_LOGI(TAG, "Last esp error code: 0x%x", err);
                ESP_LOGI(TAG, "Last mbedtls failure: 0x%x", mbedtls_err);
            }
            if (output_buffer != NULL) {
                free(output_buffer);
                output_buffer = NULL;
            }
            output_len = 0;
            break;
        case HTTP_EVENT_REDIRECT:
            ESP_LOGD(TAG, "HTTP_EVENT_REDIRECT");
            esp_http_client_set_header(evt->client, "From", "user@example.com");
            esp_http_client_set_header(evt->client, "Accept", "text/html");
            esp_http_client_set_redirection(evt->client);
            break;
    }
    return ESP_OK;
}

void http_send(){
		
		char buffer[1000];
		char *targ = buffer;
		int i = 0;
		targ += sprintf(targ, "api-key=1234&rssi=%d", csi_buffer[i][0]);
		targ += sprintf(targ, "&rate=%d", csi_buffer[i][1]);
		targ += sprintf(targ, "&sig_mode=%d", csi_buffer[i][2]);
		targ += sprintf(targ, "&mcs=%d", csi_buffer[i][3]);
		targ += sprintf(targ, "&cwb=%d", csi_buffer[i][4]);
		targ += sprintf(targ, "&smoothing=%d", csi_buffer[i][5]);
		targ += sprintf(targ, "&not_sounding=%d", csi_buffer[i][6]);
		targ += sprintf(targ, "&aggregation=%d", csi_buffer[i][7]);
		targ += sprintf(targ, "&stbc=%d", csi_buffer[i][8]);
		targ += sprintf(targ, "&fec_coding=%d", csi_buffer[i][9]);
		targ += sprintf(targ, "&sgi=%d", csi_buffer[i][10]);
		targ += sprintf(targ, "&noise_floor=%d", csi_buffer[i][11]);
		targ += sprintf(targ, "&ampdu_cnt=%d", csi_buffer[i][12]);
		targ += sprintf(targ, "&channel=%d", csi_buffer[i][13]);
		targ += sprintf(targ, "&secondary_channel=%d", csi_buffer[i][14]);
		targ += sprintf(targ, "&timestamp=%d", csi_buffer[i][15]);
		targ += sprintf(targ, "&ant=%d", csi_buffer[i][16]);
		targ += sprintf(targ, "&sig_len=%d", csi_buffer[i][17]);
		targ += sprintf(targ, "&rx_state=%d", csi_buffer[i][18]);
		targ += sprintf(targ, "&esp_id=%d", csi_buffer[i][19]);
		targ += sprintf(targ, "&buffer_len=%d", csi_buffer[i][20]);
		targ += sprintf(targ, "&buffer=%d", csi_buffer[i][21]);
		for (int x = 0; x < csi_buffer[i][20]; x++){
			targ += sprintf(targ, " %d", csi_buffer[i][21+x]);
		}
//		printf("%s", buffer);
//		printf("\n");
	    ESP_ERROR_CHECK(esp_http_client_set_post_field(handle, buffer, strlen(buffer)));
		esp_err_t err = esp_http_client_perform(handle);
	    if (err == ESP_OK) {
	        ESP_LOGD(TAG, "HTTP POST Status = %d, content_length = %"PRId64,
	        esp_http_client_get_status_code(handle),
	        esp_http_client_get_content_length(handle));
	   	} else {
	       	ESP_LOGE(TAG, "HTTP POST request failed: %s", esp_err_to_name(err));
	   	}
	   	//esp_http_client_close(handle);
	   	//esp_http_client_cleanup(handle);
}

void add_csi_data_to_buffer(wifi_csi_info_t *info){
	

    const wifi_pkt_rx_ctrl_t *rx_ctrl = &info->rx_ctrl;
    const int8_t *buf = info->buf;
    const uint16_t len = info->len;
//        printf("%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d",
//            rx_ctrl->rssi, rx_ctrl->rate, rx_ctrl->sig_mode,
//            rx_ctrl->mcs, rx_ctrl->cwb, rx_ctrl->smoothing, rx_ctrl->not_sounding,
//            rx_ctrl->aggregation, rx_ctrl->stbc, rx_ctrl->fec_coding, rx_ctrl->sgi,
//            rx_ctrl->noise_floor, rx_ctrl->ampdu_cnt, rx_ctrl->channel, rx_ctrl->secondary_channel,
//            rx_ctrl->timestamp, rx_ctrl->ant, rx_ctrl->sig_len, rx_ctrl->rx_state);
    led_level = !led_level;
    gpio_set_level(GPIO_NUM_2, led_level); 
    csi_buffer[buffer_index][0] =  rx_ctrl->rssi;
    csi_buffer[buffer_index][1] =  rx_ctrl->rate;
    csi_buffer[buffer_index][2] =  rx_ctrl->sig_mode;
    csi_buffer[buffer_index][3] =  rx_ctrl->mcs;
    csi_buffer[buffer_index][4] =  rx_ctrl->cwb;
    csi_buffer[buffer_index][5] =  rx_ctrl->smoothing;
    csi_buffer[buffer_index][6] =  rx_ctrl->not_sounding;
    csi_buffer[buffer_index][7] =  rx_ctrl->aggregation;
    csi_buffer[buffer_index][8] =  rx_ctrl->stbc;
    csi_buffer[buffer_index][9] =  rx_ctrl->fec_coding;
    csi_buffer[buffer_index][10] =  rx_ctrl->sgi;
    csi_buffer[buffer_index][11] =  rx_ctrl->noise_floor;
    csi_buffer[buffer_index][12] =  rx_ctrl->ampdu_cnt;
    csi_buffer[buffer_index][13] =  rx_ctrl->channel;
    csi_buffer[buffer_index][14] =  rx_ctrl->secondary_channel;
    csi_buffer[buffer_index][15] =  rx_ctrl->timestamp;
    csi_buffer[buffer_index][16] =  rx_ctrl->ant;
    csi_buffer[buffer_index][17] =  rx_ctrl->sig_len;
    csi_buffer[buffer_index][18] =  rx_ctrl->rx_state;
    csi_buffer[buffer_index][19] =  esp_id;
    csi_buffer[buffer_index][20] =  len;
   	if (len > 0){
    	for (int i = 0; i < len; i++){
    		csi_buffer[buffer_index][20+i] = buf[i];
    	}
    }
}

static void wifi_csi_rx_cb(void *ctx, wifi_csi_info_t *info)
{
    if (!info || !info->buf) {
        ESP_LOGW(TAG, "<%s> wifi_csi_cb", esp_err_to_name(ESP_ERR_INVALID_ARG));
        return;
    }

    if (memcmp(info->mac, ctx, 6)) {
        return;
    }
    
    add_csi_data_to_buffer(info);
    data_to_send = true;
    //http_send();
}

static void wifi_csi_init()
{
    /**
     * @brief In order to ensure the compatibility of routers, only LLTF sub-carriers are selected.
     */
    wifi_csi_config_t csi_config = {	
        .lltf_en           = true,
        .htltf_en          = false,
        .stbc_htltf2_en    = false,
        .ltf_merge_en      = true,
        .channel_filter_en = true,
        .manu_scale        = true,
        .shift             = true,
    };

    static wifi_ap_record_t s_ap_info = {0};
    ESP_ERROR_CHECK(esp_wifi_sta_get_ap_info(&s_ap_info));
    ESP_ERROR_CHECK(esp_wifi_set_csi_config(&csi_config));
    ESP_ERROR_CHECK(esp_wifi_set_csi_rx_cb(wifi_csi_rx_cb, s_ap_info.bssid));
    ESP_ERROR_CHECK(esp_wifi_set_csi(true));
    ESP_LOGI(TAG, "CSI INITIALISED");
    
}

static void event_handler(void* arg, esp_event_base_t event_base,
                                int32_t event_id, void* event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        if (s_retry_num < 1) {
            esp_wifi_connect();
            s_retry_num++;
            ESP_LOGI(TAG, "retry to connect to the AP");
        } else {
            xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
        }
        ESP_LOGI(TAG,"connect to the AP fail");
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        
        ESP_LOGI(TAG, "got ip:" IPSTR, IP2STR(&event->ip_info.ip));
        s_retry_num = 0;
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
        wifi_connected = true;
        wifi_csi_init();
        wifi_ping_router_start();
   		ESP_LOGI(TAG,"WIFI CONNECTED");
   		return;
    }
}

void wifi_init(){
	wifi_connected = false;
	s_wifi_event_group = xEventGroupCreate();
	 ESP_ERROR_CHECK(esp_netif_init());
	 
	 ESP_ERROR_CHECK(esp_event_loop_create_default());
     esp_netif_create_default_wifi_sta();

	 
	 wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
     ESP_ERROR_CHECK(esp_wifi_init(&cfg));
     esp_event_handler_instance_t instance_any_id;
     esp_event_handler_instance_t instance_got_ip;
      ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                        ESP_EVENT_ANY_ID,
                                                        &event_handler,
                                                        NULL,
                                                        &instance_any_id));
     ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                                                        IP_EVENT_STA_GOT_IP,
                                                        &event_handler,
                                                        NULL,
                                                        &instance_got_ip));
     wifi_config_t wifi_config = {
        .sta = {
            .ssid = EXAMPLE_ESP_WIFI_SSID,
            .password = EXAMPLE_ESP_WIFI_PASS,           
        },
    };
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA) );
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config) );
    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_LOGI(TAG,"WIFI INIT");
     EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
            WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
            pdFALSE,
            pdFALSE,
            portMAX_DELAY);

    if (bits & WIFI_CONNECTED_BIT) {
        ESP_LOGI(TAG, "connected to ap SSID:%s password:%s",
                 EXAMPLE_ESP_WIFI_SSID, EXAMPLE_ESP_WIFI_PASS);
    } else if (bits & WIFI_FAIL_BIT) {
        ESP_LOGI(TAG, "Failed to connect to SSID:%s, password:%s",
                 EXAMPLE_ESP_WIFI_SSID, EXAMPLE_ESP_WIFI_PASS);
    } else {
        ESP_LOGE(TAG, "UNEXPECTED EVENT");
    }
}


void app_main()
{
	
	uint8_t base_mac_addr[6];
	esp_efuse_mac_get_default(base_mac_addr);
	uint8_t index = 0;
	char macId[50];
	for(uint8_t i=0; i<6; i++){
   		index += sprintf(&macId[index], "%02x", base_mac_addr[i]);
        }
	ESP_LOGI(TAG, "macId = %s", macId);	
	char* one =  "8813bf03c4b0";
	char* two =  "8813bf024264";
	char* three =  "8813bf0374b4";
	if (strcmp(macId, one) == 0){
		esp_id = 1;
	}
	if (strcmp(macId, two) == 0){
		esp_id = 2;
	}
	if (strcmp(macId, three) == 0){
		esp_id = 3;
	}
	ESP_LOGI(TAG, "esp_id = %d", esp_id);	
	gpio_set_direction(GPIO_NUM_2, GPIO_MODE_OUTPUT);
    //Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
      ESP_ERROR_CHECK(nvs_flash_erase());
      ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
	wifi_init();
	esp_http_client_config_t config = {
		.url = databaseURL,
	    .auth_type = HTTP_AUTH_TYPE_NONE,
	    .timeout_ms = 1000,
	    .event_handler = _http_event_handler,
	    .transport_type = HTTP_TRANSPORT_OVER_SSL,
   		.crt_bundle_attach = esp_crt_bundle_attach,
		};
	handle = esp_http_client_init(&config);
	ESP_ERROR_CHECK(esp_http_client_set_url(handle, databaseURL));
	ESP_ERROR_CHECK(esp_http_client_set_method(handle, HTTP_METHOD_POST));
	data_to_send = true; 
	while (1){
		if (data_to_send){
			http_send();
			data_to_send = false;
			taskYIELD();
		}
	}
}