/*
 * FreeRTOS V202012.00
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

/**
 * @file aws_iot_ota_update_demo.c
 * @brief A simple OTA update example.
 *
 * This example initializes the OTA agent to enable OTA updates via the
 * MQTT broker. It simply connects to the MQTT broker with the users
 * credentials and spins in an indefinite loop to allow MQTT messages to be
 * forwarded to the OTA agent for possible processing. The OTA agent does all
 * of the real work; checking to see if the message topic is one destined for
 * the OTA agent. If not, it is simply ignored.
 */

/* The config header is always included first. */
#include "iot_config.h"

/* Standard includes. */
#include <stdio.h>
#include <string.h>

/* MQTT include. */
#include "iot_mqtt.h"

/* Platform includes for demo. */
#include "platform/iot_clock.h"

/* Set up logging for this demo. */
#include "iot_demo_logging.h"

/* Required to get the broker address and port. */
#include "aws_clientcredential.h"

/* FreeRTOS OTA agent includes. */
#include "aws_iot_ota_agent.h"

/* Required for demo task stack and priority */
#include "aws_demo_config.h"
#include "aws_application_version.h"

#include "iot_init.h"

/**
 * @brief Timeout for MQTT connection, if the MQTT connection is not established within
 * this time, the connect function returns #IOT_MQTT_TIMEOUT
 */
#define OTA_DEMO_CONNECTION_TIMEOUT_MS               ( 2000UL )

/**
 * @brief The maximum time interval that is permitted to elapse between the point at
 * which the MQTT client finishes transmitting one control Packet and the point it starts
 * sending the next.In the absence of control packet a PINGREQ  is sent. The broker must
 * disconnect a client that does not send a message or a PINGREQ packet in one and a
 * half times the keep alive interval.
 */
#define OTA_DEMO_KEEP_ALIVE_SECONDS                  ( 120UL )

/**
 * @brief The delay used in the main OTA Demo task loop to periodically output the OTA
 * statistics like number of packets received, dropped, processed and queued per connection.
 */
#define OTA_DEMO_TASK_DELAY_SECONDS                  ( 2UL )

/**
 * @brief The base interval in seconds for retrying network connection.
 */
#define OTA_DEMO_CONN_RETRY_BASE_INTERVAL_SECONDS    ( 4U )

/**
 * @brief The maximum interval in seconds for retrying network connection.
 */
#define OTA_DEMO_CONN_RETRY_MAX_INTERVAL_SECONDS     ( 360U )

/**
 * @brief The longest client identifier that an MQTT server must accept (as defined
 * by the MQTT 3.1.1 spec) is 23 characters. Add 1 to include the length of the NULL
 * terminator.
 */
#define OTA_DEMO_CLIENT_IDENTIFIER_MAX_LENGTH        ( 24 )

/**
 * @brief Handle of the MQTT connection used in this demo.
 */
static IotMqttConnection_t _mqttConnection = IOT_MQTT_CONNECTION_INITIALIZER;

/**
 * @brief Flag used to unset, during disconnection of currently connected network. This will
 * trigger a reconnection from the OTA demo task.
 */
volatile static bool _networkConnected = false;

/**
 * @brief Connection retry interval in seconds.
 */
static int _retryInterval = OTA_DEMO_CONN_RETRY_BASE_INTERVAL_SECONDS;

static const char * _pStateStr[ eOTA_AgentState_All ] =
{
    "Init",
    "Ready",
    "RequestingJob",
    "WaitingForJob",
    "CreatingFile",
    "RequestingFileBlock",
    "WaitingForFileBlock",
    "ClosingFile",
    "Suspended",
    "ShuttingDown",
    "Stopped"
};

/**
 * @brief Initialize the libraries required for OTA demo.
 *
 * @return `EXIT_SUCCESS` if all libraries were successfully initialized;
 * `EXIT_FAILURE` otherwise.
 */
static int _initializeOtaDemo( void )
{
    int status = EXIT_SUCCESS;
    IotMqttError_t mqttInitStatus = IOT_MQTT_SUCCESS;
    bool commonLibrariesInitialized = false;

    if( IotSdk_Init() == true )
    {
        commonLibrariesInitialized = true;
    }
    else
    {
        IotLogInfo( "Failed to initialize the common library." );
        status = EXIT_FAILURE;
    }

    /* Initialize the MQTT library.*/
    if( status == EXIT_SUCCESS )
    {
        mqttInitStatus = IotMqtt_Init();

        if( mqttInitStatus != IOT_MQTT_SUCCESS )
        {
            /* Failed to initialize MQTT library.*/
            status = EXIT_FAILURE;
        }
    }

    if(status == EXIT_FAILURE)
    {
        if( commonLibrariesInitialized == true )
        {
            IotSdk_Cleanup();
        }
    }

    return status;
}

/**
 * @brief Clean up libraries initialized for OTA demo.
 */
static void _cleanupOtaDemo( void )
{
    /* Cleanup MQTT library.*/
    IotMqtt_Cleanup();
}

/**
 * @brief Delay before retrying network connection up to a maximum interval.
 */
static void _connectionRetryDelay( void )
{
    unsigned int retryIntervalwithJitter = 0;

    if( ( _retryInterval * 2 ) >= OTA_DEMO_CONN_RETRY_MAX_INTERVAL_SECONDS )
    {
        /* Retry interval is already max.*/
        _retryInterval = OTA_DEMO_CONN_RETRY_MAX_INTERVAL_SECONDS;
    }
    else
    {
        /* Double the retry interval time.*/
        _retryInterval *= 2;
    }

    /* Add random jitter upto current retry interval .*/
    retryIntervalwithJitter = _retryInterval + ( rand() % _retryInterval );

    IotLogInfo( "Retrying network connection in %d Secs ", retryIntervalwithJitter );

    /* Delay for the calculated time interval .*/
    IotClock_SleepMs( retryIntervalwithJitter * 1000 );
}

/**
 * @brief Initialize the libraries required for OTA demo.
 *
 * @return `EXIT_SUCCESS` if all libraries were successfully initialized;
 * `EXIT_FAILURE` otherwise.
 */

static void prvNetworkDisconnectCallback( void * param,
                                          IotMqttCallbackParam_t * mqttCallbackParams )
{
    ( void ) param;

    /* Log the reason for MQTT disconnect.*/
    switch( mqttCallbackParams->u.disconnectReason )
    {
        case IOT_MQTT_DISCONNECT_CALLED:
            IotLogInfo( "Mqtt disconnected due to invoking diconnect function.\r\n" );
            break;

        case IOT_MQTT_BAD_PACKET_RECEIVED:
            IotLogInfo( "Mqtt disconnected due to invalid packet received from the network.\r\n" );
            break;

        case IOT_MQTT_KEEP_ALIVE_TIMEOUT:
            IotLogInfo( "Mqtt disconnected due to Keep-alive response not received.\r\n" );
            break;

        default:
            IotLogInfo( "Mqtt disconnected due to unknown reason." );
            break;
    }

    /* Clear the flag for network connection status.*/
    _networkConnected = false;
}

/**
 * @brief Establish a new connection to the MQTT server.
 *
 * @param[in] awsIotMqttMode Specify if this demo is running with the AWS IoT
 * MQTT server. Set this to `false` if using another MQTT server.
 * @param[in] pIdentifier NULL-terminated MQTT client identifier.
 * @param[in] pNetworkServerInfo Passed to the MQTT connect function when
 * establishing the MQTT connection.
 * @param[in] pNetworkCredentialInfo Passed to the MQTT connect function when
 * establishing the MQTT connection.
 * @param[in] pNetworkInterface The network interface to use for this demo.
 * @param[out] pMqttConnection Set to the handle to the new MQTT connection.
 *
 * @return `EXIT_SUCCESS` if the connection is successfully established; `EXIT_FAILURE`
 * otherwise.
 */
