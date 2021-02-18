/*
 * ota.h
 *
 *  Created on: Feb 12, 2021
 *      Author: Brandon
 */

#ifndef APPLICATION_CODE_TASKS_INCLUDE_OTA_H_
#define APPLICATION_CODE_TASKS_INCLUDE_OTA_H_

/**************************************************/
/******* DO NOT CHANGE the following order ********/
/**************************************************/

/* Include logging header files and define logging macros in the following order:
 * 1. Include the header file "logging_levels.h".
 * 2. Define the LIBRARY_LOG_NAME and LIBRARY_LOG_LEVEL macros depending on
 * the logging configuration for DEMO.
 * 3. Include the header file "logging_stack.h", if logging is enabled for DEMO.
 */

#include "logging_levels.h"

/* Logging configuration for the Demo. */
#ifndef LIBRARY_LOG_NAME
    #define LIBRARY_LOG_NAME    "MQTT_OTA"
#endif

#ifndef LIBRARY_LOG_LEVEL
    #define LIBRARY_LOG_LEVEL    LOG_INFO
#endif
#include "logging_stack.h"

/************ End of logging configuration ****************/

/* Standard include. */
#include <stdbool.h>

/* MQTT include. */
#include "iot_mqtt.h"

#define democonfigROOT_CA_PEM            tlsSTARFIELD_ROOT_CERTIFICATE_PEM

/**
 * @brief Size of the network buffer for MQTT packets.
 */
#define democonfigNETWORK_BUFFER_SIZE    ( 1024U )

void vStartOTAUpdateDemoTask(void * params);

#endif /* APPLICATION_CODE_TASKS_INCLUDE_OTA_H_ */
