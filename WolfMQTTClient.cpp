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

#include <WolfMQTTClient.h>

extern "C"
void Logging_cb(const int logLevel, const char *const logMessage);

static char g_print_buf[80];

#define T_PRINTF(...)    \
{ \
  snprintf(g_print_buf, 80, __VA_ARGS__); \
  Logging_cb(1, g_print_buf); \
}

WolfMQTTClient::WolfMQTTClient()
{
  int rc;

  rc = MqttClient_Init(
        MqttClient_Message_cb, tx_buf, MAX_BUFFER_SIZE,
        rx_buf, MAX_BUFFER_SIZE, cmd_timeout_ms);

  if (rc != MQTT_CODE_SUCCESS) {
    state = STATE_ERROR;
    return;
  }

  state = STATE_OK;

  mqtt_client_ping_timer = millis();
}
/*
WolfMQTTClient& WolfMQTTClient::setServer(IPAddress ip, uint16_t port)
{
  return *this;
}

WolfMQTTClient& WolfMQTTClient::setServer(uint8_t *ip, uint16_t port)
{
  return *this;
}
*/
WolfMQTTClient& WolfMQTTClient::setServer(const char *domain, uint16_t port)
{
  return *this;
}

WolfMQTTClient& WolfMQTTClient::setCallback(WolfMQTTCallback &cb)
{
  callback = &cb;
  return *this;
}

WolfMQTTClient& WolfMQTTClient::setClient(Client& client)
{
  net_client = &client;
  return *this;
}


bool WolfMQTTClient::connect(const char *id)
{
  int rc;

  do {
    Serial.println("MqttClient_NetConnect Start");
    Serial.flush();
    /* Connect to broker */
    rc = MqttClient_NetConnect(
        "a21wtlfjxarim7.iot.us-west-2.amazonaws.com", 8883,
        cmd_timeout_ms, 1, NULL);
    if (rc != MQTT_CODE_SUCCESS) {
      Serial.print("ERROR: MqttClient_NetConnect ");
      Serial.println(rc);
      break;
    }
    Serial.println("MqttClient_NetConnect Done");
    Serial.flush();

    /* Define connect parameters */
    MqttConnect connect;
    XMEMSET(&connect, 0, sizeof(MqttConnect));
    connect.keep_alive_sec = 60;
    connect.clean_session = 1;
    connect.client_id = id;
    /* Last will and testament sent by broker to subscribers
        of topic when broker connection is lost */
    connect.lwt_msg = NULL;
    connect.enable_lwt = 0;;
    /* Optional authentication */
    connect.username = NULL;
    connect.password = NULL;

    /* Send Connect and wait for Connect Ack */
    rc = MqttClient_Connect(&connect);
    if (rc == MQTT_CODE_SUCCESS) {
        break;
    }
    Serial.println("ERROR: MqttClient_Connect");
  } while (0);

  Serial.println("MqttClient_Connect Done");
  Serial.flush();

  return (rc == MQTT_CODE_SUCCESS);
}

bool WolfMQTTClient::connect(const char *id, uint32_t id_int)
{

  return true;
}

bool WolfMQTTClient::publish(const char *topic, const char *payload)
{

  return true;
}

#define MAX_PACKET_ID           ((1 << 16) - 1)

static int mPacketIdLast = 100;

static word16 mqttclient_get_packetid(void)
{
    mPacketIdLast = (mPacketIdLast >= MAX_PACKET_ID) ?
        1 : mPacketIdLast + 1;
    return (word16)mPacketIdLast;
}

bool WolfMQTTClient::publish(const char *topic, const uint8_t *payload, size_t plength)
{
  int rc;
  MqttPublish publish;

  do {
    /* Publish Topic */
    XMEMSET(&publish, 0, sizeof(MqttPublish));
    publish.retain = 0; // AWS IoT does not support retain
    publish.qos = MQTT_QOS_1;
    publish.duplicate = 0;
    publish.topic_name = topic;
    publish.packet_id = mqttclient_get_packetid();
    publish.buffer = (byte*)payload;
    publish.total_len = plength;
    rc = MqttClient_Publish(&publish);
    if (rc != MQTT_CODE_SUCCESS) {
        break;
    }
  } while (0);

  return (rc == MQTT_CODE_SUCCESS);
}

bool WolfMQTTClient::subscribe(const char* topic)
{
  return subscribe(topic, MQTT_QOS_1);
}

