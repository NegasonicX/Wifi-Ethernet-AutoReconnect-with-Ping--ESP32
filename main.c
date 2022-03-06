
#include <string.h>
#include "sdkconfig.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "freertos/queue.h"

#include "esp_netif.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_eth.h"
#include "esp_event.h"
#include "esp_err.h"
#include "esp_log.h"
#include "nvs_flash.h"

#include "driver/gpio.h"
#include "enc28j60.h"
#include "esp_eth_enc28j60.h"
#include "driver/spi_master.h"  
#include "driver/gpio.h"

#include "mdns.h"
#include "ping.h"

#include "lwip/err.h"
#include "lwip/sys.h"

//.............Indication Declarations.....................
#define builtin 2                                                       //++ Built-in LED of Devkit v1

char mac_json[50];                                                      //++ Array to store MAC Address of ESP32
uint8_t base_mac_addr[6] = {0};

//.............Ping Declarations.....................
#define ping_interval_ms 1500                                           //++ Pinging the host in every 1500ms interval
#define ping_priority 1                                                 //++ Setting the priorty for Ping task
#define ping_count 5                                                    //++ Number of times ESP pings the host
#define ping_loss_tolerance 35                                          //++ Setting maximum value for loss % 

char *TARGET_HOST = "www.google.com";                                   //++ Specify the Target Host to be pinged
bool ping_stop_flag = false;                                            //++ Flag to execute entire ping process one in while loop

//.............Wifi Declarations.....................
#define EXAMPLE_WIFI_SSID             "J@RV!$"                          //++ Set up your AP SSID which ESP will connect with
#define EXAMPLE_WIFI_PASS             "0123456789"                      //++ Set up your AP PASSWORD which ESP will connect with
#define EXAMPLE_MAXIMUM_RETRY         2                                 //++ Set the number of attempts ESP makes to connect the AP on boot
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1

static int s_retry_num = 0;
static EventGroupHandle_t s_wifi_event_group;
static const char *TAG_wifi = "Wifi";                                   //++ TAG for Wifi logs

//.............Ethernet Declarations.....................
static const char *TAG_eth = "Ethernet";                                //++ TAG for Ethernet logs
bool ethernet_connection_flag = false;                                  //++ To check whether ESP is connected to Ethernet Cable


//---------------------------------------------------------------------------------------------------------------------------
static void event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data)   //++ Handler for Wifi ( executes only once on boot )
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START)                                   //++ ESP Wifi has began
    {                                 
        esp_wifi_connect();
    } 
    else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_CONNECTED)                        //++ ESP connected to the specificed AP
    {                      
        ESP_LOGI("Connected to the AP","SSID : %s & PASS : %s", EXAMPLE_WIFI_SSID,EXAMPLE_WIFI_PASS);
    } 
    else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED)                     //++ ESP couldn't connect to the specificed AP
    {
        if (s_retry_num < EXAMPLE_MAXIMUM_RETRY) {
            esp_wifi_connect();
            s_retry_num++;
            ESP_LOGW(TAG_wifi, "retry to connect to the AP");
        } 
        else 
        {
            xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);                                    
        }
        ESP_LOGE(TAG_wifi,"connect to the AP fail");                                                  //++ ESP failed to connect to the specified AP
    } 
    else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP)                               //++ ESP got the IP from specified AP
    {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(TAG_wifi, "got ip:" IPSTR, IP2STR(&event->ip_info.ip));
        s_retry_num = 0;
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

//---------------------------------------------------------------------------------------------------------------------------
void wifi_init_sta(void)                                                    //++ Wifi Initialising Funtion                                                    
{
    s_wifi_event_group = xEventGroupCreate();

    esp_netif_init();

    esp_event_loop_create_default();

    esp_netif_t *sta_netif = esp_netif_create_default_wifi_sta();
    assert(sta_netif);

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_wifi_init(&cfg);

    esp_event_handler_instance_t instance_any_id;
    esp_event_handler_instance_t instance_got_ip;
    esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &event_handler, sta_netif, &instance_any_id);
    esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &event_handler, sta_netif, &instance_got_ip);

    wifi_config_t wifi_config = {
        .sta = {
           .ssid = EXAMPLE_WIFI_SSID,
           .password = EXAMPLE_WIFI_PASS,
            /* Setting a password implies station will connect to all security modes including WEP/WPA.
             * However these modes are deprecated and not advisable to be used. Incase your Access point
             * doesn't support WPA2, these mode can be enabled by commenting below line */
	     .threshold.authmode = WIFI_AUTH_WPA2_PSK,

            .pmf_cfg = {
                .capable = true,
                .required = false
            },
        },
    };

    esp_wifi_set_mode(WIFI_MODE_STA);
    esp_wifi_set_config(WIFI_IF_STA, &wifi_config);
    esp_wifi_start();

    ESP_LOGI(TAG_wifi, "wifi_init_sta finished.");

    /* Waiting until either the connection is established (WIFI_CONNECTED_BIT) or connection failed for the maximum
     * number of re-tries (WIFI_FAIL_BIT). The bits are set by event_handler() (see above) */
    EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
            WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
            pdFALSE,
            pdFALSE,
            portMAX_DELAY);

    /* xEventGroupWaitBits() returns the bits before the call returned, hence we can test which event actually
     * happened. */
    if (bits & WIFI_CONNECTED_BIT) {
        ESP_LOGI(TAG_wifi, "connected to ap SSID:%s password:%s",
                 EXAMPLE_WIFI_SSID, EXAMPLE_WIFI_PASS);
    
                // log_wifi = 1;
    } else if (bits & WIFI_FAIL_BIT) {
        ESP_LOGI(TAG_wifi, "Failed to connect to SSID:%s, password:%s",
                 EXAMPLE_WIFI_SSID, EXAMPLE_WIFI_PASS);
                 //log_wifi = 0;
    } else {
        ESP_LOGE(TAG_wifi, "UNEXPECTED EVENT");
    }

    /* The event will not be processed after unregister */
    esp_event_handler_instance_unregister(IP_EVENT, IP_EVENT_STA_GOT_IP, instance_got_ip);
    esp_event_handler_instance_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID, instance_any_id);
    vEventGroupDelete(s_wifi_event_group);
}

