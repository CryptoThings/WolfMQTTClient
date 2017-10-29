/* mqtt_client.c
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

/* Include the autoconf generated config.h */
#ifdef HAVE_CONFIG_H
    #include <config.h>
#endif

#include <WolfMQTTClient.h>

//#define WOLFMQTT_DEBUG_CLIENT

#ifdef WOLFMQTT_DEBUG_CLIENT
extern "C"
void Logging_cb(const int logLevel, const char *const logMessage);

static char g_print_buf[80];

#define T_PRINTF(...)    \
{ \
  snprintf(g_print_buf, 80, __VA_ARGS__); \
  Logging_cb(1, g_print_buf); \
}
#endif // WOLFMQTT_DEBUG_CLIENT

#define T_PRINTF(...)

/* Private functions */
int WolfMQTTClient::MqttClient_WaitType(int timeout_ms,
    byte wait_type, word16 wait_packet_id, void* p_decode)
{
    int rc;
    MqttPacket* header;
    byte msg_type, msg_qos;
    word16 packet_id = 0;
    int packet_len;

    while (1) {
        /* Wait for packet */
        rc = MqttPacket_Read(m_rx_buf, m_rx_buf_len,
            timeout_ms);
        if (rc <= 0) { return rc; }
        packet_len = rc;

        /* Determine packet type */
        header = (MqttPacket*)m_rx_buf;
        msg_type = MQTT_PACKET_TYPE_GET(header->type_flags);
        msg_qos = MQTT_PACKET_FLAGS_GET_QOS(header->type_flags);

#ifdef WOLFMQTT_DEBUG_CLIENT
        T_PRINTF("Read Packet: Len %d, Type %d, Qos %d",
            packet_len, msg_type, msg_qos);
#endif

        switch(msg_type) {
            case MQTT_PACKET_TYPE_CONNECT_ACK:
            {
                /* Decode connect ack */
                MqttConnectAck connect_ack, *p_connect_ack = &connect_ack;
                if (p_decode) {
                    p_connect_ack = (MqttConnectAck*)p_decode;
                }
                rc = MqttDecode_ConenctAck(m_rx_buf, packet_len,
                    p_connect_ack);
                if (rc <= 0) { return rc; }
                break;
            }
            case MQTT_PACKET_TYPE_PUBLISH:
            {
                MqttMessage msg;
                byte msg_new = 1;
                byte msg_done;

                /* Decode publish message */
                rc = MqttDecode_Publish(m_rx_buf, packet_len, &msg);
                if (rc <= 0) { return rc; }

                /* Handle packet callback and read remaining payload */
                do {
                    /* Determine if message is done */
                    msg_done =
                        ((msg.buffer_pos + msg.buffer_len) >= msg.total_len) ?
                        1 : 0;

                    /* Issue callback for new message */
                    if (m_msg_cb) {
                        if (!msg_new) {
                            /* Reset topic name since valid on new message only */
                            msg.topic_name = NULL;
                            msg.topic_name_len = 0;
                        }
                        rc = m_msg_cb(this, &msg, msg_new, msg_done);
                        if (rc != MQTT_CODE_SUCCESS) { return rc; };
                    }

                    /* Read payload */
                    if (!msg_done) {
                        int msg_len;

                        msg.buffer_pos += msg.buffer_len;
                        msg.buffer_len = 0;

                        msg_len = (msg.total_len - msg.buffer_pos);
                        if (msg_len > m_rx_buf_len) {
                            msg_len = m_rx_buf_len;
                        }

                        rc = MqttSocket_Read(m_rx_buf, msg_len,
                            timeout_ms);
                        if (rc != msg_len) { return rc; }

                        /* Update message */
                        msg.buffer = m_rx_buf;
                        msg.buffer_len = msg_len;
                    }
                    msg_new = 0;
                } while (!msg_done);

                /* Handle Qos */
                if (msg_qos > MQTT_QOS_0) {
                    MqttPublishResp publish_resp;
                    MqttPacketType type;

                    packet_id = msg.packet_id;

                    /* Determine packet type to write */
                    type = (msg_qos == MQTT_QOS_1) ?
                        MQTT_PACKET_TYPE_PUBLISH_ACK :
                        MQTT_PACKET_TYPE_PUBLISH_REC;
                    publish_resp.packet_id = packet_id;

                    /* Encode publish response */
                    rc = MqttEncode_PublishResp(m_tx_buf,
                        m_tx_buf_len, type, &publish_resp);
                    if (rc <= 0) { return rc; }
                    packet_len = rc;

                    /* Send packet */
                    rc = MqttSocket_Write(m_tx_buf, packet_len, m_cmd_timeout_ms);
                    if (rc != packet_len) { return rc; }
                }
                break;
            }
            case MQTT_PACKET_TYPE_PUBLISH_ACK:
            case MQTT_PACKET_TYPE_PUBLISH_REC:
            case MQTT_PACKET_TYPE_PUBLISH_REL:
            case MQTT_PACKET_TYPE_PUBLISH_COMP:
            {
                MqttPublishResp publish_resp, *p_publish_resp = &publish_resp;
                if (p_decode) {
                    p_publish_resp = (MqttPublishResp*)p_decode;
                }

                /* Decode publish response message */
                rc = MqttDecode_PublishResp(m_rx_buf, packet_len,
                    msg_type, p_publish_resp);
                if (rc <= 0) { return rc; }
                packet_id = p_publish_resp->packet_id;

                /* If Qos then send response */
                if (msg_type == MQTT_PACKET_TYPE_PUBLISH_REC ||
                    msg_type == MQTT_PACKET_TYPE_PUBLISH_REL) {

                    /* Encode publish response */
                    publish_resp.packet_id = packet_id;
                    rc = MqttEncode_PublishResp(m_tx_buf,
                        m_tx_buf_len, msg_type+1, &publish_resp);
                    if (rc <= 0) { return rc; }
                    packet_len = rc;

                    /* Send packet */
                    rc = MqttSocket_Write(m_tx_buf, packet_len, m_cmd_timeout_ms);
                    if (rc != packet_len) { return rc; }
                }
                break;
            }
            case MQTT_PACKET_TYPE_SUBSCRIBE_ACK:
            {
                /* Decode subscribe ack */
                MqttSubscribeAck subscribe_ack;
                MqttSubscribeAck *p_subscribe_ack = &subscribe_ack;
                if (p_decode) {
                    p_subscribe_ack = (MqttSubscribeAck*)p_decode;
                }
                rc = MqttDecode_SubscribeAck(m_rx_buf, packet_len,
                    p_subscribe_ack);
                if (rc <= 0) { return rc; }
                packet_id = p_subscribe_ack->packet_id;
                break;
            }
            case MQTT_PACKET_TYPE_UNSUBSCRIBE_ACK:
            {
                /* Decode unsubscribe ack */
                MqttUnsubscribeAck unsubscribe_ack;
                MqttUnsubscribeAck *p_unsubscribe_ack = &unsubscribe_ack;

                if (p_decode) {
                    p_unsubscribe_ack = (MqttUnsubscribeAck*)p_decode;
                }
                rc = MqttDecode_UnsubscribeAck(m_rx_buf, packet_len,
                    p_unsubscribe_ack);
                if (rc <= 0) { return rc; }
                packet_id = p_unsubscribe_ack->packet_id;
                break;
            }
            case MQTT_PACKET_TYPE_PING_RESP:
                /* Decode ping */
                rc = MqttDecode_Ping(m_rx_buf, packet_len);
                if (rc <= 0) { return rc; }
                break;

            default:
                /* Other types are server side only, ignore */
#ifdef WOLFMQTT_DEBUG_CLIENT
                T_PRINTF("MqttClient_WaitMessage: Invalid client packet type %u!",
                    msg_type);
#endif
                break;
        }

        /* Check for type and packet id */
        if (wait_type < MQTT_PACKET_TYPE_MAX) {
            if (wait_type == msg_type) {
                if (wait_packet_id == 0 || wait_packet_id == packet_id) {
                    /* We found the packet type and id */
                    break;
                }
            }
        }
        else {
            /* We got a message, so return now */
            break;
        }
    }

    return MQTT_CODE_SUCCESS;
}