static int _establishMqttConnection( const char * pIdentifier,
                                     void * pNetworkServerInfo,
                                     void * pNetworkCredentialInfo,
                                     const IotNetworkInterface_t * pNetworkInterface,
                                     IotMqttConnection_t * pMqttConnection )
{
    int status = EXIT_SUCCESS;
    IotMqttError_t connectStatus = IOT_MQTT_STATUS_PENDING;
    IotMqttNetworkInfo_t networkInfo = IOT_MQTT_NETWORK_INFO_INITIALIZER;
    IotMqttConnectInfo_t connectInfo = IOT_MQTT_CONNECT_INFO_INITIALIZER;
    IotMqttPublishInfo_t willInfo = IOT_MQTT_PUBLISH_INFO_INITIALIZER;
    char pClientIdentifierBuffer[ OTA_DEMO_CLIENT_IDENTIFIER_MAX_LENGTH ] = { 0 };

    /* Set the members of the network info not set by the initializer. This
     * struct provided information on the transport layer to the MQTT connection. */
    networkInfo.createNetworkConnection = true;
    networkInfo.u.setup.pNetworkServerInfo = pNetworkServerInfo;
    networkInfo.u.setup.pNetworkCredentialInfo = pNetworkCredentialInfo;
    networkInfo.pNetworkInterface = pNetworkInterface;
    networkInfo.disconnectCallback.function = prvNetworkDisconnectCallback;

    /* Set the members of the connection info not set by the initializer. */
    connectInfo.awsIotMqttMode = true;//using an aws mqtt server
    connectInfo.cleanSession = true;
    connectInfo.awsIotMqttMode = true;
    connectInfo.keepAliveSeconds = OTA_DEMO_KEEP_ALIVE_SECONDS;
    connectInfo.clientIdentifierLength = ( uint16_t ) strlen( clientcredentialIOT_THING_NAME );
    connectInfo.pClientIdentifier = clientcredentialIOT_THING_NAME;

    /* Establish the MQTT connection. */
    if( status == EXIT_SUCCESS )
    {
        IotLogInfo( "MQTT demo client identifier is %.*s (length %hu).",
                    connectInfo.clientIdentifierLength,
                    connectInfo.pClientIdentifier,
                    connectInfo.clientIdentifierLength );

        connectStatus = IotMqtt_Connect( &networkInfo,
                                         &connectInfo,
                                         OTA_DEMO_CONNECTION_TIMEOUT_MS,
                                         pMqttConnection );

        if( connectStatus != IOT_MQTT_SUCCESS )
        {
            IotLogError( "MQTT CONNECT returned error %s.",
                         IotMqtt_strerror( connectStatus ) );

            status = EXIT_FAILURE;
        }
    }

    return status;
}

/**
 * @brief The OTA agent has completed the update job or it is in
 * self test mode. If it was accepted, we want to activate the new image.
 * This typically means we should reset the device to run the new firmware.
 * If now is not a good time to reset the device, it may be activated later
 * by your user code. If the update was rejected, just return without doing
 * anything and we'll wait for another job. If it reported that we should
 * start test mode, normally we would perform some kind of system checks to
 * make sure our new firmware does the basic things we think it should do
 * but we'll just go ahead and set the image as accepted for demo purposes.
 * The accept function varies depending on your platform. Refer to the OTA
 * PAL implementation for your platform in aws_ota_pal.c to see what it
 * does for you.
 *
 * @param[in] eEvent Specify if this demo is running with the AWS IoT
 * MQTT server. Set this to `false` if using another MQTT server.
 * @return None.
 */
static void App_OTACompleteCallback( OTA_JobEvent_t eEvent )
{
    OTA_Err_t xErr = kOTA_Err_Uninitialized;

    DEFINE_OTA_METHOD_NAME( "App_OTACompleteCallback" );

    /* OTA job is completed. so delete the MQTT and network connection. */
    if( eEvent == eOTA_JobEvent_Activate )
    {
        IotLogInfo( "Received eOTA_JobEvent_Activate callback from OTA Agent.\r\n" );

        /* OTA job is completed. so delete the network connection. */
        if( _mqttConnection != NULL )
        {
            IotMqtt_Disconnect( _mqttConnection, false );
        }

        /* Activate the new firmware image. */
        OTA_ActivateNewImage();

        /* We should never get here as new image activation must reset the device.*/
        IotLogError( "New image activation failed.\r\n" );

        for( ; ; )
        {
        }
    }
    else if( eEvent == eOTA_JobEvent_Fail )
    {
        IotLogInfo( "Received eOTA_JobEvent_Fail callback from OTA Agent.\r\n" );

        /* Nothing special to do. The OTA agent handles it. */
    }
    else if( eEvent == eOTA_JobEvent_StartTest )
    {
        /* This demo just accepts the image since it was a good OTA update and networking
         * and services are all working (or we wouldn't have made it this far). If this
         * were some custom device that wants to test other things before calling it OK,
         * this would be the place to kick off those tests before calling OTA_SetImageState()
         * with the final result of either accepted or rejected. */

        IotLogInfo( "Received eOTA_JobEvent_StartTest callback from OTA Agent.\r\n" );
        xErr = OTA_SetImageState( eOTA_ImageState_Accepted );

        if( xErr != kOTA_Err_None )
        {
            IotLogError( " Error! Failed to set image state as accepted.\r\n" );
        }
    }
}

/**
 * @brief The function that implements the main OTA demo task loop. It first
 * establishes the connection , initializes the OTA Agent, keeps logging
 * OTA statistics and restarts the process if OTA Agent stops.
 *
 * @param[in] awsIotMqttMode Specify if this demo is running with the AWS IoT
 * MQTT server. Set this to `false` if using another MQTT server.
 * @param[in] pIdentifier NULL-terminated MQTT client identifier.
 * @param[in] pNetworkServerInfo Passed to the MQTT connect function when
 * establishing the MQTT connection.
 * @param[in] pNetworkCredentialInfo Passed to the MQTT connect function when
 * establishing the MQTT connection.
 * @param[in] pNetworkInterface The network interface to use for this demo.
 *
 * @return `EXIT_SUCCESS` if the demo completes successfully; `EXIT_FAILURE` otherwise.
 */

//#include "types/iot_network_types.h"
//#include "aws_iot_network_config.h"

#include "platform/iot_network_freertos.h"
#include "iot_secure_sockets.h"
#include "ota.h"
/*root ca certificates*/
#include "iot_root_certificates.h"

IotNetworkCredentials_t tcpIPCredentials;
IotNetworkServerInfo_t tcpIPConnectionParams;
const IotNetworkInterface_t * pNetworkInterface;

void vRunOTAUpdateDemo( const char * pIdentifier)
{
    OTA_State_t eState;
    OTA_ImageState_t eImageState;
    static OTA_ConnectionContext_t xOTAConnectionCtx;

    IotLogInfo( "OTA demo version %u.%u.%u\r\n",
                xAppFirmwareVersion.u.x.ucMajor,
                xAppFirmwareVersion.u.x.ucMinor,
                xAppFirmwareVersion.u.x.usBuild );

    for( ; ; )
    {
        IotLogInfo( "Connecting to broker...\r\n" );

        pNetworkInterface = IOT_NETWORK_INTERFACE_AFR;

        tcpIPConnectionParams.pHostName = clientcredentialMQTT_BROKER_ENDPOINT;
        tcpIPConnectionParams.port = clientcredentialMQTT_BROKER_PORT;

        if( tcpIPConnectionParams.port == 443 )
        {
            tcpIPCredentials.pAlpnProtos = socketsAWS_IOT_ALPN_MQTT;
        }
        else
        {
            tcpIPCredentials.pAlpnProtos = NULL;
        }

        tcpIPCredentials.maxFragmentLength = 0;
        tcpIPCredentials.disableSni = false;
        tcpIPCredentials.pRootCa = democonfigROOT_CA_PEM;
        tcpIPCredentials.rootCaSize = sizeof(democonfigROOT_CA_PEM);
        tcpIPCredentials.pClientCert = keyCLIENT_CERTIFICATE_PEM;
        tcpIPCredentials.clientCertSize = sizeof( keyCLIENT_CERTIFICATE_PEM );
        tcpIPCredentials.pPrivateKey = keyCLIENT_PRIVATE_KEY_PEM;
        tcpIPCredentials.privateKeySize = sizeof( keyCLIENT_PRIVATE_KEY_PEM );

        /* Establish a new MQTT connection. */
        if( _establishMqttConnection( pIdentifier,
                                      (void *)&tcpIPConnectionParams,
                                      (void *)&tcpIPCredentials,
                                      pNetworkInterface,
                                      &_mqttConnection ) == EXIT_SUCCESS )
        {
            /* Update the connection context shared with OTA Agent.*/
            xOTAConnectionCtx.pxNetworkInterface = (void *) pNetworkInterface;
            xOTAConnectionCtx.pvNetworkCredentials = (void *) &tcpIPCredentials;
            xOTAConnectionCtx.pvControlClient = _mqttConnection;

            /* Set the base interval for connection retry.*/
            _retryInterval = OTA_DEMO_CONN_RETRY_BASE_INTERVAL_SECONDS;

            /* Update the connection available flag.*/
            _networkConnected = true;

            /* Check if OTA Agent is suspended and resume.*/
            if( ( eState = OTA_GetAgentState() ) == eOTA_AgentState_Suspended )
            {
                OTA_Resume( &xOTAConnectionCtx );
            }

            /* Initialize the OTA Agent , if it is resuming the OTA statistics will be cleared for new connection.*/
            OTA_AgentInit( ( void * ) ( &xOTAConnectionCtx ),
                           ( const uint8_t * ) ( clientcredentialIOT_THING_NAME ),
                           App_OTACompleteCallback,
                           ( TickType_t ) ~0 );


            //OTA_CheckForUpdate();
            //OTA_GetImageState == eOTA_ImageState_Aborted
            while( ( ( eState = OTA_GetAgentState() ) != eOTA_AgentState_Stopped ) && ( ( eImageState = OTA_GetImageState() ) != eOTA_ImageState_Aborted ) && _networkConnected )
            {
                /* Wait forever for OTA traffic but allow other tasks to run and output statistics only once per second. */
                IotClock_SleepMs( OTA_DEMO_TASK_DELAY_SECONDS * 1000 );

                IotLogInfo( "State: %s  Received: %u   Queued: %u   Processed: %u   Dropped: %u\r\n", _pStateStr[ eState ],
                            OTA_GetPacketsReceived(), OTA_GetPacketsQueued(), OTA_GetPacketsProcessed(), OTA_GetPacketsDropped() );
            }
            IotLogInfo( "State: %s" , _pStateStr[ eState ]);

            /* Check if we got network disconnect callback and suspend OTA Agent.*/
            if( _networkConnected == false )
            {
                /* Suspend OTA agent.*/
                if( OTA_Suspend() == kOTA_Err_None )
                {
                    while( ( eState = OTA_GetAgentState() ) != eOTA_AgentState_Suspended )
                    {
                        /* Wait for OTA Agent to process the suspend event. */
                        IotClock_SleepMs( OTA_DEMO_TASK_DELAY_SECONDS * 1000 );
                    }
                }
            }
            /*ota stopped because OTA image abort or OTA agent state is stopped*/
            else
            {

                OTA_AgentShutdown(pdMS_TO_TICKS( 10 * 1000 ));
                /* Try to close the MQTT connection. */
                if( _mqttConnection != NULL )
                {
                    IotMqtt_Disconnect( _mqttConnection, 0 );
                }
                /*exit this loop, will attempt to OTA after a restart*/
                break;
            }
        }
        else
        {
            IotLogError( "ERROR:  MQTT_AGENT_Connect() Failed.\r\n" );
        }

        /* After failure to connect or a disconnect, delay for retrying connection. */
        _connectionRetryDelay();
    }
}

