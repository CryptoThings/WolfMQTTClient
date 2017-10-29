/* mqtt_socket.c
 *
 * Copyright (C) 2006-2016 wolfSSL Inc.
 *
 * This file is part of wolfMQTT.
 *
 * wolfMQTT is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * wolfMQTT is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1335, USA
 */

#include <WolfMQTTClient.h>

/* Options */
//#define WOLFMQTT_DEBUG_SOCKET
#ifdef WOLFMQTT_NO_STDIO
    #undef WOLFMQTT_DEBUG_SOCKET
#endif

#ifdef WOLFMQTT_DEBUG_SOCKET
extern "C"
void Logging_cb(const int logLevel, const char *const logMessage);

static char g_print_buf[80];

#define T_PRINTF(...)    \
{ \
  snprintf(g_print_buf, 80, __VA_ARGS__); \
  Logging_cb(1, g_print_buf); \
}
#endif // WOLFMQTT_DEBUG_SOCKET

#define T_PRINTF(...)

/* Public Functions */

int WolfMQTTClient::MqttSocket_Write(const byte* buf, int buf_len, int timeout_ms)
{
    int rc;

    /* Validate arguments */
    if (buf == NULL || buf_len <= 0) {
        T_PRINTF("MqttSocket_Write @%d\n", __LINE__);
        return MQTT_CODE_ERROR_BAD_ARG;
    }
    rc = net_client->write(buf, buf_len);

    /* Check for error */
    if (rc < 0) {
      T_PRINTF("MqttSocket_Write @%d\n", __LINE__);
      rc = MQTT_CODE_ERROR_NETWORK;
    }

    return rc;
}

int WolfMQTTClient::MqttSocket_Read(byte* buf, int buf_len, int timeout_ms)
{
    int rc=0;

    /* Validate arguments */
    if (buf == NULL || buf_len <= 0) {
        T_PRINTF("MqttSocket_Read @%d\n", __LINE__);
        return MQTT_CODE_ERROR_BAD_ARG;
    }

    rc = net_client->read(buf, buf_len);

    /* Check for timeout */
    if (rc == 0) {
        rc = MQTT_CODE_ERROR_TIMEOUT;
    }
    else if (rc < 0) {
        rc = MQTT_CODE_ERROR_NETWORK;
    }

    return rc;
}


