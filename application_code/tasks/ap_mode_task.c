#include <application_code/tasks/include/ap_mode_task.h>

/* standard includes */
#include <stdlib.h>
#include <string.h>

/* driverlib Header files */
#include <ti/devices/cc32xx/inc/hw_types.h>
#include <ti/devices/cc32xx/inc/hw_memmap.h>
#include <ti/devices/cc32xx/driverlib/prcm.h>

/* TI-DRIVERS Header files */
//#include <ti_drivers_config.h>
//#include <uart_term.h>
#include <ti/drivers/GPIO.h>
#include <ti/drivers/net/wifi/simplelink.h>
#include <ti/drivers/net/wifi/wlan.h>
#include <ti/drivers/power/PowerCC32XX.h>

//freeRTOS headers
#include "FreeRTOS.h"
#include "task.h"
#include "event_groups.h"

#include "iot_wifi.h"

#define SIMPLELINK_INIT   ( 1 << 0 )
#define SIMPLELINK_ERROR   ( 1 << 1 )
#define DEFAULT_ROLE ROLE_AP

#define PROVISIONING_INACTIVITY_TIMEOUT         (600)
#define SL_STOP_TIMEOUT         (200)

#define ASSERT_ON_ERROR(error_code) \
    { \
        if(error_code < 0) \
        { \
            ERR_PRINT(error_code); \
            return error_code; \
        } \
    }

typedef enum
{
    PrvsnMode_AP,       /* AP provisioning (AP role) */
    PrvsnMode_SC,       /* Smart Config provisioning (STA role) */
    PrvsnMode_APSC      /* AP + Smart Config provisioning (AP role) */
}PrvsnMode;

EventGroupHandle_t xSimpleLinkEventGroup;

uint8_t desiredRole = DEFAULT_ROLE;

void provisioningInit(void)
{

}

int32_t provisioningStart(void)
{
    int32_t retVal = 0;
    SlDeviceVersion_t ver = {0};
    uint8_t configOpt = 0;
    uint16_t configLen = 0;

    /* check if provisioning is running */
    /* if auto provisioning - the command stops it automatically */
    /* in case host triggered provisioning - need to stop it explicitly */
    configOpt = SL_DEVICE_GENERAL_VERSION;
    configLen = sizeof(ver);
    retVal = sl_DeviceGet(SL_DEVICE_GENERAL, &configOpt, &configLen,
                     (uint8_t *)(&ver));
    if(SL_RET_CODE_PROVISIONING_IN_PROGRESS == retVal)
    {
        LogInfo((
            "[Provisioning task] Provisioning is already running,"
            " stopping it...\r\n"));
        retVal =
            sl_WlanProvisioning(SL_WLAN_PROVISIONING_CMD_STOP,ROLE_STA,0,NULL,
                                0);

        /* return  SL_RET_CODE_PROVISIONING_IN_PROGRESS to indicate the SM
            to stay in the same state*/
        return(SL_RET_CODE_PROVISIONING_IN_PROGRESS);
    }

    /*
        IMPORTANT NOTE - This is an example reset function, user must update
                         this function to match the application settings.
     */
    retVal = ConfigureSimpleLinkToDefaultState();

    if(retVal < 0)
    {
        LogInfo((
            "[Provisioning task] Failed to configure the device in its default "
            "state \n\r"));
        return(retVal);
    }

    LogInfo((
        "[Provisioning task] Device is configured \
    in default state \n\r"                                                     ));

    retVal = InitSimplelink(ROLE_AP);
    if(retVal < 0)
    {
        LogInfo(("[Provisioning task] Failed to initialize the device\n\r"));
        return(retVal);
    }

    return(retVal);
}