/**
 * @brief The function that runs the OTA demo, called by the demo runner.
 *
 * @param[in] awsIotMqttMode Specify if this demo is running with the AWS IoT
 * MQTT server. Set this to `false` if using another MQTT server.
 * @param[in] pIdentifier NULL-terminated MQTT client identifier.
 * @param[in] pNetworkServerInfo Passed to the MQTT connect function when
 * establishing the MQTT connection.
 * @param[in] pNetworkCredentialInfo Passed to the MQTT connect function when
 * establishing the MQTT connection.
 * @param[in] pNetworkInterface The network interface to use for this demo.
 *
 * @return `EXIT_SUCCESS` if the demo completes successfully; `EXIT_FAILURE` otherwise.
 */
void vStartOTAUpdateDemoTask(void * params)
{
    /* Return value of this function and the exit status of this program. */
    int status = EXIT_SUCCESS;

    /* Flags for tracking which cleanup functions must be called. */
    bool otademoInitialized = false;

    /* Initialize the libraries required for this demo. */
    status = _initializeOtaDemo();

    if( status == EXIT_SUCCESS )
    {
        otademoInitialized = true;

        /* Start OTA Agent.*/
        vRunOTAUpdateDemo( clientcredentialIOT_THING_NAME );
    }

    /* Clean up libraries if they were initialized. */
    if( otademoInitialized == true )
    {
        _cleanupOtaDemo();
    }

    //return status;
}