/** Event handler for Ethernet events ( executes continuously ) */
static void eth_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    uint8_t mac_addr[6] = {0};
    /* we can get the ethernet driver handle from event data */
    esp_eth_handle_t eth_handle = *(esp_eth_handle_t *)event_data;

    switch (event_id) {
    case ETHERNET_EVENT_CONNECTED:
        esp_eth_ioctl(eth_handle, ETH_CMD_G_MAC_ADDR, mac_addr);
        ESP_LOGI(TAG_eth, "Ethernet Link Up");
        ESP_LOGI(TAG_eth, "Ethernet HW Addr %02x:%02x:%02x:%02x:%02x:%02x",
                 mac_addr[0], mac_addr[1], mac_addr[2], mac_addr[3], mac_addr[4], mac_addr[5]);
            
        break;
    case ETHERNET_EVENT_DISCONNECTED:
        ESP_LOGI(TAG_eth, "Ethernet Link Down");
        ethernet_connection_flag = false;
        break;
    case ETHERNET_EVENT_START:
        ESP_LOGI(TAG_eth, "Ethernet Started");
        break;
    case ETHERNET_EVENT_STOP:
        ESP_LOGI(TAG_eth, "Ethernet Stopped");
        break;
    default:
        break;
    }
}

/** Event handler for IP_EVENT_ETH_GOT_IP */
static void got_ip_event_handler(void *arg, esp_event_base_t event_base,
                                 int32_t event_id, void *event_data)
{
    ip_event_got_ip_t *event = (ip_event_got_ip_t *) event_data;
    const esp_netif_ip_info_t *ip_info = &event->ip_info;

    ESP_LOGI(TAG_eth, "Ethernet Got IP Address");
    ESP_LOGI(TAG_eth, "~~~~~~~~~~~");
    ESP_LOGI(TAG_eth, "ETHIP:" IPSTR, IP2STR(&ip_info->ip));
    ESP_LOGI(TAG_eth, "ETHMASK:" IPSTR, IP2STR(&ip_info->netmask));
    ESP_LOGI(TAG_eth, "ETHGW:" IPSTR, IP2STR(&ip_info->gw));
    ESP_LOGI(TAG_eth, "~~~~~~~~~~~");

    ethernet_connection_flag = true;                                                //++ Set the flag as "true" once ESP get IP from ethernet
    
}

