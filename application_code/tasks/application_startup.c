/*
 * application_startup.c
 *
 *  Created on: Feb 28, 2021
 *      Author: Brandon
 */


#include "application_startup.h"

#include "ap_mode_task.h"
#include "mqtt_auth.h"
#include "ota.h"

/* Wi-Fi Interface files. */
#include "iot_wifi.h"

#include <stdio.h>

void startup(void * params){


    WIFIReturnCode_t xWifiStatus;

    WIFI_On();

    xWifiStatus = WIFI_ConnectAP( NULL );
    if(xWifiStatus == eWiFiSuccess)
    {
        vStartOTAUpdateDemoTask(NULL);
        RunCoreMqttMutualAuthDemo();
        /*Iot_CreateDetachedThread( vStartOTAUpdateDemoTask,
                                  NULL,
                                  democonfigDEMO_PRIORITY,
                                  democonfigDEMO_STACKSIZE );*/
    }else{
        WIFI_Off();
        AP_Task(NULL);
    }
}
