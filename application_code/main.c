/*
 * FreeRTOS V1.4.7
 * Copyright (C) 2020 Amazon.com, Inc. or its affiliates.  All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy of
 * this software and associated documentation files (the "Software"), to deal in
 * the Software without restriction, including without limitation the rights to
 * use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
 * the Software, and to permit persons to whom the Software is furnished to do so,
 * subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
 * FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
 * COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
 * IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 * http://aws.amazon.com/freertos
 * http://www.FreeRTOS.org
 */

/*
 * Debug setup instructions:
 * 1) Open the debug configuration dialog.
 * 2) Go to the target tab.
 * 3) Ensure that check box 'Reset target on a connect' is selected.
 */

/* Standard includes. */
#include "ap_mode_task.h"
#include "mqtt_auth.h"
#include "ota.h"
//#include "mqtt_demo_mutual_auth_config.h"
//#include "aws_demo.h"
#include <stdint.h>
#include <stdio.h>

/* EDIT THIS FILE:
 * Wi-Fi SSID, password & security settings,
 * AWS endpoint, certificate, private key & thing name. */
#include "iot_config.h"

#include "iot_default_root_certificates.h"
#include "aws_secure_sockets_config.h"
#include "core_pkcs11.h"
#include "core_pkcs11_config.h"

/* Demo priorities & stack sizes. */
//#include "aws_demo_config.h"

/* FreeRTOS header files. */
#include "FreeRTOS.h"
#include "task.h"

/* Wi-Fi Interface files. */
#include "iot_wifi.h"

/* Demo files. */
//#include "aws_demo.h"
#include "iot_logging_task.h"
#include "iot_system_init.h"
#include "aws_dev_mode_key_provisioning.h"

/* TI-Driver includes. */
#include <ti/drivers/GPIO.h>
#include <ti/drivers/SPI.h>
#include <ti/drivers/net/wifi/simplelink.h>

#include "Board.h"

#include "iot_network_manager_private.h"
/* Platform layer types include. */
#include "types/iot_platform_types.h"
#include "ota.h"

/* Logging Task Defines. */
#define mainLOGGING_MESSAGE_QUEUE_LENGTH    ( 15 )
#define mainLOGGING_TASK_STACK_SIZE         ( configMINIMAL_STACK_SIZE * 8 )

/* Application version info. */
#include "aws_version.h"
#include "aws_application_version.h"

#include "iot_threads.h"


/* The length of the logging task's queue to hold messages. */
#define mainLOGGING_MESSAGE_QUEUE_LENGTH    ( 15 )

/* The task delay for allowing the lower priority logging task to print out Wi-Fi
 * failure status before blocking indefinitely. */
#define mainLOGGING_WIFI_STATUS_DELAY       pdMS_TO_TICKS( 1000 )

void vApplicationDaemonTaskStartupHook( void );
static CK_RV prvProvisionRootCA( void );
static void prvShowTiCc3220SecurityAlertCounts( void );

/**
 * @brief Performs board and logging initializations, then starts the OS.
 *
 * Functions that require the OS to be running
 * are handled in vApplicationDaemonTaskStartupHook().
 *
 * @sa vApplicationDaemonTaskStartupHook()
 *
 * @return This function should not return.
 */
int main( void )
{
    /* Call board init functions. */
    Board_initGeneral();

    /* Start logging task. */
    xLoggingTaskInitialize( mainLOGGING_TASK_STACK_SIZE,
                            tskIDLE_PRIORITY,
                            mainLOGGING_MESSAGE_QUEUE_LENGTH );

    /* Start the FreeRTOS scheduler. */
    vTaskStartScheduler();

    return( 0 );
}

/*-----------------------------------------------------------*/

/**
 * @brief Completes board, Wi-Fi, and AWS system initializations
 * and creates the test runner task.
 *
 * This task is run when configUSE_DAEMON_TASK_STARTUP_HOOK = 1.
 */