/* Public Functions */
int WolfMQTTClient::MqttClient_Init(
    MqttMsgCb msg_cb,
    byte* tx_buf, int tx_buf_len,
    byte* rx_buf, int rx_buf_len,
    int cmd_timeout_ms)
{
    int rc = MQTT_CODE_SUCCESS;

    /* Check arguments */
    if (tx_buf == NULL || tx_buf_len <= 0 ||
        rx_buf == NULL || rx_buf_len <= 0) {
        return MQTT_CODE_ERROR_BAD_ARG;
    }

    /* Initialize the client structure to zero */
//    XMEMSET(client, 0, sizeof(MqttClient));

    /* Setup client structure */
    m_msg_cb = msg_cb;
    m_flags = 0;
    m_tx_buf = tx_buf;
    m_tx_buf_len = tx_buf_len;
    m_rx_buf = rx_buf;
    m_rx_buf_len = rx_buf_len;
    m_cmd_timeout_ms = cmd_timeout_ms;

    m_flags &= ~(MQTT_CLIENT_FLAG_IS_CONNECTED |
            MQTT_CLIENT_FLAG_IS_TLS);

    return rc;
}

int WolfMQTTClient::MqttClient_Connect(MqttConnect *connect)
{
    int rc, len;

    /* Validate required arguments */
    if (connect == NULL) {
        return MQTT_CODE_ERROR_BAD_ARG;
    }

    /* Encode the connect packet */
    rc = MqttEncode_Connect(m_tx_buf, m_tx_buf_len, connect);
    if (rc <= 0) { return rc; }
    len = rc;

    /* Send connect packet */
    rc = MqttSocket_Write(m_tx_buf, len, m_cmd_timeout_ms);
    if (rc != len) { return rc; }

    /* Wait for connect ack packet */
    rc = MqttClient_WaitType(m_cmd_timeout_ms,
        MQTT_PACKET_TYPE_CONNECT_ACK, 0, &connect->ack);

    return rc;
}

