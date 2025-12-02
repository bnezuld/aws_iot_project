/*
 * application_startup.c
 *
 *  Created on: Feb 28, 2021
 *      Author: Brandon
 */


#include "application_startup.h"

#include "ap_mode_task.h"
#include "mqtt_auth.h"
#include "mqtt_shadow.h"
#include "ota.h"

/* Wi-Fi Interface files. */
#include "iot_wifi.h"

#include <stdio.h>

#include <ti/devices/cc32xx/inc/hw_types.h>
#include <ti/devices/cc32xx/driverlib/prcm.h>
#include <ti/drivers/power/PowerCC32XX.h>

#define HIBERNATE_CLOCK_SPEED_SECOND 32768

void startup(void * params){


    WIFIReturnCode_t xWifiStatus;

    WIFI_On();

    xWifiStatus = WIFI_ConnectAP( NULL );
    if(xWifiStatus == eWiFiSuccess)
    {
        //vStartOTAUpdateDemoTask(NULL);
        //RunCoreMqttMutualAuthDemo();
        RunDeviceShadowDemo();
        /*Iot_CreateDetachedThread( vStartOTAUpdateDemoTask,
                                  NULL,
                                  democonfigDEMO_PRIORITY,
                                  democonfigDEMO_STACKSIZE );*/
        WIFI_Off();
    }else{
        WIFI_Off();
        AP_Task(NULL);
    }

    //Power_shutdown(0, 1);


    PRCMHibernateIntervalSet(HIBERNATE_CLOCK_SPEED_SECOND*60);

    PRCMHibernateWakeupSourceEnable(PRCM_HIB_SLOW_CLK_CTR);

    PRCMHibernateEnter();
}