void vApplicationDaemonTaskStartupHook( void )
{
    UART_Handle xtUartHndl;

    /* Hardware initialization required after the RTOS is running. */
    GPIO_init();
    SPI_init();

    /* Configure the UART. */
    xtUartHndl = InitTerm();
    UART_control( xtUartHndl, UART_CMD_RXDISABLE, NULL );

    // Emit some serial port debugging
    vTaskDelay( mainLOGGING_WIFI_STATUS_DELAY );

    /* Initialize the AWS Libraries system. */
    if( SYSTEM_Init() == pdPASS )
    {
        WIFI_On();

        //WIFINetworkParams_t xNetworkParams;
        WIFIReturnCode_t xWifiStatus;
        //xNetworkParams.pcSSID = NULL;
        //xNetworkParams.ucSSIDLength = 0;
        //xNetworkParams.pcPassword = NULL;
        //xNetworkParams.ucPasswordLength = 0;
        //xNetworkParams.xSecurity = eWiFiSecurityWPA2;

        xWifiStatus = WIFI_ConnectAP( NULL );
        if(xWifiStatus == eWiFiSuccess)
        {
            //RunCoreMqttMutualAuthDemo();
            Iot_CreateDetachedThread( vStartOTAUpdateDemoTask,
                                      NULL,
                                      democonfigDEMO_PRIORITY,
                                      democonfigDEMO_STACKSIZE );
        }else{
            WIFI_Off();
            AP_Task(NULL);
        }
    }
}

/* ----------------------------------------------------------*/

/**
 * @brief Imports the trusted Root CA required for a connection to
 * AWS IoT endpoint.
 */
CK_RV prvProvisionRootCA( void )
{
    uint8_t * pucRootCA = NULL;
    uint32_t ulRootCALength = 0;
    CK_RV xResult = CKR_OK;
    CK_SESSION_HANDLE xSessionHandle;
    CK_OBJECT_HANDLE xCertificateHandle;

    /* Use either Verisign or Starfield root CA,
     * depending on whether this is an ATS endpoint. */
    if( strstr( clientcredentialMQTT_BROKER_ENDPOINT, "-ats.iot" ) == NULL )
    {
        pucRootCA = ( uint8_t * ) tlsVERISIGN_ROOT_CERTIFICATE_PEM;
        ulRootCALength = tlsVERISIGN_ROOT_CERTIFICATE_LENGTH;
    }
    else
    {
        pucRootCA = ( uint8_t * ) tlsSTARFIELD_ROOT_CERTIFICATE_PEM;
        ulRootCALength = tlsSTARFIELD_ROOT_CERTIFICATE_LENGTH;
    }

    xResult = xInitializePkcs11Session( &xSessionHandle );

    if( xResult == CKR_OK )
    {
        xResult = xProvisionCertificate( xSessionHandle,
                                         pucRootCA,
                                         ulRootCALength,
                                         pkcs11configLABEL_ROOT_CERTIFICATE,
                                         &xCertificateHandle );
    }

    return xResult;
}

/*-----------------------------------------------------------*/

/**
 * @brief In the Texas Instruments CC3220(SF) device, we retrieve the number of security alerts and the threshold.
 */
static void prvShowTiCc3220SecurityAlertCounts( void )
{
    int32_t lResult;
    SlFsControlGetStorageInfoResponse_t xStorageResponseInfo;

    lResult = sl_FsCtl( ( SlFsCtl_e ) SL_FS_CTL_GET_STORAGE_INFO, 0, NULL, NULL, 0, ( _u8 * ) &xStorageResponseInfo, sizeof( SlFsControlGetStorageInfoResponse_t ), NULL );

    if( lResult == 0 )
    {
        configPRINTF( ( "Security alert threshold = %d\r\n", xStorageResponseInfo.FilesUsage.NumOfAlertsThreshold ) );
        configPRINTF( ( "Current number of alerts = %d\r\n", xStorageResponseInfo.FilesUsage.NumOfAlerts ) );
    }
    else
    {
        configPRINTF( ( "sl_FsCtl failed with error code: %d\r\n", lResult ) );
    }
}

/* configUSE_STATIC_ALLOCATION is set to 1, so the application must provide an
 * implementation of vApplicationGetIdleTaskMemory() to provide the memory that is
 * used by the Idle task. */