int WolfMQTTClient::MqttClient_Publish(MqttPublish *publish)
{
    int rc, len;

    /* Validate required arguments */
    if (publish == NULL) {
        return MQTT_CODE_ERROR_BAD_ARG;
    }

    /* Encode the publish packet */
    rc = MqttEncode_Publish(m_tx_buf, m_tx_buf_len, publish);
    if (rc <= 0) { return rc; }
    len = rc;

    /* Send packet and payload */
    do {
        rc = MqttSocket_Write(m_tx_buf, len, m_cmd_timeout_ms);
        if (rc != len) { return rc; }
        publish->buffer_pos += publish->buffer_len;
        publish->buffer_len = 0;

        /* Check if there is anything left to send */
        if (publish->buffer_pos >= publish->total_len) {
            rc = MQTT_CODE_SUCCESS;
            break;
        }

        /* Build packet payload to send */
        len = (publish->total_len - publish->buffer_pos);
        if (len > m_tx_buf_len) {
            len = m_tx_buf_len;
        }
        publish->buffer_len = len;
        XMEMCPY(m_tx_buf, &publish->buffer[publish->buffer_pos], len);
    } while (publish->buffer_pos < publish->total_len);

    /* Handle QoS */
    if (publish->qos > MQTT_QOS_0) {
        /* Determine packet type to wait for */
        MqttPacketType type = (publish->qos == MQTT_QOS_1) ?
            MQTT_PACKET_TYPE_PUBLISH_ACK : MQTT_PACKET_TYPE_PUBLISH_COMP;

        /* Wait for publish response packet */
        rc = MqttClient_WaitType(m_cmd_timeout_ms,
            type, publish->packet_id, NULL);
    }

    return rc;
}