bool WolfMQTTClient::subscribe(const char* topic, MqttQoS qos)
{
  int rc, i;
  MqttTopic topics[1], *mtopic;

  MqttSubscribe subscribe;

  do {
    Serial.print("MQTT Subscribe Start: Topic ");
    Serial.println(topic);

    /* Build list of topics */
    topics[0].topic_filter = topic;
    topics[0].qos = qos;

    /* Subscribe Topic */
    XMEMSET(&subscribe, 0, sizeof(MqttSubscribe));
    subscribe.packet_id = mqttclient_get_packetid();
    subscribe.topic_count = sizeof(topics)/sizeof(MqttTopic);
    subscribe.topics = topics;
    rc = MqttClient_Subscribe(&subscribe);

    Serial.print("MQTT Subscribe: ");
    Serial.println(MqttClient_ReturnCodeToString(rc));

    if (rc != MQTT_CODE_SUCCESS) {
        return false;
    }
    for (i = 0; i < subscribe.topic_count; i++) {
        mtopic = &subscribe.topics[i];
        Serial.print("Topic ");
        Serial.print(mtopic->topic_filter);
        Serial.print(" Qos ");
        Serial.print(mtopic->qos);
        Serial.print(" Return Code ");
        Serial.println(mtopic->return_code);
    }

  } while (0);

  return true;
}

bool WolfMQTTClient::unsubscribe(const char* topic)
{

  return true;
}

bool WolfMQTTClient::loop()
{
  int rc;

  /* Read Loop */
  rc = MqttClient_WaitMessage(cmd_timeout_ms);
  if (rc == MQTT_CODE_ERROR_TIMEOUT) {
    if ((millis() - mqtt_client_ping_timer) > 60000) {
      rc = MqttClient_Ping();
      if (rc != MQTT_CODE_SUCCESS) {
        Serial.print("MQTT Ping Keep Alive Error: ");
        Serial.println(MqttClient_ReturnCodeToString(rc));
        return false;
      }
//      Serial.println("MQTT Ping");
      mqtt_client_ping_timer = millis();
    }
  } else if (rc != MQTT_CODE_SUCCESS) {
    /* There was an error */
    Serial.print("MQTT Message Wait: ");
    Serial.print(rc);
    Serial.print(" ");
    Serial.println(MqttClient_ReturnCodeToString(rc));
    return false;
  }

  return true;
}

bool WolfMQTTClient::connected()
{

  return true;
}

int WolfMQTTClient::MqttClientNet_Connect(void *_context, const char* host,
      word16 port, int timeout_ms)
{
  int ret;
  WolfMQTTClient *context = (WolfMQTTClient *)_context;
  Client *c = context->net_client;

  Serial.println("MqttClientNet_Connect");
  Serial.flush();

  ret = c->connect(host, port);
  if (!ret) {
    c->stop();
    return -1;
  }
  return 0;
}

int WolfMQTTClient::MqttClientNet_Read(void* ssl, void *_context,
      uint8_t* buf, int buf_len, int timeout_ms)
{
  WolfMQTTClient *context = (WolfMQTTClient *)_context;
  Client *c = context->net_client;
  uint32_t t;
  int ret = 0;

  t = millis();

  while ((ret < buf_len) && ((millis() - t) < (uint32_t)timeout_ms)) {
    if (c->available() > 0) {
      int r_count;
      r_count = (((buf_len-ret) > 256) ? 256 : (buf_len-ret));
      r_count = c->read(buf+ret, r_count);
      ret += r_count;
      if (r_count < 0) {
        ret = -1;
        break;
      }
    }
    delay(1);
  }

  return ret;
} 

int WolfMQTTClient::MqttClientNet_Write(void* ssl, void *_context,
      const uint8_t* buf, int buf_len, int timeout_ms)
{
  int ret = 0;
  WolfMQTTClient *context = (WolfMQTTClient *)_context;
  Client *c = context->net_client;

  ret = c->write(buf+ret, buf_len-ret);
  if (ret == 0) {
    return -1;
  }

  return (ret);
}

int WolfMQTTClient::MqttClientNet_Disconnect(void *_context)
{
  WolfMQTTClient *context = (WolfMQTTClient *)_context;
  Client *c = context->net_client;

  c->stop();

  return 0;
}

int WolfMQTTClient::MqttClient_Message_cb(WolfMQTTClient *client, MqttMessage *msg,
    uint8_t msg_new, uint8_t msg_done)
{
  char topic[80];
  if (client == NULL)
    return -1;

  if (client->callback == NULL)
    return -1;

  memset(topic, 0, 80);
  memcpy(topic, msg->topic_name,
    ((msg->topic_name_len > 79) ? 79 : msg->topic_name_len));

  if (msg_new) {
    client->callback->message(topic, msg->buffer, msg->buffer_len);
  }
  // TODO: handle multipart messages

  return 0;
}

extern "C"
void atca_delay_ms(uint32_t d)
{
    delay(d);
}