void vApplicationGetIdleTaskMemory( StaticTask_t ** ppxIdleTaskTCBBuffer,
                                    StackType_t ** ppxIdleTaskStackBuffer,
                                    uint32_t * pulIdleTaskStackSize )
{
    /* If the buffers to be provided to the Idle task are declared inside this
     * function then they must be declared static - otherwise they will be allocated on
     * the stack and so not exists after this function exits. */
    static StaticTask_t xIdleTaskTCB;
    static StackType_t uxIdleTaskStack[ configMINIMAL_STACK_SIZE ];

    /* Pass out a pointer to the StaticTask_t structure in which the Idle
     * task's state will be stored. */
    *ppxIdleTaskTCBBuffer = &xIdleTaskTCB;

    /* Pass out the array that will be used as the Idle task's stack. */
    *ppxIdleTaskStackBuffer = uxIdleTaskStack;

    /* Pass out the size of the array pointed to by *ppxIdleTaskStackBuffer.
     * Note that, as the array is necessarily of type StackType_t,
     * configMINIMAL_STACK_SIZE is specified in words, not bytes. */
    *pulIdleTaskStackSize = configMINIMAL_STACK_SIZE;
}
/*-----------------------------------------------------------*/

/**
 * @brief This is to provide the memory that is used by the RTOS daemon/time task.
 *
 * If configUSE_STATIC_ALLOCATION is set to 1, so the application must provide an
 * implementation of vApplicationGetTimerTaskMemory() to provide the memory that is
 * used by the RTOS daemon/time task.
 */
void vApplicationGetTimerTaskMemory( StaticTask_t ** ppxTimerTaskTCBBuffer,
                                     StackType_t ** ppxTimerTaskStackBuffer,
                                     uint32_t * pulTimerTaskStackSize )
{
    /* If the buffers to be provided to the Timer task are declared inside this
     * function then they must be declared static - otherwise they will be allocated on
     * the stack and so not exists after this function exits. */
    static StaticTask_t xTimerTaskTCB;
    static StackType_t uxTimerTaskStack[ configTIMER_TASK_STACK_DEPTH ];

    /* Pass out a pointer to the StaticTask_t structure in which the Idle
     * task's state will be stored. */
    *ppxTimerTaskTCBBuffer = &xTimerTaskTCB;

    /* Pass out the array that will be used as the Timer task's stack. */
    *ppxTimerTaskStackBuffer = uxTimerTaskStack;

    /* Pass out the size of the array pointed to by *ppxTimerTaskStackBuffer.
     * Note that, as the array is necessarily of type StackType_t,
     * configMINIMAL_STACK_SIZE is specified in words, not bytes. */
    *pulTimerTaskStackSize = configTIMER_TASK_STACK_DEPTH;
}
/*-----------------------------------------------------------*/

/**
 * @brief Warn user if pvPortMalloc fails.
 *
 * Called if a call to pvPortMalloc() fails because there is insufficient
 * free memory available in the FreeRTOS heap.  pvPortMalloc() is called
 * internally by FreeRTOS API functions that create tasks, queues, software
 * timers, and semaphores.  The size of the FreeRTOS heap is set by the
 * configTOTAL_HEAP_SIZE configuration constant in FreeRTOSConfig.h.
 *
*/
void vApplicationMallocFailedHook()
{
    configPRINT_STRING( ( "ERROR: Malloc failed to allocate memory\r\n" ) );
    taskDISABLE_INTERRUPTS();

    /* Loop forever */
    for( ; ; )
    {
    }
}

/*-----------------------------------------------------------*/

/**
 * @brief Loop forever if stack overflow is detected.
 *
 * If configCHECK_FOR_STACK_OVERFLOW is set to 1,
 * this hook provides a location for applications to
 * define a response to a stack overflow.
 *
 * Use this hook to help identify that a stack overflow
 * has occurred.
 *
 */
void vApplicationStackOverflowHook( TaskHandle_t xTask,
                                    char * pcTaskName )
{
    configPRINT_STRING( ( "ERROR: stack overflow\r\n" ) );
    portDISABLE_INTERRUPTS();

    /* Unused Parameters */
    ( void ) xTask;
    ( void ) pcTaskName;

    /* Loop forever */
    for( ; ; )
    {
    }
}