int WolfMQTTClient::MqttClient_Subscribe(MqttSubscribe *subscribe)
{
    int rc, len, i;
    MqttSubscribeAck subscribe_ack;
    MqttTopic* topic;

    /* Validate required arguments */
    if (subscribe == NULL) {
        return MQTT_CODE_ERROR_BAD_ARG;
    }

    /* Encode the subscribe packet */
    rc = MqttEncode_Subscribe(m_tx_buf, m_tx_buf_len, subscribe);
    if (rc <= 0) { return rc; }
    len = rc;

    /* Send subscribe packet */
    rc = MqttSocket_Write(m_tx_buf, len, m_cmd_timeout_ms);
    if (rc != len) { return rc; }

    /* Wait for subscribe ack packet */
    rc = MqttClient_WaitType(m_cmd_timeout_ms,
        MQTT_PACKET_TYPE_SUBSCRIBE_ACK, subscribe->packet_id, &subscribe_ack);

    /* Populate return codes */
    if (rc == MQTT_CODE_SUCCESS) {
        for (i = 0; i < subscribe->topic_count; i++) {
            topic = &subscribe->topics[i];
            topic->return_code = subscribe_ack.return_codes[i];
        }
    }

    return rc;
}

int WolfMQTTClient::MqttClient_Unsubscribe(MqttUnsubscribe *unsubscribe)
{
    int rc, len;
    MqttUnsubscribeAck unsubscribe_ack;

    /* Validate required arguments */
    if (unsubscribe == NULL) {
        return MQTT_CODE_ERROR_BAD_ARG;
    }

    /* Encode the subscribe packet */
    rc = MqttEncode_Unsubscribe(m_tx_buf, m_tx_buf_len,
        unsubscribe);
    if (rc <= 0) { return rc; }
    len = rc;

    /* Send unsubscribe packet */
    rc = MqttSocket_Write(m_tx_buf, len, m_cmd_timeout_ms);
    if (rc != len) { return rc; }

    /* Wait for unsubscribe ack packet */
    rc = MqttClient_WaitType(m_cmd_timeout_ms,
        MQTT_PACKET_TYPE_UNSUBSCRIBE_ACK, unsubscribe->packet_id,
            &unsubscribe_ack);

    return rc;
}

int WolfMQTTClient::MqttClient_Ping()
{
    int rc, len;

    /* Encode the subscribe packet */
    rc = MqttEncode_Ping(m_tx_buf, m_tx_buf_len);
    if (rc <= 0) { return rc; }
    len = rc;

    /* Send ping req packet */
    rc = MqttSocket_Write(m_tx_buf, len, m_cmd_timeout_ms);
    if (rc != len) { return rc; }

    /* Wait for ping resp packet */
    rc = MqttClient_WaitType(m_cmd_timeout_ms,
        MQTT_PACKET_TYPE_PING_RESP, 0, NULL);

    return rc;
}

int WolfMQTTClient::MqttClient_Disconnect()
{
    int rc, len;

    /* Encode the disconnect packet */
    rc = MqttEncode_Disconnect(m_tx_buf, m_tx_buf_len);
    if (rc <= 0) { return rc; }
    len = rc;

    /* Send disconnect packet */
    rc = MqttSocket_Write(m_tx_buf, len, m_cmd_timeout_ms);
    if (rc != len) { return rc; }

    /* No response for MQTT disconnect packet */

    return MQTT_CODE_SUCCESS;
}


int WolfMQTTClient::MqttClient_WaitMessage(int timeout_ms)
{
    return MqttClient_WaitType(timeout_ms, MQTT_PACKET_TYPE_MAX, 
        0, NULL);
}

