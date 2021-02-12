/*
 * ota.h
 *
 *  Created on: Feb 12, 2021
 *      Author: Brandon
 */

#ifndef APPLICATION_CODE_TASKS_INCLUDE_OTA_H_
#define APPLICATION_CODE_TASKS_INCLUDE_OTA_H_

/* Standard include. */
#include <stdbool.h>

/* MQTT include. */
#include "iot_mqtt.h"

int vStartOTAUpdateDemoTask( bool awsIotMqttMode,
                             const char * pIdentifier,
                             void * pNetworkServerInfo,
                             void * pNetworkCredentialInfo,
                             const IotNetworkInterface_t * pNetworkInterface );


#endif /* APPLICATION_CODE_TASKS_INCLUDE_OTA_H_ */
