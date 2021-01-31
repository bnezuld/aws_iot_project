/*
 * AccessPoint.h
 *
 *  Created on: Jan 25, 2021
 *      Author: Brandon
 */

#ifndef APPLICATION_CODE_TASKS_INCLUDE_AP_MODE_TASK_H_
#define APPLICATION_CODE_TASKS_INCLUDE_AP_MODE_TASK_H_

/* TI-DRIVERS Header files */
#include <ti/drivers/net/wifi/simplelink.h>

/* POSIX Header files */
#include <semaphore.h>
#include <pthread.h>
#include <time.h>


#include "logging_levels.h"

#ifndef LIBRARY_LOG_NAME
    #define LIBRARY_LOG_NAME    "AP Mode"
#endif

#ifndef LIBRARY_LOG_LEVEL
    #define LIBRARY_LOG_LEVEL    LOG_INFO
#endif

#include "logging_stack.h"


void AP_Task(void * pvParameters);

void provisioningInit(void);

int32_t provisioningStart(void);

static int32_t ConfigureSimpleLinkToDefaultState(void);

static int32_t InitSimplelink(uint8_t const role);

static int32_t HandleStrtdEvt(void);

#endif /* APPLICATION_CODE_TASKS_INCLUDE_AP_MODE_TASK_H_ */