static int32_t ConfigureSimpleLinkToDefaultState(void)
{
    SlWlanRxFilterOperationCommandBuff_t RxFilterIdMask;

    uint8_t ucConfigOpt = 0;
    uint16_t ifBitmap = 0;
    uint8_t ucPower = 0;

    int32_t ret = -1;
    int32_t mode = -1;

    memset(&RxFilterIdMask,0,sizeof(SlWlanRxFilterOperationCommandBuff_t));

    /* Start Simplelink - Blocking mode */
    mode = sl_Start(0, 0, 0);
    if(SL_RET_CODE_DEV_ALREADY_STARTED != mode)
    {
        ASSERT_ON_ERROR(mode);
    }

    /* If the device is not in AP mode, try configuring it in AP mode
       in case device is already started
       (got SL_RET_CODE_DEV_ALREADY_STARTED error code), then mode would remain
       -1 and in this case we do not know the role. Move to AP role anyway  */
    if(ROLE_AP != mode)
    {
        /* Switch to AP role and restart */
        ret = sl_WlanSetMode(ROLE_AP);
        ASSERT_ON_ERROR(ret);

        ret = sl_Stop(SL_STOP_TIMEOUT);
        ASSERT_ON_ERROR(ret);

        ret = sl_Start(0, 0, 0);
        ASSERT_ON_ERROR(ret);

        /* Check if the device is in AP again */
        if(ROLE_AP != ret)
        {
            return(ret);
        }
    }

    /* Set connection policy to Auto (no AutoProvisioning)  */
    ret = sl_WlanPolicySet(SL_WLAN_POLICY_CONNECTION,
                           SL_WLAN_CONNECTION_POLICY(1, 0, 0, 0), NULL, 0);
    ASSERT_ON_ERROR(ret);

    /* Remove all profiles */
    //ret = sl_WlanProfileDel(0xFF);
    //ASSERT_ON_ERROR(ret);

    /* Enable DHCP client */
    ret = sl_NetCfgSet(SL_NETCFG_IPV4_STA_ADDR_MODE,SL_NETCFG_ADDR_DHCP,0,0);
    ASSERT_ON_ERROR(ret);

    /* Disable IPV6 */
    ifBitmap = 0;
    ret =
        sl_NetCfgSet(SL_NETCFG_IF, SL_NETCFG_IF_STATE, sizeof(ifBitmap),
                     (uint8_t *)&ifBitmap);
    ASSERT_ON_ERROR(ret);

    /* Disable scan */
    ucConfigOpt = SL_WLAN_SCAN_POLICY(0, 0);
    ret = sl_WlanPolicySet(SL_WLAN_POLICY_SCAN, ucConfigOpt, NULL, 0);
    ASSERT_ON_ERROR(ret);

    /* Set Tx power level for station mode
       Number between 0-15, as dB offset from max power - 0 will
       set max power */
    ucPower = 0;
    ret = sl_WlanSet(SL_WLAN_CFG_GENERAL_PARAM_ID,
                     SL_WLAN_GENERAL_PARAM_OPT_STA_TX_POWER, 1,
                     (uint8_t *)&ucPower);
    ASSERT_ON_ERROR(ret);

    /* Set PM policy to normal */
    ret = sl_WlanPolicySet(SL_WLAN_POLICY_PM, SL_WLAN_NORMAL_POLICY, NULL, 0);
    ASSERT_ON_ERROR(ret);

    /* Unregister mDNS services */
    ret = sl_NetAppMDNSUnRegisterService(0, 0, 0);
    ASSERT_ON_ERROR(ret);

    /* Remove  all 64 filters (8*8) */
    memset(RxFilterIdMask.FilterBitmap, 0xFF, 8);
    ret = sl_WlanSet(SL_WLAN_RX_FILTERS_ID,
                     SL_WLAN_RX_FILTER_REMOVE,
                     sizeof(SlWlanRxFilterOperationCommandBuff_t),
                     (uint8_t *)&RxFilterIdMask);
    ASSERT_ON_ERROR(ret);

    ret = sl_Stop(SL_STOP_TIMEOUT);
    ASSERT_ON_ERROR(ret);

    return(ret);
}

void SimpleLinkInitCallback(uint32_t status,
                            SlDeviceInitInfo_t *DeviceInitInfo)
{
    LogInfo(("[Provisioning task] Device started in %s role\n\r",
               (0 == status) ? "Station" : \
               ((2 == status) ? "AP" : "P2P")));

    if(desiredRole == status)
    {
        xEventGroupSetBits(xSimpleLinkEventGroup, SIMPLELINK_INIT);
    }
    else
    {
        LogInfo(("[Provisioning task] But the intended role is %s \n\r", \
                   (0 == desiredRole) ? "Station" : \
                   ((2 == desiredRole) ? "AP" : "P2P")));
       xEventGroupSetBits(xSimpleLinkEventGroup, SIMPLELINK_ERROR);
    }
}

static int32_t InitSimplelink(uint8_t const role)
{
    int32_t retVal = -1;

    desiredRole = role;

    retVal = sl_Start(0, 0, (P_INIT_CALLBACK)SimpleLinkInitCallback);
    ASSERT_ON_ERROR(retVal);

    /* Start timer *///for timeout if simple link init does not get called
    //pCtx->asyncEvtTimeout = ASYNC_EVT_TIMEOUT;
    //retVal = StartAsyncEvtTimer(pCtx->asyncEvtTimeout);
    //ASSERT_ON_ERROR(retVal);

    return(retVal);
}