int WolfMQTTClient::MqttClient_NetConnect(const char* host,
    word16 port, int timeout_ms, int use_tls, MqttTlsCb cb)
{
    int rc;

    /* Validate arguments */
    /* Validate port */
    if (port == 0) {
        port = MQTT_SECURE_PORT;
    }

    if (cb) {
      rc = cb(this);
    }

    rc = net_client->connect(host, port);
    if (!rc) {
      T_PRINTF("MqttSocket_Connect connect @%d\n", __LINE__);
      MqttClient_NetDisconnect();
      return MQTT_CODE_ERROR_TLS_CONNECT;
    }

    m_flags |= MQTT_CLIENT_FLAG_IS_CONNECTED;
    m_flags |= MQTT_CLIENT_FLAG_IS_TLS;

    return MQTT_CODE_SUCCESS;
}

int WolfMQTTClient::MqttClient_NetDisconnect()
{
    int rc = MQTT_CODE_SUCCESS;

    net_client->stop();
    m_flags &= ~MQTT_CLIENT_FLAG_IS_TLS;
    m_flags &= ~MQTT_CLIENT_FLAG_IS_CONNECTED;
    
    T_PRINTF("MqttClient_NetDisconnect: Rc=%d\n", rc);

    /* Check for error */
    if (rc < 0) {
        rc = MQTT_CODE_ERROR_NETWORK;
    }

    return rc;
}

/* Read return code is length when > 0 */
int WolfMQTTClient::MqttPacket_Read(byte* rx_buf, int rx_buf_len,
    int timeout_ms)
{
    int rc, len, header_len = 2, remain_len = 0;
    MqttPacket* header = (MqttPacket*)rx_buf;

    /* Read fix header portion */
    rc = MqttSocket_Read(&rx_buf[0], header_len, timeout_ms);
    if (rc != header_len) {
        return rc;
    }

    do {
        /* Try and decode remaining length */
        rc = MqttDecode_RemainLen(header, header_len, &remain_len);
        if (rc < 0) { /* Indicates error */
            return rc;
        }
        /* Indicates decode success and rc is len of header */
        else if (rc > 0) {
            header_len = rc;
            break;
        }

        /* Read next byte and try decode again */
        len = 1;
        rc = MqttSocket_Read(&rx_buf[header_len], len, timeout_ms);
        if (rc != len) {
            return rc;
        }
        header_len += len;
    } while (header_len < MQTT_PACKET_MAX_SIZE);

    /* Make sure it does not overflow rx_buf */
    if (remain_len > (rx_buf_len - header_len)) {
        remain_len = rx_buf_len - header_len;
    }

    /* Read remaining */
    if (remain_len > 0) {
        rc = MqttSocket_Read(&rx_buf[header_len], remain_len,
            timeout_ms);
        if (rc != remain_len) {
            return rc;
        }
    }

    /* Return read packet length */
    return header_len + remain_len;
}
const char* WolfMQTTClient::MqttClient_ReturnCodeToString(int return_code)
{
    switch(return_code) {
        case MQTT_CODE_SUCCESS:
            return "Success";
        case MQTT_CODE_ERROR_BAD_ARG:
            return "Error (Bad argument)";
        case MQTT_CODE_ERROR_OUT_OF_BUFFER:
            return "Error (Out of buffer)";
        case MQTT_CODE_ERROR_MALFORMED_DATA:
            return "Error (Malformed Remaining Length)";
        case MQTT_CODE_ERROR_PACKET_TYPE:
            return "Error (Packet Type Mismatch)";
        case MQTT_CODE_ERROR_PACKET_ID:
            return "Error (Packet Id Mismatch)";
        case MQTT_CODE_ERROR_TLS_CONNECT:
            return "Error (TLS Connect)";
        case MQTT_CODE_ERROR_TIMEOUT:
            return "Error (Timeout)";
        case MQTT_CODE_ERROR_NETWORK:
            return "Error (Network)";
        case MQTT_CODE_ERROR_MEMORY:
            return "Error (Memory)";
    }
    return "Unknown";
}