//---------------------------------------------------------------------------------------------------------------------------
void ethernet_init_sta()                                                            //++ Ethernet Initialising Function
{
   // ESP_ERROR_CHECK(esp_netif_init());
    // Create default event loop that running in background
    //ESP_ERROR_CHECK(esp_event_loop_create_default());

    esp_netif_config_t netif_cfg = ESP_NETIF_DEFAULT_ETH();
    esp_netif_t *eth_netif = esp_netif_new(&netif_cfg);
   
   spi_bus_config_t buscfg = {
        .miso_io_num = CONFIG_EXAMPLE_ENC28J60_MISO_GPIO,
        .mosi_io_num = CONFIG_EXAMPLE_ENC28J60_MOSI_GPIO,
        .sclk_io_num = CONFIG_EXAMPLE_ENC28J60_SCLK_GPIO,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
    };
   
     ESP_ERROR_CHECK(spi_bus_initialize(CONFIG_EXAMPLE_ENC28J60_SPI_HOST, &buscfg, SPI_DMA_CH_AUTO));
    /* ENC28J60 ethernet driver is based on spi driver */
    spi_device_interface_config_t devcfg = {
        .command_bits = 3,
        .address_bits = 5,
        .mode = 0,
        .clock_speed_hz = CONFIG_EXAMPLE_ENC28J60_SPI_CLOCK_MHZ * 1000 * 1000,
        .spics_io_num = CONFIG_EXAMPLE_ENC28J60_CS_GPIO,
        .queue_size = 20,
        .cs_ena_posttrans = enc28j60_cal_spi_cs_hold_time(CONFIG_EXAMPLE_ENC28J60_SPI_CLOCK_MHZ),
    };

   spi_device_handle_t spi_handle = NULL;
    ESP_ERROR_CHECK(spi_bus_add_device(CONFIG_EXAMPLE_ENC28J60_SPI_HOST, &devcfg, &spi_handle));

     eth_enc28j60_config_t enc28j60_config = ETH_ENC28J60_DEFAULT_CONFIG(spi_handle);
    enc28j60_config.int_gpio_num = CONFIG_EXAMPLE_ENC28J60_INT_GPIO;

      eth_mac_config_t mac_config = ETH_MAC_DEFAULT_CONFIG();
    mac_config.smi_mdc_gpio_num = -1;  // ENC28J60 doesn't have SMI interface
    mac_config.smi_mdio_gpio_num = -1;
    esp_eth_mac_t *mac = esp_eth_mac_new_enc28j60(&enc28j60_config, &mac_config);
   
    eth_phy_config_t phy_config = ETH_PHY_DEFAULT_CONFIG();
    phy_config.autonego_timeout_ms = 0; // ENC28J60 doesn't support auto-negotiation
    phy_config.reset_gpio_num = -1; // ENC28J60 doesn't have a pin to reset internal PHY
    esp_eth_phy_t *phy = esp_eth_phy_new_enc28j60(&phy_config);

     esp_eth_config_t eth_config = ETH_DEFAULT_CONFIG(mac, phy);
    esp_eth_handle_t eth_handle = NULL;
    ESP_ERROR_CHECK(esp_eth_driver_install(&eth_config, &eth_handle));

    /* ENC28J60 doesn't burn any factory MAC address, we need to set it manually.
       02:00:00 is a Locally Administered OUI range so should not be used except when testing on a LAN under your control.
    */
       mac->set_addr(mac, (uint8_t[]) {
        //0x02, 0x00, 0x00, 0x12, 0x34, 0x56
        base_mac_addr[0], base_mac_addr[1], base_mac_addr[2], base_mac_addr[3], base_mac_addr[4], base_mac_addr[5]
    });
   
   // ENC28J60 Errata #1 check
    if (emac_enc28j60_get_chip_info(mac) < ENC28J60_REV_B5 && CONFIG_EXAMPLE_ENC28J60_SPI_CLOCK_MHZ < 8) {
        ESP_LOGE(TAG_eth, "SPI frequency must be at least 8 MHz for chip revision less than 5");
        ESP_ERROR_CHECK(ESP_FAIL);
    }
   
   
    // Set default handlers to process TCP/IP stuffs
   ESP_ERROR_CHECK(esp_eth_set_default_handlers(eth_netif));
    //-------------------------------------------------------------------------------------------------------
   
//    if(strcmp(nvs_sta_edhcp,"0")==0)
//    {
//       // printf(" Trueee \n"); 
//     ESP_ERROR_CHECK(esp_netif_dhcpc_stop(eth_netif));
//     // char* ip= "192.168.1.251";
//     // char* gateway = "192.168.1.1";
//     // char* netmask = "255.255.255.0";
//     // char* dns = "8.8.8.8";
//     esp_netif_ip_info_t info_t;
//     //esp_netif_dns_info_t dns_info;
//     memset(&info_t, 0, sizeof(esp_netif_ip_info_t));
//     info_t.ip.addr = esp_ip4addr_aton((const char *)nvs_sta_eip);
//     info_t.gw.addr = esp_ip4addr_aton((const char *)nvs_sta_egw);
//     info_t.netmask.addr = esp_ip4addr_aton((const char *) nvs_sta_esnt);
//     // dns_info.ip.u_addr = esp_ip4addr_aton((const char *)dns);
//     // IP_ADDR4(&dns_info.ip, 208, 91, 112, 53);
//     esp_netif_set_ip_info(eth_netif, &info_t); 
//         // ESP_LOGI(TAG, "Success to set static ip: %s, netmask: %s, gw: %s", nvs_sta_eip, nvs_sta_egw, nvs_sta_esnt);
//         ESP_ERROR_CHECK(example_set_dns_server(eth_netif, ipaddr_addr(nvs_sta_edns1), ESP_NETIF_DNS_MAIN));
//         ESP_ERROR_CHECK(example_set_dns_server(eth_netif, ipaddr_addr(nvs_sta_edns2), ESP_NETIF_DNS_BACKUP));
//     // esp_netif_set_dns_info(eth_netif, esp_ip4addr_aton(nvs_sta_edns1), &dns_info);
//     //    esp_netif_set_dns_info(eth_netif, nvs_sta_edns2, &dns_info);
//     //    ESP_ERROR_CHECK(esp_eth_set_default_handlers(eth_netif));
//    }
    //-------------------------------------------------------------------------------------------------------

    
    /* attach Ethernet driver to TCP/IP stack */
    // ESP_ERROR_CHECK(esp_netif_attach(eth_netif, esp_eth_new_netif_glue(eth_handle)));
    esp_netif_attach(eth_netif, esp_eth_new_netif_glue(eth_handle));
   
   
    // Register user defined event handers
    ESP_ERROR_CHECK(esp_event_handler_register(ETH_EVENT, ESP_EVENT_ANY_ID, &eth_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_ETH_GOT_IP, &got_ip_event_handler, NULL));

    /* start Ethernet driver state machine */
    esp_eth_start(eth_handle);
    enc28j60_set_phy_duplex(phy, ETH_DUPLEX_FULL);                 //++ Running the enc28j60 ic on FULL DUPLEX mode ( use "ETH_DUPLEX_HALF" to run on half duplex )

}