static int32_t HandleStrtdEvt(void)
{
    int32_t retVal = 0;

    /*SlDeviceVersion_t firmwareVersion = {0};

    uint8_t ucConfigOpt = 0;
    uint16_t ucConfigLen = 0;


    ucConfigOpt = SL_DEVICE_GENERAL_VERSION;
    ucConfigLen = sizeof(firmwareVersion);
    retVal = sl_DeviceGet(SL_DEVICE_GENERAL, &ucConfigOpt, \
                          &ucConfigLen,
                          (unsigned char *)(&firmwareVersion));
    ASSERT_ON_ERROR(retVal);

    UART_PRINT("[Provisioning task] Host Driver Version: %s\n\r",
               SL_DRIVER_VERSION);
    UART_PRINT(
        "[Provisioning task] Build Version "
        "%d.%d.%d.%d.31.%d.%d.%d.%d.%d.%d.%d.%d\n\r", \
        firmwareVersion.NwpVersion[0],  \
        firmwareVersion.NwpVersion[1],  \
        firmwareVersion.NwpVersion[2],  \
        firmwareVersion.NwpVersion[3],  \
        firmwareVersion.FwVersion[0],   \
        firmwareVersion.FwVersion[1],   \
        firmwareVersion.FwVersion[2],   \
        firmwareVersion.FwVersion[3],   \
        firmwareVersion.PhyVersion[0],  \
        firmwareVersion.PhyVersion[1],  \
        firmwareVersion.PhyVersion[2],  \
        firmwareVersion.PhyVersion[3]);

    Start provisioning process
    UART_PRINT("[Provisioning task] Starting Provisioning - ");
    UART_PRINT(
        "[Provisioning task] in mode %d (0 = AP, 1 = SC, 2 = AP+SC)\r\n",
        pCtx->provisioningMode);*/

    retVal = sl_WlanProvisioning(PrvsnMode_APSC, ROLE_STA,
                                 PROVISIONING_INACTIVITY_TIMEOUT, NULL,0);

    LogInfo(("[Provisioning task] Provisioning Started. Waiting to "
        "be provisioned..!! \r\n"));


    return(retVal);
}

void AP_Task(void * pvParameters)
{
    WIFIReturnCode_t xWifiStatus;

    xWifiStatus = WIFI_On(); // Turn on Wi-Fi module

    // Check that Wi-Fi initialization was successful
    if( xWifiStatus == eWiFiSuccess )
    {
        LogInfo( ( "WiFi library initialized.\n") );
    }
    else
    {
        LogInfo( ( "WiFi library failed to initialize.\n" ) );
        // Handle module init failure
    }

    xSimpleLinkEventGroup = xEventGroupCreate();
    EventBits_t uxBits;

    /* Was the event group created successfully? */
    if( xSimpleLinkEventGroup == NULL )
    {
        /* The event group was not created because there was insufficient
        FreeRTOS heap available. */
    }
    else
    {
        /* The event group was created. */
    }

    provisioningInit();
    provisioningStart();

    while(true)
    {
        uxBits = xEventGroupWaitBits(
                  xSimpleLinkEventGroup,   /* The event group being tested. */
                  SIMPLELINK_INIT | SIMPLELINK_ERROR, /* The bits within the event group to wait for. */
                  pdTRUE,        /*the bits should be cleared before returning. */
                  pdFALSE,       /* Don't wait for both bits, either bit will do. */
                  portMAX_DELAY  );/* Wait a maximum time for either bit to be set. */
        if( ( uxBits & SIMPLELINK_ERROR ) != 0 )
        {
          ///error event
        }
        if( ( uxBits & SIMPLELINK_INIT ) != 0 )
        {
          HandleStrtdEvt();
        }
    }

    //TO DO: implement provisioning using iot_wifi.c
    /*WIFINetworkParams_t xNetworkParams;
    WIFIReturnCode_t xWifiStatus;

    xWifiStatus = WIFI_On(); // Turn on Wi-Fi module

    // Check that Wi-Fi initialization was successful
    if( xWifiStatus == eWiFiSuccess )
    {
        configPRINT( ( "WiFi library initialized.\n") );
    }
    else
    {
        configPRINT( ( "WiFi library failed to initialize.\n" ) );
        // Handle module init failure
    }

    xNetworkParams.pcSSID = "SSID_Name";
    xNetworkParams.pcPassword = "PASSWORD";
    xNetworkParams.xSecurity = eWiFiSecurityWPA2;
    xNetworkParams.cChannel = ChannelNum;
    WIFI_ConfigureAP( &xNetworkParams );

    xWifiStatus = WIFI_StartAP(); // Turn on Wi-Fi module

    // Check that Wi-Fi initialization was successful
    if( xWifiStatus == eWiFiSuccess )
    {
        configPRINT( ( "WiFi AP initialized.\n") );
    }
    else
    {
        configPRINT( ( "WiFi AP failed to initialize.\n" ) );
        // Handle module init failure
    }

    while(true)
    {

    }*/
}
