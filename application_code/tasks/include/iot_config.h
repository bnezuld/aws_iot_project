/*
 * iot_config.h
 *
 *  Created on: Feb 7, 2021
 *      Author: Brandon
 */

#ifndef APPLICATION_CODE_TASKS_INCLUDE_IOT_CONFIG_H_
#define APPLICATION_CODE_TASKS_INCLUDE_IOT_CONFIG_H_

/*
 * @brief MQTT Broker endpoint.
 *
 * @todo Set this to the fully-qualified DNS name of your MQTT broker.
 */
#define clientcredentialMQTT_BROKER_ENDPOINT         "a1f2962jrydfdr-ats.iot.us-east-2.amazonaws.com"

/*
 * @brief Host name.
 *
 * @todo Set this to the unique name of your IoT Thing.
 * Please note that for convenience of demonstration only we
 * are using a #define here. In production scenarios the thing
 * name can be something unique to the device that can be read
 * by software, such as a production serial number, rather
 * than a hard coded constant.
 */
#define clientcredentialIOT_THING_NAME               "MyIotThing"

/*
 * @brief Port number the MQTT broker is using.
 */
#define clientcredentialMQTT_BROKER_PORT             8883

/*
 * @brief Port number the Green Grass Discovery use for JSON retrieval from cloud is using.
 */
#define clientcredentialGREENGRASS_DISCOVERY_PORT    8443

/*
 * @brief Wi-Fi network to join.
 *
 * @todo If you are using Wi-Fi, set this to your network name.
 */
#define clientcredentialWIFI_SSID                    "nezuldr"

/*
 * @brief Password needed to join Wi-Fi network.
 * @todo If you are using WPA, set this to your network password.
 */
#define clientcredentialWIFI_PASSWORD                "Flam1ngo513"

/*
 * @brief Wi-Fi network security type.
 *
 * @see WIFISecurity_t.
 *
 * @note Possible values are eWiFiSecurityOpen, eWiFiSecurityWEP, eWiFiSecurityWPA,
 * eWiFiSecurityWPA2 (depending on the support of your device Wi-Fi radio).
 */
#define clientcredentialWIFI_SECURITY                eWiFiSecurityWPA2

#include <stdint.h>

/*
 * PEM-encoded client certificate.
 *
 * Must include the PEM header and footer:
 * "-----BEGIN CERTIFICATE-----\n"\
 * "...base64 data...\n"\
 * "-----END CERTIFICATE-----"
 */
#define keyCLIENT_CERTIFICATE_PEM \
"-----BEGIN CERTIFICATE-----\n"\
"MIIDWjCCAkKgAwIBAgIVAMclJ0f9Qf8p19KWqGa7gbT508MVMA0GCSqGSIb3DQEB\n"\
"CwUAME0xSzBJBgNVBAsMQkFtYXpvbiBXZWIgU2VydmljZXMgTz1BbWF6b24uY29t\n"\
"IEluYy4gTD1TZWF0dGxlIFNUPVdhc2hpbmd0b24gQz1VUzAeFw0yMTAxMDUxNTQ2\n"\
"MTRaFw00OTEyMzEyMzU5NTlaMB4xHDAaBgNVBAMME0FXUyBJb1QgQ2VydGlmaWNh\n"\
"dGUwggEiMA0GCSqGSIb3DQEBAQUAA4IBDwAwggEKAoIBAQDVfCJVJHkSXaxbIeaB\n"\
"oFKTg9RyLbRaBUeMPvTPpFDtesNe9t20ftXr7REdoIBFKkuRMOieaJZWFIlUFA7m\n"\
"693dAz5ErCIHLNZEcIpfxU0u5K1dRAR3buPNsoMIAdu20HD9PfDU6c4+Cd/ywU66\n"\
"hA2RWq6DW/wFsb8d84xIbkhVd3ftggiamjmCPWq+hXl1ItkOEQEGk7B8t3BMyfH4\n"\
"8zA7vyoalyQhT7c7wYoF8lMwmkZzIhM6I75ysiTU6SNu4BSLvPxX+wEv7J64XZqy\n"\
"KcJx5KIUniocrBszKvTUOSq0H4K/mIpk7GlXUhr8OBNv3Z5q1M6RRkEfm710KaWG\n"\
"dL8/AgMBAAGjYDBeMB8GA1UdIwQYMBaAFKvrp1n948Mb0w1VC3oHXGVBpc5oMB0G\n"\
"A1UdDgQWBBQLQNSYLHkq9KKt5nMkXz397BXa6zAMBgNVHRMBAf8EAjAAMA4GA1Ud\n"\
"DwEB/wQEAwIHgDANBgkqhkiG9w0BAQsFAAOCAQEATmcrApsON7gdn88C+0rjJoF6\n"\
"rVsPKZpe3XoWelFpBA0ISPTBNsRHgIYSui6wIBf1bJWHJE/xiQmkrXusMh08/nEN\n"\
"rhMCpcHaVH3Xrj0SVDOpn4v4MBDEbl84XlWUo6W0VXEjqpLdNcDZb0Epm3pkkrFo\n"\
"U1r6iZp+zXgR9MSirrhEty/B1KS3iut8ybGu7W1UKgX7WQrMtHNqfe8cEDBRc8IP\n"\
"qm+IYteWPo/seA9hMlzy0ZThumlgSckdh37fKSPGSO3gkMy59oX1b1gtlnErGoK5\n"\
"JslGnezQIIjyk/QeXPwiazqsjIENld30x8ufg5z89XsFNlo2NtMHDkEe7few6A==\n"\
"-----END CERTIFICATE-----"

