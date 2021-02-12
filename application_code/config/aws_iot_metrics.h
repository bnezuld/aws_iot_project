/*
 * iot_metrics.h
 *
 *  Created on: Feb 8, 2021
 *      Author: Brandon
 */

#ifndef APPLICATION_CODE_TASKS_INCLUDE_IOT_METRICS_H_
#define APPLICATION_CODE_TASKS_INCLUDE_IOT_METRICS_H_


/* Include MQTT header for MQTT_LIBRARY_VERSION macro. */
#include "core_mqtt.h"

/**
 * @brief The name of the operating system that the application is running on.
 * The current value is given as an example. Please update for your specific
 * operating system.
 */
#define democonfigOS_NAME       "FreeRTOS"

/**
 * @brief The version of the operating system that the application is running
 * on. The current value is given as an example. Please update for your specific
 * operating system version.
 */
#define democonfigOS_VERSION    "V10.4.3"

/**
 * @brief The name of the MQTT library used and its version, following an "@"
 * symbol.
 */
#define democonfigMQTT_LIB      "core-mqtt@" MQTT_LIBRARY_VERSION

/**
 * @brief The MQTT metrics string expected by AWS IoT.
 */
#define AWS_IOT_METRICS_STRING                                 \
    "?SDK=" democonfigOS_NAME "&Version=" democonfigOS_VERSION \
    "&MQTTLib=" democonfigMQTT_LIB

/**
 * @brief The length of the MQTT metrics string expected by AWS IoT.
 */
#define AWS_IOT_METRICS_STRING_LENGTH    ( ( uint16_t ) ( sizeof( AWS_IOT_METRICS_STRING ) - 1 ) )


#endif /* APPLICATION_CODE_TASKS_INCLUDE_IOT_METRICS_H_ */
