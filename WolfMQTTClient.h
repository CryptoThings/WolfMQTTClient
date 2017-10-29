/*
 * Copyright (C) 2016-2017 Robert Totte
 *
 * This is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#ifndef __WOLFMQTTCLIENT_H__
#define __WOLFMQTTCLIENT_H__

#include <Arduino.h>
#include <Client.h>
#include <stdint.h>

#include "wolfmqtt/mqtt_packet.h"

class WolfMQTTClient;

/* Function callbacks */
typedef int (*MqttTlsCb)(WolfMQTTClient* client);

typedef int (*MqttNetConnectCb)(void *context,
    const char* host, word16 port, int timeout_ms);
typedef int (*MqttNetWriteCb)(void* ssl, void *context,
    const byte* buf, int buf_len, int timeout_ms);
typedef int (*MqttNetReadCb)(void* ssl, void *context,
    byte* buf, int buf_len, int timeout_ms);
typedef int (*MqttNetDisconnectCb)(void *context);

/*! \brief      Mqtt Message Callback
 *  \discussion If the message payload is larger than the maximum RX buffer
    then this callback is called multiple times.
    If msg_new = 1 its a new message.
    The topc_name and topic_name length are only valid when msg_new = 1.
    If msg_new = 0 then we are receiving additional payload.
    Each callback populates the payload in MqttMessage.buffer.
    The MqttMessage.buffer_len is the size of the buffer payload.
    The MqttMessage.buffer_pos is the location in the total payload.
    The MqttMessage.total_len is the length of the complete payload message.
    If msg_done = 1 the entire publish payload has been received.
 *  \param      client      Pointer to MqttClient structure
 *  \param      message     Pointer to MqttNet structure that has been
                            initialized with callback pointers and context
 *  \param      msg_new     If non-zero value then message is new and topic
                            name / len is provided and valid.
 *  \param      msg_done    If non-zero value then we have received the entire
                            message and payload.
 *  \return     MQTT_CODE_SUCCESS to remain connected (other values will cause
                net disconnect - see enum MqttPacketResponseCodes)
 */
typedef int (*MqttMsgCb)(WolfMQTTClient *client, MqttMessage *message,
    byte msg_new, byte msg_done);

/* Client flags */
enum MqttClientFlags {
    MQTT_CLIENT_FLAG_IS_CONNECTED = 0x01,
    MQTT_CLIENT_FLAG_IS_TLS = 0x02,
};

#define MQTT_SECURE_PORT    8883


class WolfMQTTCallback {
public:
  virtual void message(char *topic, uint8_t *payload, size_t payload_len) = 0;
};

class WolfMQTTClient {
private:
  Client* net_client;
  WolfMQTTCallback *callback;

//  MqttNet mqtt_client_net;

  static const size_t MAX_BUFFER_SIZE = 512;
  uint8_t tx_buf[MAX_BUFFER_SIZE];
  uint8_t rx_buf[MAX_BUFFER_SIZE];

  static const size_t MAX_SERVER_NAME_SIZE = 64;
  char server_name[MAX_SERVER_NAME_SIZE];
  uint16_t server_port;

  typedef enum {
    STATE_OK,
    STATE_ERROR
  } mqtt_state;
  mqtt_state state;

  static const uint32_t cmd_timeout_ms = 3000;
  uint32_t mqtt_client_ping_timer;
#if 0
  struct cert_buf {
    uint16_t cert_type;
    uint8_t *data;
    size_t len;
    cert_buf() : cert_type(0), data(NULL), len(0) {}
  };

  cert_buf verify_buffer;
  cert_buf cert_chain;
  cert_buf cert_buffer;
  cert_buf private_key;
#endif

public:
  WolfMQTTClient();

  WolfMQTTClient& setServer(const char * domain, uint16_t port);
  WolfMQTTClient& setCallback(WolfMQTTCallback &);
  WolfMQTTClient& setClient(Client& client);

  bool connect(const char *id);
  bool connect(const char *id, uint32_t id_int);
  bool publish(const char *topic, const char *payload);
  bool publish(const char *topic, const uint8_t *payload, size_t plength);

  bool subscribe(const char* topic);
  bool subscribe(const char* topic, MqttQoS qos);
  bool unsubscribe(const char* topic);
  bool loop(int timeout_ms = 0);
  bool connected();

  static int MqttClientNet_Connect(void *_context,
        const char* host, word16 port, int timeout_ms);

  static int MqttClientNet_Read(void* ssl, void *_context,
        uint8_t* buf, int buf_len, int timeout_ms);

  static int MqttClientNet_Write(void* ssl, void *_context,
        const uint8_t* buf, int buf_len, int timeout_ms);

  static int MqttClientNet_Disconnect(void *_context);

  static int MqttClient_Message_cb(WolfMQTTClient *client, MqttMessage *msg,
    uint8_t msg_new, uint8_t msg_done);

/*
Register client cert
Connect
Topic Class (with callback)
Subscribe
Subscribe Callback function
Subscribe callback class
Publish
Unsubscribe
Disconnect
*/

public:
    word32       m_flags; /* MqttClientFlags */
    int          m_cmd_timeout_ms;

    byte        *m_tx_buf;
    int          m_tx_buf_len;
    byte        *m_rx_buf;
    int          m_rx_buf_len;

//    MqttNet     *m_net;   /* Pointer to network callbacks and context */
    void        *m_context;

    MqttMsgCb    m_msg_cb;

private:

int MqttClient_WaitType(int timeout_ms,
    byte wait_type, word16 wait_packet_id, void* p_decode);
int MqttClient_Init(
    MqttMsgCb msg_cb,
    byte* tx_buf, int tx_buf_len,
    byte* rx_buf, int rx_buf_len,
    int cmd_timeout_ms);
int MqttClient_Connect(MqttConnect *connect);
int MqttClient_Publish(MqttPublish *publish);
int MqttClient_Subscribe(MqttSubscribe *subscribe);
int MqttClient_Unsubscribe(MqttUnsubscribe *unsubscribe);
int MqttClient_Ping();
int MqttClient_Disconnect();
int MqttClient_WaitMessage(int timeout_ms);
int MqttClient_NetConnect(const char* host,
    word16 port, int timeout_ms, int use_tls, MqttTlsCb cb);
int MqttClient_NetDisconnect();
int MqttPacket_Read(byte* rx_buf, int rx_buf_len, int timeout_ms);

static const char* MqttClient_ReturnCodeToString(int return_code);

public:
int MqttSocket_Write(const byte* buf, int buf_len,
    int timeout_ms);
int MqttSocket_Read(byte* buf, int buf_len,
    int timeout_ms);


};

typedef class WolfMQTTClient MqttClient;

#endif // __WOLFMQTTCLIENT_H__