///*
// * FreeRTOS V202012.00
// * Copyright (C) 2020 Amazon.com, Inc. or its affiliates.  All Rights Reserved.
// *
// * Permission is hereby granted, free of charge, to any person obtaining a copy of
// * this software and associated documentation files (the "Software"), to deal in
// * the Software without restriction, including without limitation the rights to
// * use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
// * the Software, and to permit persons to whom the Software is furnished to do so,
// * subject to the following conditions:
// *
// * The above copyright notice and this permission notice shall be included in all
// * copies or substantial portions of the Software.
// *
// * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
// * FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
// * COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
// * IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
// * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
// *
// * http://www.FreeRTOS.org
// * http://aws.amazon.com/freertos
// *
// */
//
///*
// * Demo for showing use of the MQTT API using a mutually authenticated
// * network connection.
// *
// * The Example shown below uses MQTT APIs to create MQTT messages and send them
// * over the mutually authenticated network connection established with the
// * MQTT broker. This example is single threaded and uses statically allocated
// * memory. It uses QoS1 for sending to and receiving messages from the broker.
// *
// * A mutually authenticated TLS connection is used to connect to the
// * MQTT message broker in this example. Define democonfigMQTT_BROKER_ENDPOINT
// * and democonfigROOT_CA_PEM, in mqtt_demo_mutual_auth_config.h, and the client
// * private key and certificate, in aws_clientcredential_keys.h, to establish a
// * mutually authenticated connection.
// */
//
///**
// * @file mqtt_demo_mutual_auth.c
// * @brief Demonstrates usage of the MQTT library.
// */
//
///* Standard includes. */
//#include <string.h>
//#include <stdio.h>
//#include <stdlib.h>
//
///* Demo Specific configs. */
//#include "ota.h"
//
///* Kernel includes. */
//#include "FreeRTOS.h"
//#include "task.h"
//
///* MQTT library includes. */
//#include "core_mqtt.h"
//
///* Retry utilities include. */
//#include "backoff_algorithm.h"
//
///* Include header for root CA certificates. */
//#include "pkcs11_helpers.h""
//
///* Transport interface implementation include header for TLS. */
//#include "transport_secure_sockets.h"
//
///* Include header for connection configurations. */
//#include "aws_clientcredential.h"
//#include "aws_clientcredential_keys.h"
//
///* Include header for root CA certificates. */
//#include "iot_default_root_certificates.h"
//
///* Include AWS IoT metrics macros header. */
//#include "aws_iot_metrics.h"
//
///*------------- Demo configurations -------------------------*/
//
///** Note: The device client certificate and private key credentials are
// * obtained by the transport interface implementation (with Secure Sockets)
// * from the demos/include/aws_clientcredential_keys.h file.
// *
// * The following macros SHOULD be defined for this demo which uses both server
// * and client authentications for TLS session:
// *   - keyCLIENT_CERTIFICATE_PEM for client certificate.
// *   - keyCLIENT_PRIVATE_KEY_PEM for client private key.
// */
//
///**
// * @brief The MQTT broker endpoint used for this demo.
// */
//#ifndef democonfigMQTT_BROKER_ENDPOINT
//    #define democonfigMQTT_BROKER_ENDPOINT    clientcredentialMQTT_BROKER_ENDPOINT
//#endif
//
///**
// * @brief The root CA certificate belonging to the broker.
// */
//#ifndef democonfigROOT_CA_PEM
//    #define democonfigROOT_CA_PEM    tlsATS1_ROOT_CERTIFICATE_PEM
//#endif
//
//#ifndef democonfigCLIENT_IDENTIFIER
//
///**
// * @brief The MQTT client identifier used in this example.  Each client identifier
// * must be unique so edit as required to ensure no two clients connecting to the
// * same broker use the same client identifier.
// */
//    #define democonfigCLIENT_IDENTIFIER    clientcredentialIOT_THING_NAME
//#endif
//
//#ifndef democonfigMQTT_BROKER_PORT
//
///**
// * @brief The port to use for the demo.
// */
//    #define democonfigMQTT_BROKER_PORT    clientcredentialMQTT_BROKER_PORT
//#endif
//
///**
// * @brief The maximum number of times to run the subscribe publish loop in this
// * demo.
// */
//#ifndef democonfigMQTT_MAX_DEMO_COUNT
//    #define democonfigMQTT_MAX_DEMO_COUNT    ( 3 )
//#endif
///*-----------------------------------------------------------*/
//
///**
// * @brief The maximum number of retries for network operation with server.
// */
//#define RETRY_MAX_ATTEMPTS                                ( 5U )
//
///**
// * @brief The maximum back-off delay (in milliseconds) for retrying failed operation
// *  with server.
// */
//#define RETRY_MAX_BACKOFF_DELAY_MS                        ( 5000U )
//
///**
// * @brief The base back-off delay (in milliseconds) to use for network operation retry
// * attempts.
// */
//#define RETRY_BACKOFF_BASE_MS                             ( 500U )
//
///**
// * @brief Timeout for receiving CONNACK packet in milliseconds.
// */
//#define mqttexampleCONNACK_RECV_TIMEOUT_MS                ( 1000U )
//
///**
// * @brief The topic to subscribe and publish to in the example.
// *
// * The topic name starts with the client identifier to ensure that each demo
// * interacts with a unique topic name.
// */
//#define mqttexampleTOPIC                                  democonfigCLIENT_IDENTIFIER "/example/topic"
//
///**
// * @brief The number of topic filters to subscribe.
// */
//#define mqttexampleTOPIC_COUNT                            ( 1 )
//
///**
// * @brief The MQTT message published in this example.
// */
//#define mqttexampleMESSAGE                                "Hello World!"
//
///**
// * @brief Time in ticks to wait between each cycle of the demo implemented
// * by RunCoreMqttMutualAuthDemo().
// */
//#define mqttexampleDELAY_BETWEEN_DEMO_ITERATIONS_TICKS    ( pdMS_TO_TICKS( 5000U ) )
//
///**
// * @brief Timeout for MQTT_ProcessLoop in milliseconds.
// */
//#define mqttexamplePROCESS_LOOP_TIMEOUT_MS                ( 700U )
//
///**
// * @brief The maximum number of times to call MQTT_ProcessLoop() when polling
// * for a specific packet from the broker.
// */
//#define MQTT_PROCESS_LOOP_PACKET_WAIT_COUNT_MAX           ( 30U )
//
///**
// * @brief Keep alive time reported to the broker while establishing
// * an MQTT connection.
// *
// * It is the responsibility of the Client to ensure that the interval between
// * Control Packets being sent does not exceed the this Keep Alive value. In the
// * absence of sending any other Control Packets, the Client MUST send a
// * PINGREQ Packet.
// */
//#define mqttexampleKEEP_ALIVE_TIMEOUT_SECONDS             ( 60U )
//
///**
// * @brief Delay (in ticks) between consecutive cycles of MQTT publish operations in a
// * demo iteration.
// *
// * Note that the process loop also has a timeout, so the total time between
// * publishes is the sum of the two delays.
// */
//#define mqttexampleDELAY_BETWEEN_PUBLISHES_TICKS          ( pdMS_TO_TICKS( 2000U ) )
//
///**
// * @brief Transport timeout in milliseconds for transport send and receive.
// */
//#define mqttexampleTRANSPORT_SEND_RECV_TIMEOUT_MS         ( 500U )
//
///**
// * @brief Milliseconds per second.
// */
//#define MILLISECONDS_PER_SECOND                           ( 1000U )
//
///**
// * @brief Milliseconds per FreeRTOS tick.
// */
//#define MILLISECONDS_PER_TICK                             ( MILLISECONDS_PER_SECOND / configTICK_RATE_HZ )
//
///*-----------------------------------------------------------*/
//
///**
// * @brief Each compilation unit that consumes the NetworkContext must define it.
// * It should contain a single pointer to the type of your desired transport.
// * When using multiple transports in the same compilation unit, define this pointer as void *.
// *
// * @note Transport stacks are defined in amazon-freertos/libraries/abstractions/transport/secure_sockets/transport_secure_sockets.h.
// */
//struct NetworkContext
//{
//    SecureSocketsTransportParams_t * pParams;
//};
//
///*-----------------------------------------------------------*/
//
///**
// * @brief Calculate and perform an exponential backoff with jitter delay for
// * the next retry attempt of a failed network operation with the server.
// *
// * The function generates a random number, calculates the next backoff period
// * with the generated random number, and performs the backoff delay operation if the
// * number of retries have not exhausted.
// *
// * @note The PKCS11 module is used to generate the random number as it allows access
// * to a True Random Number Generator (TRNG) if the vendor platform supports it.
// * It is recommended to seed the random number generator with a device-specific entropy
// * source so that probability of collisions from devices in connection retries is mitigated.
// *
// * @note The backoff period is calculated using the backoffAlgorithm library.
// *
// * @param[in, out] pxRetryAttempts The context to use for backoff period calculation
// * with the backoffAlgorithm library.
// *
// * @return pdPASS if calculating the backoff period was successful; otherwise pdFAIL
// * if there was failure in random number generation OR all retry attempts had exhausted.
// */
//static BaseType_t prvBackoffForRetry( BackoffAlgorithmContext_t * pxRetryParams );
//
///**
// * @brief Connect to MQTT broker with reconnection retries.
// *
// * If connection fails, retry is attempted after a timeout.
// * Timeout value will exponentially increase until maximum
// * timeout value is reached or the number of attempts are exhausted.
// *
// * @param[out] pxNetworkContext The output parameter to return the created network context.
// *
// * @return pdFAIL on failure; pdPASS on successful TLS+TCP network connection.
// */
//static BaseType_t prvConnectToServerWithBackoffRetries( NetworkContext_t * pNetworkContext );
//
///**
// * @brief Sends an MQTT Connect packet over the already connected TLS over TCP connection.
// *
// * @param[in, out] pxMQTTContext MQTT context pointer.
// * @param[in] xNetworkContext Network context.
// *
// * @return pdFAIL on failure; pdPASS on successful MQTT connection.
// */
//static BaseType_t prvCreateMQTTConnectionWithBroker( MQTTContext_t * pxMQTTContext,
//                                                     NetworkContext_t * pxNetworkContext );
//
///**
// * @brief Function to update variable #xTopicFilterContext with status
// * information from Subscribe ACK. Called by the event callback after processing
// * an incoming SUBACK packet.
// *
// * @param[in] Server response to the subscription request.
// */
//static void prvUpdateSubAckStatus( MQTTPacketInfo_t * pxPacketInfo );
//
///**
// * @brief Subscribes to the topic as specified in mqttexampleTOPIC at the top of
// * this file. In the case of a Subscribe ACK failure, then subscription is
// * retried using an exponential backoff strategy with jitter.
// *
// * @param[in] pxMQTTContext MQTT context pointer.
// *
// * @return pdFAIL on failure; pdPASS on successful SUBSCRIBE request.
// */
//static BaseType_t prvMQTTSubscribeWithBackoffRetries( MQTTContext_t * pxMQTTContext );
//
///**
// * @brief Publishes a message mqttexampleMESSAGE on mqttexampleTOPIC topic.
// *
// * @param[in] pxMQTTContext MQTT context pointer.
// *
// * @return pdFAIL on failure; pdPASS on successful PUBLISH operation.
// */
//static BaseType_t prvMQTTPublishToTopic( MQTTContext_t * pxMQTTContext );
//
///**
// * @brief Unsubscribes from the previously subscribed topic as specified
// * in mqttexampleTOPIC.
// *
// * @param[in] pxMQTTContext MQTT context pointer.
// *
// * @return pdFAIL on failure; pdPASS on successful UNSUBSCRIBE request.
// */
//static BaseType_t prvMQTTUnsubscribeFromTopic( MQTTContext_t * pxMQTTContext );
//
///**
// * @brief The timer query function provided to the MQTT context.
// *
// * @return Time in milliseconds.
// */
//static uint32_t prvGetTimeMs( void );
//
///**
// * @brief Process a response or ack to an MQTT request (PING, PUBLISH,
// * SUBSCRIBE or UNSUBSCRIBE). This function processes PINGRESP, PUBACK,
// * SUBACK, and UNSUBACK.
// *
// * @param[in] pxIncomingPacket is a pointer to structure containing deserialized
// * MQTT response.
// * @param[in] usPacketId is the packet identifier from the ack received.
// */
//static void prvMQTTProcessResponse( MQTTPacketInfo_t * pxIncomingPacket,
//                                    uint16_t usPacketId );
//
///**
// * @brief Process incoming Publish message.
// *
// * @param[in] pxPublishInfo is a pointer to structure containing deserialized
// * Publish message.
// */
//static void prvMQTTProcessIncomingPublish( MQTTPublishInfo_t * pxPublishInfo );
//
///**
// * @brief The application callback function for getting the incoming publishes,
// * incoming acks, and ping responses reported from the MQTT library.
// *
// * @param[in] pxMQTTContext MQTT context pointer.
// * @param[in] pxPacketInfo Packet Info pointer for the incoming packet.
// * @param[in] pxDeserializedInfo Deserialized information from the incoming packet.
// */
//static void prvEventCallback( MQTTContext_t * pxMQTTContext,
//                              MQTTPacketInfo_t * pxPacketInfo,
//                              MQTTDeserializedInfo_t * pxDeserializedInfo );
//
///**
// * @brief Helper function to wait for a specific incoming packet from the
// * broker.
// *
// * @param[in] pxMQTTContext MQTT context pointer.
// * @param[in] usPacketType Packet type to wait for.
// *
// * @return The return status from call to #MQTT_ProcessLoop API.
// */
//static MQTTStatus_t prvWaitForPacket( MQTTContext_t * pxMQTTContext,
//                                      uint16_t usPacketType );
//
///*-----------------------------------------------------------*/
//
///**
// * @brief Static buffer used to hold MQTT messages being sent and received.
// */
//static uint8_t ucSharedBuffer[ democonfigNETWORK_BUFFER_SIZE ];
//
///**
// * @brief Global entry time into the application to use as a reference timestamp
// * in the #prvGetTimeMs function. #prvGetTimeMs will always return the difference
// * between the current time and the global entry time. This will reduce the chances
// * of overflow for the 32 bit unsigned integer used for holding the timestamp.
// */
//static uint32_t ulGlobalEntryTimeMs;
//
///**
// * @brief Packet Identifier generated when Publish request was sent to the broker;
// * it is used to match received Publish ACK to the transmitted Publish packet.
// */
//static uint16_t usPublishPacketIdentifier;
//
///**
// * @brief Packet Identifier generated when Subscribe request was sent to the broker;
// * it is used to match received Subscribe ACK to the transmitted Subscribe packet.
// */
//static uint16_t usSubscribePacketIdentifier;
//
///**
// * @brief Packet Identifier generated when Unsubscribe request was sent to the broker;
// * it is used to match received Unsubscribe response to the transmitted Unsubscribe
// * request.
// */
//static uint16_t usUnsubscribePacketIdentifier;
//
///**
// * @brief MQTT packet type received from the MQTT broker.
// *
// * @note Only on receiving incoming PUBLISH, SUBACK, and UNSUBACK, this
// * variable is updated. For MQTT packets PUBACK and PINGRESP, the variable is
// * not updated since there is no need to specifically wait for it in this demo.
// * A single variable suffices as this demo uses single task and requests one operation
// * (of PUBLISH, SUBSCRIBE, UNSUBSCRIBE) at a time before expecting response from
// * the broker. Hence it is not possible to receive multiple packets of type PUBLISH,
// * SUBACK, and UNSUBACK in a single call of #prvWaitForPacket.
// * For a multi task application, consider a different method to wait for the packet, if needed.
// */
//static uint16_t usPacketTypeReceived = 0U;
//
///**
// * @brief A pair containing a topic filter and its SUBACK status.
// */
//typedef struct topicFilterContext
//{
//    const char * pcTopicFilter;
//    MQTTSubAckStatus_t xSubAckStatus;
//} topicFilterContext_t;
//
///**
// * @brief An array containing the context of a SUBACK; the SUBACK status
// * of a filter is updated when the event callback processes a SUBACK.
// */
//static topicFilterContext_t xTopicFilterContext[ mqttexampleTOPIC_COUNT ] =
//{
//    { mqttexampleTOPIC, MQTTSubAckFailure }
//};
//
//
///** @brief Static buffer used to hold MQTT messages being sent and received. */
//static MQTTFixedBuffer_t xBuffer =
//{
//    ucSharedBuffer,
//    democonfigNETWORK_BUFFER_SIZE
//};
//
///*-----------------------------------------------------------*/
//
//static BaseType_t prvBackoffForRetry( BackoffAlgorithmContext_t * pxRetryParams )
//{
//    BaseType_t xReturnStatus = pdFAIL;
//    uint16_t usNextRetryBackOff = 0U;
//    BackoffAlgorithmStatus_t xBackoffAlgStatus = BackoffAlgorithmSuccess;
//
//    /**
//     * To calculate the backoff period for the next retry attempt, we will
//     * generate a random number to provide to the backoffAlgorithm library.
//     *
//     * Note: The PKCS11 module is used to generate the random number as it allows access
//     * to a True Random Number Generator (TRNG) if the vendor platform supports it.
//     * It is recommended to use a random number generator seeded with a device-specific
//     * entropy source so that probability of collisions from devices in connection retries
//     * is mitigated.
//     */
//    uint32_t ulRandomNum = 0;
//
//    if( xPkcs11GenerateRandomNumber( ( uint8_t * ) &ulRandomNum,
//                                     sizeof( ulRandomNum ) ) == pdPASS )
//    {
//        /* Get back-off value (in milliseconds) for the next retry attempt. */
//        xBackoffAlgStatus = BackoffAlgorithm_GetNextBackoff( pxRetryParams, ulRandomNum, &usNextRetryBackOff );
//
//        if( xBackoffAlgStatus == BackoffAlgorithmRetriesExhausted )
//        {
//            LogError( ( "All retry attempts have exhausted. Operation will not be retried" ) );
//        }
//        else if( xBackoffAlgStatus == BackoffAlgorithmSuccess )
//        {
//            /* Perform the backoff delay. */
//            vTaskDelay( pdMS_TO_TICKS( usNextRetryBackOff ) );
//
//            xReturnStatus = pdPASS;
//
//            LogInfo( ( "Retry attempt %lu out of maximum retry attempts %lu.",
//                       ( pxRetryParams->attemptsDone + 1 ),
//                       pxRetryParams->maxRetryAttempts ) );
//        }
//    }
//    else
//    {
//        LogError( ( "Unable to retry operation with broker: Random number generation failed" ) );
//    }
//
//    return xReturnStatus;
//}
//
///*-----------------------------------------------------------*/
//
//static BaseType_t prvConnectToServerWithBackoffRetries( NetworkContext_t * pxNetworkContext )
//{
//    ServerInfo_t xServerInfo = { 0 };
//
//    SocketsConfig_t xSocketsConfig = { 0 };
//    BaseType_t xStatus = pdPASS;
//    TransportSocketStatus_t xNetworkStatus = TRANSPORT_SOCKET_STATUS_SUCCESS;
//    BackoffAlgorithmContext_t xReconnectParams;
//    BaseType_t xBackoffStatus = pdFALSE;
//
//    /* Set the credentials for establishing a TLS connection. */
//    /* Initializer server information. */
//    xServerInfo.pHostName = democonfigMQTT_BROKER_ENDPOINT;
//    xServerInfo.hostNameLength = strlen( democonfigMQTT_BROKER_ENDPOINT );
//    xServerInfo.port = democonfigMQTT_BROKER_PORT;
//
//    /* Configure credentials for TLS mutual authenticated session. */
//    xSocketsConfig.enableTls = true;
//    xSocketsConfig.pAlpnProtos = NULL;
//    xSocketsConfig.maxFragmentLength = 0;
//    xSocketsConfig.disableSni = false;
//    xSocketsConfig.pRootCa = democonfigROOT_CA_PEM;
//    xSocketsConfig.rootCaSize = sizeof( democonfigROOT_CA_PEM );
//    xSocketsConfig.sendTimeoutMs = mqttexampleTRANSPORT_SEND_RECV_TIMEOUT_MS;
//    xSocketsConfig.recvTimeoutMs = mqttexampleTRANSPORT_SEND_RECV_TIMEOUT_MS;
//
//    /* Initialize reconnect attempts and interval. */
//    BackoffAlgorithm_InitializeParams( &xReconnectParams,
//                                       RETRY_BACKOFF_BASE_MS,
//                                       RETRY_MAX_BACKOFF_DELAY_MS,
//                                       RETRY_MAX_ATTEMPTS );
//
//    /* Attempt to connect to MQTT broker. If connection fails, retry after
//     * a timeout. Timeout value will exponentially increase till maximum
//     * attempts are reached.
//     */
//    do
//    {
//        /* Establish a TLS session with the MQTT broker. This example connects to
//         * the MQTT broker as specified in democonfigMQTT_BROKER_ENDPOINT and
//         * democonfigMQTT_BROKER_PORT at the top of this file. */
//        LogInfo( ( "Creating a TLS connection to %s:%u.",
//                   democonfigMQTT_BROKER_ENDPOINT,
//                   democonfigMQTT_BROKER_PORT ) );
//        /* Attempt to create a mutually authenticated TLS connection. */
//        xNetworkStatus = SecureSocketsTransport_Connect( pxNetworkContext,
//                                                         &xServerInfo,
//                                                         &xSocketsConfig );
//
//        if( xNetworkStatus != TRANSPORT_SOCKET_STATUS_SUCCESS )
//        {
//            LogWarn( ( "Connection to the broker failed. Attempting connection retry after backoff delay." ) );
//
//            /* As the connection attempt failed, we will retry the connection after an
//             * exponential backoff with jitter delay. */
//
//            /* Calculate the backoff period for the next retry attempt and perform the wait operation. */
//            xBackoffStatus = prvBackoffForRetry( &xReconnectParams );
//        }
//    } while( ( xNetworkStatus != TRANSPORT_SOCKET_STATUS_SUCCESS ) && ( xBackoffStatus == pdPASS ) );
//
//    return xStatus;
//}
///*-----------------------------------------------------------*/
//
//static BaseType_t prvCreateMQTTConnectionWithBroker( MQTTContext_t * pxMQTTContext,
//                                                     NetworkContext_t * pxNetworkContext )
//{
//    MQTTStatus_t xResult;
//    MQTTConnectInfo_t xConnectInfo;
//    bool xSessionPresent;
//    TransportInterface_t xTransport;
//    BaseType_t xStatus = pdFAIL;
//
//    /* Fill in Transport Interface send and receive function pointers. */
//    xTransport.pNetworkContext = pxNetworkContext;
//    xTransport.send = SecureSocketsTransport_Send;
//    xTransport.recv = SecureSocketsTransport_Recv;
//
//    /* Initialize MQTT library. */
//    xResult = MQTT_Init( pxMQTTContext, &xTransport, prvGetTimeMs, prvEventCallback, &xBuffer );
//    configASSERT( xResult == MQTTSuccess );
//
//    /* Some fields are not used in this demo so start with everything at 0. */
//    ( void ) memset( ( void * ) &xConnectInfo, 0x00, sizeof( xConnectInfo ) );
//
//    /* Start with a clean session i.e. direct the MQTT broker to discard any
//     * previous session data. Also, establishing a connection with clean session
//     * will ensure that the broker does not store any data when this client
//     * gets disconnected. */
//    xConnectInfo.cleanSession = true;
//
//    /* The client identifier is used to uniquely identify this MQTT client to
//     * the MQTT broker. In a production device the identifier can be something
//     * unique, such as a device serial number. */
//    xConnectInfo.pClientIdentifier = democonfigCLIENT_IDENTIFIER;
//    xConnectInfo.clientIdentifierLength = ( uint16_t ) strlen( democonfigCLIENT_IDENTIFIER );
//
//    /* Use the metrics string as username to report the OS and MQTT client version
//     * metrics to AWS IoT. */
//    xConnectInfo.pUserName = AWS_IOT_METRICS_STRING;
//    xConnectInfo.userNameLength = AWS_IOT_METRICS_STRING_LENGTH;
//
//    /* Set MQTT keep-alive period. If the application does not send packets at an interval less than
//     * the keep-alive period, the MQTT library will send PINGREQ packets. */
//    xConnectInfo.keepAliveSeconds = mqttexampleKEEP_ALIVE_TIMEOUT_SECONDS;
//
//    /* Send MQTT CONNECT packet to broker. LWT is not used in this demo, so it
//     * is passed as NULL. */
//    xResult = MQTT_Connect( pxMQTTContext,
//                            &xConnectInfo,
//                            NULL,
//                            mqttexampleCONNACK_RECV_TIMEOUT_MS,
//                            &xSessionPresent );
//
//    if( xResult != MQTTSuccess )
//    {
//        LogError( ( "Failed to establish MQTT connection: Server=%s, MQTTStatus=%s",
//                    democonfigMQTT_BROKER_ENDPOINT, MQTT_Status_strerror( xResult ) ) );
//    }
//    else
//    {
//        /* Successfully established and MQTT connection with the broker. */
//        LogInfo( ( "An MQTT connection is established with %s.", democonfigMQTT_BROKER_ENDPOINT ) );
//        xStatus = pdPASS;
//    }
//
//    return xStatus;
//}
///*-----------------------------------------------------------*/
//
//static void prvUpdateSubAckStatus( MQTTPacketInfo_t * pxPacketInfo )
//{
//    MQTTStatus_t xResult = MQTTSuccess;
//    uint8_t * pucPayload = NULL;
//    size_t ulSize = 0;
//    uint32_t ulTopicCount = 0U;
//
//    xResult = MQTT_GetSubAckStatusCodes( pxPacketInfo, &pucPayload, &ulSize );
//
//    /* MQTT_GetSubAckStatusCodes always returns success if called with packet info
//     * from the event callback and non-NULL parameters. */
//    configASSERT( xResult == MQTTSuccess );
//
//    for( ulTopicCount = 0; ulTopicCount < ulSize; ulTopicCount++ )
//    {
//        xTopicFilterContext[ ulTopicCount ].xSubAckStatus = pucPayload[ ulTopicCount ];
//    }
//}
///*-----------------------------------------------------------*/
//
//static BaseType_t prvMQTTSubscribeWithBackoffRetries( MQTTContext_t * pxMQTTContext )
//{
//    MQTTStatus_t xResult = MQTTSuccess;
//    BackoffAlgorithmContext_t xRetryParams;
//    BaseType_t xBackoffStatus = pdFAIL;
//    MQTTSubscribeInfo_t xMQTTSubscription[ mqttexampleTOPIC_COUNT ];
//    BaseType_t xFailedSubscribeToTopic = pdFALSE;
//    uint32_t ulTopicCount = 0U;
//    BaseType_t xStatus = pdFAIL;
//
//    /* Some fields not used by this demo so start with everything at 0. */
//    ( void ) memset( ( void * ) &xMQTTSubscription, 0x00, sizeof( xMQTTSubscription ) );
//
//    /* Get a unique packet id. */
//    usSubscribePacketIdentifier = MQTT_GetPacketId( pxMQTTContext );
//
//    /* Subscribe to the mqttexampleTOPIC topic filter. This example subscribes to
//     * only one topic and uses QoS1. */
//    xMQTTSubscription[ 0 ].qos = MQTTQoS1;
//    xMQTTSubscription[ 0 ].pTopicFilter = mqttexampleTOPIC;
//    xMQTTSubscription[ 0 ].topicFilterLength = ( uint16_t ) strlen( mqttexampleTOPIC );
//
//    /* Initialize retry attempts and interval. */
//    BackoffAlgorithm_InitializeParams( &xRetryParams,
//                                       RETRY_BACKOFF_BASE_MS,
//                                       RETRY_MAX_BACKOFF_DELAY_MS,
//                                       RETRY_MAX_ATTEMPTS );
//
//    do
//    {
//        /* The client is now connected to the broker. Subscribe to the topic
//         * as specified in mqttexampleTOPIC at the top of this file by sending a
//         * subscribe packet then waiting for a subscribe acknowledgment (SUBACK).
//         * This client will then publish to the same topic it subscribed to, so it
//         * will expect all the messages it sends to the broker to be sent back to it
//         * from the broker. This demo uses QOS0 in Subscribe, therefore, the Publish
//         * messages received from the broker will have QOS0. */
//        LogInfo( ( "Attempt to subscribe to the MQTT topic %s.", mqttexampleTOPIC ) );
//        xResult = MQTT_Subscribe( pxMQTTContext,
//                                  xMQTTSubscription,
//                                  sizeof( xMQTTSubscription ) / sizeof( MQTTSubscribeInfo_t ),
//                                  usSubscribePacketIdentifier );
//
//        if( xResult != MQTTSuccess )
//        {
//            LogError( ( "Failed to SUBSCRIBE to MQTT topic %s. Error=%s",
//                        mqttexampleTOPIC, MQTT_Status_strerror( xResult ) ) );
//        }
//        else
//        {
//            xStatus = pdPASS;
//            LogInfo( ( "SUBSCRIBE sent for topic %s to broker.", mqttexampleTOPIC ) );
//
//            /* Process incoming packet from the broker. After sending the subscribe, the
//             * client may receive a publish before it receives a subscribe ack. Therefore,
//             * call generic incoming packet processing function. Since this demo is
//             * subscribing to the topic to which no one is publishing, probability of
//             * receiving Publish message before subscribe ack is zero; but application
//             * must be ready to receive any packet.  This demo uses the generic packet
//             * processing function everywhere to highlight this fact. */
//            xResult = prvWaitForPacket( pxMQTTContext, MQTT_PACKET_TYPE_SUBACK );
//
//            if( xResult != MQTTSuccess )
//            {
//                xStatus = pdFAIL;
//            }
//        }
//
//        if( xStatus == pdPASS )
//        {
//            /* Reset flag before checking suback responses. */
//            xFailedSubscribeToTopic = pdFALSE;
//
//            /* Check if recent subscription request has been rejected. #xTopicFilterContext is updated
//             * in the event callback to reflect the status of the SUBACK sent by the broker. It represents
//             * either the QoS level granted by the server upon subscription, or acknowledgement of
//             * server rejection of the subscription request. */
//            for( ulTopicCount = 0; ulTopicCount < mqttexampleTOPIC_COUNT; ulTopicCount++ )
//            {
//                if( xTopicFilterContext[ ulTopicCount ].xSubAckStatus == MQTTSubAckFailure )
//                {
//                    xFailedSubscribeToTopic = pdTRUE;
//
//                    /* As the subscribe attempt failed, we will retry the connection after an
//                     * exponential backoff with jitter delay. */
//
//                    /* Retry subscribe after exponential back-off. */
//                    LogWarn( ( "Server rejected subscription request. Attempting to re-subscribe to topic %s.",
//                               xTopicFilterContext[ ulTopicCount ].pcTopicFilter ) );
//
//                    xBackoffStatus = prvBackoffForRetry( &xRetryParams );
//                    break;
//                }
//            }
//        }
//    } while( ( xFailedSubscribeToTopic == pdTRUE ) && ( xBackoffStatus == pdPASS ) );
//
//    return xStatus;
//}
///*-----------------------------------------------------------*/
//
//static BaseType_t prvMQTTPublishToTopic( MQTTContext_t * pxMQTTContext )
//{
//    MQTTStatus_t xResult;
//    MQTTPublishInfo_t xMQTTPublishInfo;
//    BaseType_t xStatus = pdPASS;
//
//    /* Some fields are not used by this demo so start with everything at 0. */
//    ( void ) memset( ( void * ) &xMQTTPublishInfo, 0x00, sizeof( xMQTTPublishInfo ) );
//
//    /* This demo uses QoS1. */
//    xMQTTPublishInfo.qos = MQTTQoS1;
//    xMQTTPublishInfo.retain = false;
//    xMQTTPublishInfo.pTopicName = mqttexampleTOPIC;
//    xMQTTPublishInfo.topicNameLength = ( uint16_t ) strlen( mqttexampleTOPIC );
//    xMQTTPublishInfo.pPayload = mqttexampleMESSAGE;
//    xMQTTPublishInfo.payloadLength = strlen( mqttexampleMESSAGE );
//
//    /* Get a unique packet id. */
//    usPublishPacketIdentifier = MQTT_GetPacketId( pxMQTTContext );
//
//    /* Send PUBLISH packet. Packet ID is not used for a QoS1 publish. */
//    xResult = MQTT_Publish( pxMQTTContext, &xMQTTPublishInfo, usPublishPacketIdentifier );
//
//    if( xResult != MQTTSuccess )
//    {
//        xStatus = pdFAIL;
//        LogError( ( "Failed to send PUBLISH message to broker: Topic=%s, Error=%s",
//                    mqttexampleTOPIC,
//                    MQTT_Status_strerror( xResult ) ) );
//    }
//
//    return xStatus;
//}
///*-----------------------------------------------------------*/
//
//static BaseType_t prvMQTTUnsubscribeFromTopic( MQTTContext_t * pxMQTTContext )
//{
//    MQTTStatus_t xResult;
//    MQTTSubscribeInfo_t xMQTTSubscription[ mqttexampleTOPIC_COUNT ];
//    BaseType_t xStatus = pdPASS;
//
//    /* Some fields not used by this demo so start with everything at 0. */
//    ( void ) memset( ( void * ) &xMQTTSubscription, 0x00, sizeof( xMQTTSubscription ) );
//
//    /* Get a unique packet id. */
//    usSubscribePacketIdentifier = MQTT_GetPacketId( pxMQTTContext );
//
//    /* Subscribe to the mqttexampleTOPIC topic filter. This example subscribes to
//     * only one topic and uses QoS1. */
//    xMQTTSubscription[ 0 ].qos = MQTTQoS1;
//    xMQTTSubscription[ 0 ].pTopicFilter = mqttexampleTOPIC;
//    xMQTTSubscription[ 0 ].topicFilterLength = ( uint16_t ) strlen( mqttexampleTOPIC );
//
//    /* Get next unique packet identifier. */
//    usUnsubscribePacketIdentifier = MQTT_GetPacketId( pxMQTTContext );
//
//    /* Send UNSUBSCRIBE packet. */
//    xResult = MQTT_Unsubscribe( pxMQTTContext,
//                                xMQTTSubscription,
//                                sizeof( xMQTTSubscription ) / sizeof( MQTTSubscribeInfo_t ),
//                                usUnsubscribePacketIdentifier );
//
//    if( xResult != MQTTSuccess )
//    {
//        xStatus = pdFAIL;
//        LogError( ( "Failed to send UNSUBSCRIBE request to broker: TopicFilter=%s, Error=%s",
//                    mqttexampleTOPIC,
//                    MQTT_Status_strerror( xResult ) ) );
//    }
//
//    return xStatus;
//}
///*-----------------------------------------------------------*/
//
//static void prvMQTTProcessResponse( MQTTPacketInfo_t * pxIncomingPacket,
//                                    uint16_t usPacketId )
//{
//    uint32_t ulTopicCount = 0U;
//
//    switch( pxIncomingPacket->type )
//    {
//        case MQTT_PACKET_TYPE_PUBACK:
//            LogInfo( ( "PUBACK received for packet Id %u.", usPacketId ) );
//            /* Make sure ACK packet identifier matches with Request packet identifier. */
//            configASSERT( usPublishPacketIdentifier == usPacketId );
//            break;
//
//        case MQTT_PACKET_TYPE_SUBACK:
//
//            /* Update the packet type received to SUBACK. */
//            usPacketTypeReceived = MQTT_PACKET_TYPE_SUBACK;
//
//            /* A SUBACK from the broker, containing the server response to our subscription request, has been received.
//             * It contains the status code indicating server approval/rejection for the subscription to the single topic
//             * requested. The SUBACK will be parsed to obtain the status code, and this status code will be stored in global
//             * variable #xTopicFilterContext. */
//            prvUpdateSubAckStatus( pxIncomingPacket );
//
//            for( ulTopicCount = 0; ulTopicCount < mqttexampleTOPIC_COUNT; ulTopicCount++ )
//            {
//                if( xTopicFilterContext[ ulTopicCount ].xSubAckStatus != MQTTSubAckFailure )
//                {
//                    LogInfo( ( "Subscribed to the topic %s with maximum QoS %u.",
//                               xTopicFilterContext[ ulTopicCount ].pcTopicFilter,
//                               xTopicFilterContext[ ulTopicCount ].xSubAckStatus ) );
//                }
//            }
//
//            /* Make sure ACK packet identifier matches with Request packet identifier. */
//            configASSERT( usSubscribePacketIdentifier == usPacketId );
//            break;
//
//        case MQTT_PACKET_TYPE_UNSUBACK:
//            LogInfo( ( "Unsubscribed from the topic %s.", mqttexampleTOPIC ) );
//
//            /* Update the packet type received to UNSUBACK. */
//            usPacketTypeReceived = MQTT_PACKET_TYPE_UNSUBACK;
//
//            /* Make sure ACK packet identifier matches with Request packet identifier. */
//            configASSERT( usUnsubscribePacketIdentifier == usPacketId );
//            break;
//
//        case MQTT_PACKET_TYPE_PINGRESP:
//            LogInfo( ( "Ping Response successfully received." ) );
//
//            break;
//
//        /* Any other packet type is invalid. */
//        default:
//            LogWarn( ( "prvMQTTProcessResponse() called with unknown packet type:(%02X).",
//                       pxIncomingPacket->type ) );
//    }
//}
//
///*-----------------------------------------------------------*/
//
//static void prvMQTTProcessIncomingPublish( MQTTPublishInfo_t * pxPublishInfo )
//{
//    configASSERT( pxPublishInfo != NULL );
//
//    /* Set the global for indicating that an incoming publish is received. */
//    usPacketTypeReceived = MQTT_PACKET_TYPE_PUBLISH;
//
//    /* Process incoming Publish. */
//    LogInfo( ( "Incoming QoS : %d\n", pxPublishInfo->qos ) );
//
//    /* Verify the received publish is for the we have subscribed to. */
//    if( ( pxPublishInfo->topicNameLength == strlen( mqttexampleTOPIC ) ) &&
//        ( 0 == strncmp( mqttexampleTOPIC, pxPublishInfo->pTopicName, pxPublishInfo->topicNameLength ) ) )
//    {
//        LogInfo( ( "Incoming Publish Topic Name: %.*s matches subscribed topic."
//                   "Incoming Publish Message : %.*s",
//                   pxPublishInfo->topicNameLength,
//                   pxPublishInfo->pTopicName,
//                   pxPublishInfo->payloadLength,
//                   pxPublishInfo->pPayload ) );
//    }
//    else
//    {
//        LogInfo( ( "Incoming Publish Topic Name: %.*s does not match subscribed topic.",
//                   pxPublishInfo->topicNameLength,
//                   pxPublishInfo->pTopicName ) );
//    }
//}
//
///*-----------------------------------------------------------*/
//
//static void prvEventCallback( MQTTContext_t * pxMQTTContext,
//                              MQTTPacketInfo_t * pxPacketInfo,
//                              MQTTDeserializedInfo_t * pxDeserializedInfo )
//{
//    /* The MQTT context is not used for this demo. */
//    ( void ) pxMQTTContext;
//
//    if( ( pxPacketInfo->type & 0xF0U ) == MQTT_PACKET_TYPE_PUBLISH )
//    {
//        prvMQTTProcessIncomingPublish( pxDeserializedInfo->pPublishInfo );
//    }
//    else
//    {
//        prvMQTTProcessResponse( pxPacketInfo, pxDeserializedInfo->packetIdentifier );
//    }
//}
//
///*-----------------------------------------------------------*/
//
//static uint32_t prvGetTimeMs( void )
//{
//    TickType_t xTickCount = 0;
//    uint32_t ulTimeMs = 0UL;
//
//    /* Get the current tick count. */
//    xTickCount = xTaskGetTickCount();
//
//    /* Convert the ticks to milliseconds. */
//    ulTimeMs = ( uint32_t ) xTickCount * MILLISECONDS_PER_TICK;
//
//    /* Reduce ulGlobalEntryTimeMs from obtained time so as to always return the
//     * elapsed time in the application. */
//    ulTimeMs = ( uint32_t ) ( ulTimeMs - ulGlobalEntryTimeMs );
//
//    return ulTimeMs;
//}
//
///*-----------------------------------------------------------*/
//
//static MQTTStatus_t prvWaitForPacket( MQTTContext_t * pxMQTTContext,
//                                      uint16_t usPacketType )
//{
//    uint8_t ucCount = 0U;
//    MQTTStatus_t xMQTTStatus = MQTTSuccess;
//
//    /* Reset the packet type received. */
//    usPacketTypeReceived = 0U;
//
//    while( ( usPacketTypeReceived != usPacketType ) &&
//           ( ucCount++ < MQTT_PROCESS_LOOP_PACKET_WAIT_COUNT_MAX ) &&
//           ( xMQTTStatus == MQTTSuccess ) )
//    {
//        /* Event callback will set #usPacketTypeReceived when receiving appropriate packet. This
//         * will wait for at most mqttexamplePROCESS_LOOP_TIMEOUT_MS. */
//        xMQTTStatus = MQTT_ProcessLoop( pxMQTTContext, mqttexamplePROCESS_LOOP_TIMEOUT_MS );
//    }
//
//    if( ( xMQTTStatus != MQTTSuccess ) || ( usPacketTypeReceived != usPacketType ) )
//    {
//        LogError( ( "MQTT_ProcessLoop failed to receive packet: Packet type=%02X, LoopDuration=%u, Status=%s",
//                    usPacketType,
//                    ( mqttexamplePROCESS_LOOP_TIMEOUT_MS * ucCount ),
//                    MQTT_Status_strerror( xMQTTStatus ) ) );
//    }
//
//    return xMQTTStatus;
//}
//
///*-----------------------------------------------------------*/
///* FreeRTOS OTA agent includes. */
//#include "aws_iot_ota_agent.h"
//
//#include "aws_application_version.h"
//
//#define OTA_DEMO_TASK_DELAY_SECONDS                  ( 1UL )
//
//volatile static bool _networkConnected = false;
//
//MQTTContext_t xMQTTContext = { 0 };
//
//static const char * _pStateStr[ eOTA_AgentState_All ] =
//{
//    "Init",
//    "Ready",
//    "RequestingJob",
//    "WaitingForJob",
//    "CreatingFile",
//    "RequestingFileBlock",
//    "WaitingForFileBlock",
//    "ClosingFile",
//    "Suspended",
//    "ShuttingDown",
//    "Stopped"
//};
//
//static void App_OTACompleteCallback( OTA_JobEvent_t eEvent )
//{
//    OTA_Err_t xErr = kOTA_Err_Uninitialized;
//
//    DEFINE_OTA_METHOD_NAME( "App_OTACompleteCallback" );
//
//    /* OTA job is completed. so delete the MQTT and network connection. */
//    if( eEvent == eOTA_JobEvent_Activate )
//    {
//        LogInfo(( "Received eOTA_JobEvent_Activate callback from OTA Agent.\r\n") );
//
//        /* OTA job is completed. so delete the network connection. */
//        if( &xMQTTContext != NULL )
//        {
//            LogInfo( ( "Disconnecting the MQTT connection with %s.", clientcredentialMQTT_BROKER_ENDPOINT ) );
//            MQTT_Disconnect( &xMQTTContext );
//        }
//
//        /* Activate the new firmware image. */
//        OTA_ActivateNewImage();
//
//        /* We should never get here as new image activation must reset the device.*/
//        LogError( ("New image activation failed.\r\n") );
//
//        for( ; ; )
//        {
//        }
//    }
//    else if( eEvent == eOTA_JobEvent_Fail )
//    {
//        LogInfo( ("Received eOTA_JobEvent_Fail callback from OTA Agent.\r\n") );
//
//        /* Nothing special to do. The OTA agent handles it. */
//    }
//    else if( eEvent == eOTA_JobEvent_StartTest )
//    {
//        /* This demo just accepts the image since it was a good OTA update and networking
//         * and services are all working (or we wouldn't have made it this far). If this
//         * were some custom device that wants to test other things before calling it OK,
//         * this would be the place to kick off those tests before calling OTA_SetImageState()
//         * with the final result of either accepted or rejected. */
//
//        LogInfo( ("Received eOTA_JobEvent_StartTest callback from OTA Agent.\r\n") );
//        xErr = OTA_SetImageState( eOTA_ImageState_Accepted );
//
//        if( xErr != kOTA_Err_None )
//        {
//            LogError( (" Error! Failed to set image state as accepted.\r\n") );
//        }
//    }
//}
//
//int OverTheAirUpdate()
//{
//    //uint32_t ulPublishCount = 0U, ulTopicCount = 0U;
//    //const uint32_t ulMaxPublishCount = 5UL;
//    NetworkContext_t xNetworkContext = { 0 };
//    MQTTStatus_t xMQTTStatus;
//    //uint32_t ulDemoRunCount = 0UL, ulDemoSuccessCount = 0UL;
//    TransportSocketStatus_t xNetworkStatus;
//    //BaseType_t xIsConnectionEstablished = pdFALSE;
//    SecureSocketsTransportParams_t secureSocketsTransportParams = { 0 };
//
//    /* Upon return, pdPASS will indicate a successful demo execution.
//    * pdFAIL will indicate some failures occurred during execution. The
//    * user of this demo must check the logs for any failure codes. */
//    BaseType_t xDemoStatus = pdFAIL;
//
//    /* Set the entry time of the demo application. This entry time will be used
//     * to calculate relative time elapsed in the execution of the demo application,
//     * by the timer utility function that is provided to the MQTT library.
//     */
//    ulGlobalEntryTimeMs = prvGetTimeMs();
//    xNetworkContext.pParams = &secureSocketsTransportParams;
//
//    LogInfo( ("OTA demo version %u.%u.%u\r\n",
//                    xAppFirmwareVersion.u.x.ucMajor,
//                    xAppFirmwareVersion.u.x.ucMinor,
//                    xAppFirmwareVersion.u.x.usBuild ));
//
//
//    /****************************** Connect. ******************************/
//
//    /* Attempt to establish TLS session with MQTT broker. If connection fails,
//     * retry after a timeout. Timeout value will be exponentially increased until
//     * the maximum number of attempts are reached or the maximum timeout value is reached.
//     * The function returns a failure status if the TLS over TCP connection cannot be established
//     * to the broker after the configured number of attempts. */
//    xDemoStatus = prvConnectToServerWithBackoffRetries( &xNetworkContext );
//
//    if( xDemoStatus == pdPASS )
//    {
//        /* Set a flag indicating a TLS connection exists. This is done to
//         * disconnect if the loop exits before disconnection happens. */
//        //xIsConnectionEstablished = pdTRUE;
//
//        /* Sends an MQTT Connect packet over the already established TLS connection,
//         * and waits for connection acknowledgment (CONNACK) packet. */
//        LogInfo( ( "Creating an MQTT connection to %s.", clientcredentialMQTT_BROKER_ENDPOINT ) );
//        xDemoStatus = prvCreateMQTTConnectionWithBroker( &xMQTTContext, &xNetworkContext );
//
//        if( xDemoStatus == pdPASS )
//        {
//            /**************************** OTA. ******************************/
//
//            OTA_State_t eState;
//            static OTA_ConnectionContext_t xOTAConnectionCtx;
//            while(true)
//            {
//                xOTAConnectionCtx.pxNetworkInterface = NULL;
//                xOTAConnectionCtx.pvNetworkCredentials = &xNetworkContext;//pNetworkCredentialInfo;
//                xOTAConnectionCtx.pvControlClient = &xMQTTContext;//_mqttConnection;
//
//                /* Check if OTA Agent is suspended and resume.*/
//                if( ( eState = OTA_GetAgentState() ) == eOTA_AgentState_Suspended )
//                {
//                    OTA_Resume( &xOTAConnectionCtx );
//                }
//
//                /* Initialize the OTA Agent , if it is resuming the OTA statistics will be cleared for new connection.*/
//                OTA_AgentInit( ( void * ) ( &xOTAConnectionCtx ),
//                               ( const uint8_t * ) ( clientcredentialIOT_THING_NAME ),
//                               App_OTACompleteCallback,
//                               ( TickType_t ) ~0 );
//
//                while( ( ( eState = OTA_GetAgentState() ) != eOTA_AgentState_Stopped ))//for checking if network is connected && _networkConnected )
//                {
//                    LogInfo( ("State: %s  Received: %u   Queued: %u   Processed: %u   Dropped: %u\r\n", _pStateStr[ eState ],
//                                OTA_GetPacketsReceived(), OTA_GetPacketsQueued(), OTA_GetPacketsProcessed(), OTA_GetPacketsDropped()) );
//
//                    /* Wait forever for OTA traffic but allow other tasks to run and output statistics only once per second. */
//                    vTaskDelay( pdMS_TO_TICKS(500) );
//
//
//                }
//
//                //OTA_AgentShutdown
//            }
//
//            /**************************** Disconnect. ******************************/
//
//            /* Send an MQTT Disconnect packet over the already connected TLS over TCP connection.
//             * There is no corresponding response for the disconnect packet. After sending
//             * disconnect, client must close the network connection. */
//            LogInfo( ( "Disconnecting the MQTT connection with %s.", clientcredentialMQTT_BROKER_ENDPOINT ) );
//            xMQTTStatus = MQTT_Disconnect( &xMQTTContext );
//        }
//
//        /* We will always close the network connection, even if an error may have occurred during
//         * demo execution, to clean up the system resources that it may have consumed. */
//
//        /* Close the network connection.  */
//        xNetworkStatus = SecureSocketsTransport_Disconnect( &xNetworkContext );
//
//        if( xNetworkStatus != TRANSPORT_SOCKET_STATUS_SUCCESS )
//        {
//            xDemoStatus = pdFAIL;
//            LogError( ( "SecureSocketsTransport_Disconnect() failed to close the network connection. "
//                        "StatusCode=%d.", ( int ) xNetworkStatus ) );
//        }
//
//    }
//}