/*
 * PEM-encoded client private key.
 *
 * Must include the PEM header and footer:
 * "-----BEGIN RSA PRIVATE KEY-----\n"\
 * "...base64 data...\n"\
 * "-----END RSA PRIVATE KEY-----"
 */
#define keyCLIENT_PRIVATE_KEY_PEM \
"-----BEGIN RSA PRIVATE KEY-----\n"\
"MIIEpAIBAAKCAQEA1XwiVSR5El2sWyHmgaBSk4PUci20WgVHjD70z6RQ7XrDXvbd\n"\
"tH7V6+0RHaCARSpLkTDonmiWVhSJVBQO5uvd3QM+RKwiByzWRHCKX8VNLuStXUQE\n"\
"d27jzbKDCAHbttBw/T3w1OnOPgnf8sFOuoQNkVqug1v8BbG/HfOMSG5IVXd37YII\n"\
"mpo5gj1qvoV5dSLZDhEBBpOwfLdwTMnx+PMwO78qGpckIU+3O8GKBfJTMJpGcyIT\n"\
"OiO+crIk1OkjbuAUi7z8V/sBL+yeuF2asinCceSiFJ4qHKwbMyr01DkqtB+Cv5iK\n"\
"ZOxpV1Ia/DgTb92eatTOkUZBH5u9dCmlhnS/PwIDAQABAoIBACi+lF7joyfaMPcD\n"\
"tVawHpKA6p8QEgfMUid2LIsktT1d3MPXIeE9A98PU+DvrQuGUv3W886n72lmaf9e\n"\
"BKoWAjYYVF4O7D+qUwqk4AP/SAfXJS9Tt/aDd37evxtcH274wVfT5o78QJyejdtr\n"\
"AXeflGdVg1EW0TbVAcDZpsB8K9oDpZCrkpq8rU4+hoBp/O4v4P1OvuBx+ddZRtHC\n"\
"XeWpue66eKgDycTNy3ea8e43iysUAxiRU4KHA6UXxxQaB+iq4VE+EDGNIvzszb28\n"\
"CH0PQmh8HihP68XdgfioqVyB6xuw3ObWvvv9gSBscg1cm5acnukxM1CYPBNw88qT\n"\
"EdNqMyECgYEA/xA4j3qOfGLeA1RaDNSQbvMie5VylcNImZh9MRrf5RsvST4YEkMv\n"\
"wONDA6ryVJbrD+IvLjpyvFmGRRYxkcgZSNyMU0gXcAKMvLWyo1azdVK/yEGSRIDW\n"\
"2LAWoEIPsvudV0N+/ycQtoyyW9pcSZTCwJDt6f9FRvNhBs7u620KzK0CgYEA1kTT\n"\
"hEs0bRP/HMXu5YrHUVtd8HWdVSaUnf1iILvcxyW2H4VhULrZThQ/yGYakroe6v2U\n"\
"FxrR3+AfGWHbQ1SwKNjflQMsFzvzhHc2A4h+lK3ejeyg5OrPH4KdvqW+Op1UOuHJ\n"\
"W7PRxQIuEE7Uz3BYZl6GcUOVeKoz2i+G7//J7RsCgYAWsZPGuEnLKWTFeRDWCWec\n"\
"Z4eO5VoflxNzjwy8fL6k/Mk1RBASN+YczFufDOuouRDLBf3aqBqjRXfb18CrTtlp\n"\
"ES5vDn//WEq7U5NEUyd+bdFgeO0RqUD0YJ5yvc31x8tVA01eWFR1WYlZANGrPlAh\n"\
"oAN7CVpZmLfuSiUZz0bhtQKBgQCCetUnebiKOCQhfHM3OySXaYEyHh1aLi1QbG2m\n"\
"K2CNsWxPk6SoSbBs+K3CtlK2STrstNDKpR1rLIsjpNCmFttTdIXqs0zVNT/cyc+N\n"\
"pUAYAC1H1fJAlLDeqmavIzVNcmNJnBdHjaBPTT2J1seHLw3WAPfz30kVeugqlMii\n"\
"O+zWQwKBgQCd+kjGlc9BGJ/aM+XqXnFpfUzk/Tt0kE6A1B42RUS70LeSFCDwqYwb\n"\
"8K+a12kMrVPl/fxurFHRLzDKGRlweYcTUfcZxkucY868kA6Z6ZNwLTizyu+HOlbB\n"\
"tRp9Gp5la1u9S8uPz6TkpdSdsb9XgxMV/QHBfDubZcmJ88iAVMhPhg==\n"\
"-----END RSA PRIVATE KEY-----"

/*
 * PEM-encoded Just-in-Time Registration (JITR) certificate (optional).
 *
 * If used, must include the PEM header and footer:
 * "-----BEGIN CERTIFICATE-----\n"\
 * "...base64 data...\n"\
 * "-----END CERTIFICATE-----"
 */
#define keyJITR_DEVICE_CERTIFICATE_AUTHORITY_PEM  ""

#endif /* APPLICATION_CODE_TASKS_INCLUDE_IOT_CONFIG_H_ */