//---------------------------------------------------------------------------------------------------------------------------
void reconnection()                                                                         //++ Task to check for Interent and Reconnection                                                                            
{
    while(1)
    { 
        wifi_ap_record_t wifidata;
        esp_wifi_sta_get_ap_info(&wifidata);
        int rssi= wifidata.rssi;                                                            //++ Get the RSSI of the AP ESP is connected to

        if((rssi < 0 && rssi > -100) || ethernet_connection_flag == true)                   //++ Perform the ping operations only when sufficient Wifi RSSI is available or Ethernet cable is connected
        {
            ping_stop_flag = false;
            do{                                                                             //++ Loop to perform one complete cycle of ping function
                initialize_ping(ping_interval_ms, ping_priority, TARGET_HOST, ping_count);  //++ Initialize the ping process

                vTaskDelay(ping_interval_ms*ping_count / portTICK_PERIOD_MS);               //++ Wait for the complete execution to complete and take the response

                int loss = cmd_ping_on_ping_results(2);                                     //++ Extract the loss % from the ping cycle

                if(loss<ping_loss_tolerance)                                                //++ Condition to check whether good internet is available or not
                {
                    gpio_set_level(builtin,1);
                    printf("GOOD INTERNET\n");
                    ping_stop_flag = true;
                }
                else
                {
                    gpio_set_level(builtin,0);
                    printf("BAD INTERNET\n");
                    ping_stop_flag = true;
                }

            }while(ping_stop_flag==false);
        }

        else if(rssi == 0 || rssi < -100)                              //++ If RSSI is 0 or beyond -100, ESP is connected to AP and try establishing the connection
        {
            gpio_set_level(builtin,0);
            ESP_LOGI(TAG_wifi, "Lost AP Radio, Trying to Establish Network.....");
            esp_wifi_connect();
        }

        vTaskDelay( 10000 / portTICK_PERIOD_MS);                       //++ Execute complete while loop every 10 seconds
    }
}

//---------------------------------------------------------------------------------------------------------------------------
void app_main(void)
{
    esp_err_t ret = nvs_flash_init();                                                   //++ Initialize the NVS of ESP
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
      ESP_ERROR_CHECK(nvs_flash_erase());
      ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    
    esp_efuse_mac_get_default(base_mac_addr);;
    sprintf(mac_json,"%x:%x:%x:%x:%x:%x", base_mac_addr[0], base_mac_addr[1], base_mac_addr[2], base_mac_addr[3], base_mac_addr[4], base_mac_addr[5]);
    printf("MAC Address for the device = %s \n",mac_json);                              //++ Get the MAC Address of the ESP

    wifi_init_sta();                                                                    //++ Call the Wifi Initializing Function
    ethernet_init_sta();                                                             //++ Call the Ethernet Initializing Function
     
    gpio_set_direction(builtin, GPIO_MODE_OUTPUT);                                      //++ Set the built-in LED direction as OUTPUT
    gpio_set_level(builtin,0);

    xTaskCreate(reconnection, "reconnection", 1024*4, NULL, configMAX_PRIORITIES-1, NULL);      //++ Create the Task to check reconnection and interent

}

