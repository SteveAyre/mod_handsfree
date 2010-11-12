/* 
 * FreeSWITCH Modular Media Switching Software Library / Soft-Switch Application
 * Copyright (C) 2005-2010, Anthony Minessale II <anthm@freeswitch.org>
 *
 * Version: MPL 1.1
 *
 * The contents of this file are subject to the Mozilla Public License Version
 * 1.1 (the "License"); you may not use this file except in compliance with
 * the License. You may obtain a copy of the License at
 * http://www.mozilla.org/MPL/
 *
 * Software distributed under the License is distributed on an "AS IS" basis,
 * WITHOUT WARRANTY OF ANY KIND, either express or implied. See the License
 * for the specific language governing rights and limitations under the
 * License.
 *
 * The Original Code is FreeSWITCH Modular Media Switching Software Library / Soft-Switch Application
 *
 * The Initial Developer of the Original Code is
 * Anthony Minessale II <anthm@freeswitch.org>
 * Portions created by the Initial Developer are Copyright (C)
 * the Initial Developer. All Rights Reserved.
 *
 * This module (mod_handsfree) has been contributed by:
 *
 * Moises Silva (moises.silva@gmail.com)
 *
 * mod_handsfree.c -- Hands Free Profile Module
 *
 */
#include <switch.h>
#include <poll.h>
#include <sys/wait.h>
#include <poll.h>
#include <fcntl.h>
#include "ipc.h"


SWITCH_MODULE_LOAD_FUNCTION(mod_handsfree_load);
SWITCH_MODULE_SHUTDOWN_FUNCTION(mod_handsfree_shutdown);
SWITCH_MODULE_RUNTIME_FUNCTION(mod_handsfree_runtime);
SWITCH_MODULE_DEFINITION(mod_handsfree, mod_handsfree_load, mod_handsfree_shutdown, mod_handsfree_runtime);

#define EVENT_NAME_LEN 255
#define MODEM_NAME_LEN 255
#define MODEM_ID_LEN 255
#define EVENT_SRC_VOICE_CALL_MANAGER "{VoiceCallManager}"
#define EVENT_SRC_VOICE_CALL "{VoiceCall}"
#define EVENT_SRC_MODEM "{Modem}"
#define EVENT_SRC_CALL_VOL "{CallVolume}"
#define EVENT_SRC_NETWORK_REG "{NetworkRegistration}"

typedef enum {
	GFLAG_MY_CODEC_PREFS = (1 << 0)
} GFLAGS;

typedef enum {
	TFLAG_IO = (1 << 0),
	TFLAG_INBOUND = (1 << 1),
	TFLAG_OUTBOUND = (1 << 2),
	TFLAG_DTMF = (1 << 3),
	TFLAG_VOICE = (1 << 4),
	TFLAG_HANGUP = (1 << 5),
	TFLAG_LINEAR = (1 << 6),
	TFLAG_CODEC = (1 << 7),
	TFLAG_BREAK = (1 << 8)
} TFLAGS;

switch_endpoint_interface_t *handsfree_endpoint_interface;

/* SCO connections work with 48 byte-sized frames only */
#define SCO_PCM_MTU 48

#define MODEM_RATE 8000
#define MODEM_INTERVAL 20
typedef struct ofono_modem {
	char id[MODEM_ID_LEN];
	char name[MODEM_NAME_LEN];

	/* audio connection to bluez */
	int audiosock;

	uint8_t pcm_write_buf[SCO_PCM_MTU * 10];
	int write_in_use;
	int writecnt;

	uint8_t pcm_read_buf[SCO_PCM_MTU * 10];
	int read_in_use;
	int readcnt;

	unsigned char databuf[SWITCH_RECOMMENDED_BUFFER_SIZE];
	switch_core_session_t *session;
	switch_caller_profile_t *caller_profile;
	switch_mutex_t *mutex;

	switch_codec_t read_codec;
	switch_codec_t write_codec;
	switch_frame_t read_frame;

	int32_t flags;
	switch_mutex_t *flag_mutex;

	char dialplan[255];
	char context[255];
} ofono_modem_t;

static struct {
	unsigned int flags;
	int calls;
	switch_mutex_t *mutex;
	volatile uint8_t running;
	volatile uint8_t thread_running;
	switch_hash_t *modems;
	switch_memory_pool_t *module_pool;
	int audio_service_fd;
	char dialplan[255];
	char context[255];
	ofono_modem_t modems_array[50];
} globals;

static switch_status_t channel_on_init(switch_core_session_t *session);
static switch_status_t channel_on_hangup(switch_core_session_t *session);
static switch_status_t channel_on_destroy(switch_core_session_t *session);
static switch_status_t channel_on_routing(switch_core_session_t *session);
static switch_status_t channel_on_exchange_media(switch_core_session_t *session);
static switch_status_t channel_on_soft_execute(switch_core_session_t *session);
static switch_call_cause_t channel_outgoing_channel(switch_core_session_t *session, switch_event_t *var_event,
													switch_caller_profile_t *outbound_profile,
													switch_core_session_t **new_session, switch_memory_pool_t **pool, switch_originate_flag_t flags,
													switch_call_cause_t *cancel_cause);
static switch_status_t channel_read_frame(switch_core_session_t *session, switch_frame_t **frame, switch_io_flag_t flags, int stream_id);
static switch_status_t channel_write_frame(switch_core_session_t *session, switch_frame_t *frame, switch_io_flag_t flags, int stream_id);
static switch_status_t channel_kill_channel(switch_core_session_t *session, int sig);

/** SCO audio stuff **/

static int loop_read(int sock, char *buf, ssize_t len)
{
	int r;
	int ret = 0;

	while (len > 0) {
		r = read(sock, buf, len);
		if (r < 0 ) {
			return r;
		}
		if (!r) {
			break;
		}
		ret += r;
		buf += r;
		len -= r;
	}
	return ret;
}

static int loop_write(int sock, char *buf, ssize_t len)
{
	int r = 0;
	int ret = 0;
	while (len > 0) {
		r = write(sock, buf, len);
		if (r < 0) {
			return r;
		}
		if (!r) {
			break;
		}
		ret += r;
		buf += r;
		len -= r;
	}
	return ret;
}	

static int send_message(int servicefd, const bt_audio_msg_header_t *msg)
{
	ssize_t r;
	switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG, "Sending %s -> %s\n", bt_audio_strtype(msg->type), bt_audio_strname(msg->name));
	r = loop_write(servicefd, (char *)msg, msg->length);
	if (r < 0) {
		perror("write");
		return -1;
	}
	if (r != msg->length) {
		switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_WARNING, "Only wrote %d bytes out of %d\n", r, msg->length);
		return -1;
	}
	return 0;
}

static int read_message(int svcsock, bt_audio_msg_header_t *msg, size_t max)
{
	ssize_t r = 0;
	size_t payloadlen = 0;
	char *payload = 0;

	switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG, "Trying to receive message from audio service\n");

	r = loop_read(svcsock, (char *)msg, sizeof(*msg));

	if (r < 0) {
		perror("read");
		return -1;
	}

	if (!r) {
		switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_WARNING, "Bluez server closed the connection\n");
		return -1;
	}

	if (r != sizeof(*msg)) {
		switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_WARNING, "read only %d out of %d bytes, discarding ...\n", r, sizeof(*msg));
		return -1;
	}

	if (msg->length > max) {
		switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_WARNING, "Not enough room to fit %d bytes, only room for %d, discarding ...\n", r, max);
		return -1;
	}

	switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG, "Received %s <- %s of length %d ... reading payload ...\n", bt_audio_strtype(msg->type), bt_audio_strname(msg->name), msg->length);
	if (msg->length > sizeof(*msg)) {
		payloadlen = msg->length - sizeof(*msg);
		payload = ((char *)msg) + sizeof(*msg);
		r = loop_read(svcsock, payload, payloadlen);
		if (r < 0) {
			perror("read");
			return -1;
		}
		if (!r) {
			printf("Server closed the connection\n");
			return -1;
		}
		if (r != payloadlen) {
			fprintf(stderr, "read only %d out of %d payload bytes, discarding ...\n", r, sizeof(*msg));
			return -1;
		}
	}
	switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG, "Finished receiving %s <- %s of length %d\n", bt_audio_strtype(msg->type), bt_audio_strname(msg->name), msg->length);
	return 0;
}

static int print_caps(int servicefd, char *obj)
{
	uint16_t bytes_left;
	const codec_capabilities_t *codec;
	int r = 0;
	union {
		struct bt_get_capabilities_req req;
		struct bt_get_capabilities_rsp rsp;
		bt_audio_error_t error;
		uint8_t buf[BT_SUGGESTED_BUFFER_SIZE];
	} msg;

	memset(&msg, 0, sizeof(msg));

	msg.req.h.type = BT_REQUEST;
	msg.req.h.name = BT_GET_CAPABILITIES;
	msg.req.h.length = sizeof(msg.req);
	msg.req.seid = 0;

	//msg.req.flags = BT_FLAG_AUTOCONNECT;
	msg.req.flags = 0;

	snprintf(msg.req.object, sizeof(msg.req.object), "%s", obj);
	msg.req.transport = BT_CAPABILITIES_TRANSPORT_SCO;

	r = send_message(servicefd, &msg.req.h);
	if (r < 0) {
		return r;
	}

	r = read_message(servicefd, &msg.rsp.h, sizeof(msg));
	if (r < 0) {
		return r;
	}
	if (msg.rsp.h.type == BT_ERROR) {
		switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR, "Failed to get capabilities\n");
		return -1;
	}

	bytes_left = msg.rsp.h.length - sizeof(msg.rsp);
	codec = (codec_capabilities_t *)msg.rsp.data;

	if (bytes_left < sizeof(*codec)) {
		switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR, "%lu bytes are not enough to hold codec data of length %lu\n", 
				(unsigned long)bytes_left, (unsigned long)sizeof(*codec));
		return -1;
	}

	switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG, "Payload size is %lu %lu\n", (unsigned long)bytes_left, (unsigned long)sizeof(*codec));

	switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG, "Codec Seid: %d\n", codec->seid);
	switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG, "Codec Transport: %d\n", codec->transport);
	switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG, "Codec Type: %d\n", codec->type);
	switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG, "Codec Length: %d\n", codec->length);
	
	return 0;
}

static int open_device(int servicefd, char *obj)
{
	int r = 0;
	union {
		struct bt_open_req open_req;
		struct bt_open_rsp open_rsp;
		struct bt_set_configuration_req setconf_req;
		struct bt_set_configuration_rsp setconf_rsp;
		bt_audio_error_t error;
		uint8_t buf[BT_SUGGESTED_BUFFER_SIZE];
	} msg;

	/* now open the device */
	memset(&msg, 0, sizeof(msg));

	msg.open_req.h.type = BT_REQUEST;
	msg.open_req.h.name = BT_OPEN;
	msg.open_req.h.length = sizeof(msg.open_req);
	msg.open_req.seid = 0;

	snprintf(msg.open_req.object, sizeof(msg.open_req.object), "%s", obj);
	/* QUESTION: What is this SEID RANGE thing? */
	msg.open_req.seid = BT_A2DP_SEID_RANGE + 1;
	/* QUESTION: what are those 2 locks? */
	msg.open_req.lock = BT_READ_LOCK | BT_WRITE_LOCK;

	r = send_message(servicefd, &msg.open_req.h);
	if (r < 0) {
		return r;
	}

	r = read_message(servicefd, &msg.open_rsp.h, sizeof(msg));
	if (r < 0) {
		return r;
	}
	if (msg.open_rsp.h.type == BT_ERROR) {
		switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR, "Failed to open device %s\n", obj);
		return -1;
	}

	/* at this point device is open. It seems for HFP we always have: LINEAR16, 1 channel at 8khz
	 * is time to configure ...  */
	memset(&msg, 0, sizeof(msg));

	/* QUESTION: is the request configuration global? why no device is specified? */
	msg.setconf_req.h.type = BT_REQUEST;
	msg.setconf_req.h.name = BT_SET_CONFIGURATION;
	msg.setconf_req.h.length = sizeof(msg.setconf_req);

	msg.setconf_req.codec.transport = BT_CAPABILITIES_TRANSPORT_SCO;
	msg.setconf_req.codec.seid = BT_A2DP_SEID_RANGE + 1;
	msg.setconf_req.codec.length = sizeof(pcm_capabilities_t);
	msg.setconf_req.h.length += msg.setconf_req.codec.length - sizeof(msg.setconf_req.codec);

	r = send_message(servicefd, &msg.setconf_req.h);
	if (r < 0) {
		return r;
	}

	r = read_message(servicefd, &msg.setconf_rsp.h, sizeof(msg));
	if (r < 0) {
		return r;
	}
	if (msg.setconf_rsp.h.type == BT_ERROR) {
		switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR, "Failed to configure device %s\n", obj);
		return -1;
	}

	switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG, "Device %s configured successfuly, MTU = %d\n", obj, msg.setconf_rsp.link_mtu);
	if (msg.setconf_rsp.link_mtu != SCO_PCM_MTU) {
		switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR, "Unsupported MTU %d, we support %d\n", msg.setconf_rsp.link_mtu, SCO_PCM_MTU);
		return -1;
	}
	return 0;
}

static int close_device(int servicefd, char *obj)
{
	int r = 0;
	union {
		struct bt_close_req close_req;
		struct bt_close_rsp close_rsp;
		bt_audio_error_t error;
		uint8_t buf[BT_SUGGESTED_BUFFER_SIZE];
	} msg;

	/* now close the device */
	memset(&msg, 0, sizeof(msg));

	msg.close_req.h.type = BT_REQUEST;
	msg.close_req.h.name = BT_CLOSE;
	msg.close_req.h.length = sizeof(msg.close_req);

	r = send_message(servicefd, &msg.close_req.h);
	if (r < 0) {
		return r;
	}

	r = read_message(servicefd, &msg.close_rsp.h, sizeof(msg));
	if (r < 0) {
		return r;
	}
	if (msg.close_rsp.h.type == BT_ERROR) {
		fprintf(stderr, "Failed to close device %s\n", obj);
		return -1;
	}
	printf("Closed device %s\n", obj);
	return 0;
}

static int start_stream(int servicefd, char *obj)
{
	int pcmsock = -1;
	int f = 0;
	int r = 0;
#if 0
	int priority = 0;
	int one = 0;
#endif
	union {
		bt_audio_msg_header_t rsp;
		struct bt_start_stream_req start_req;
		struct bt_start_stream_rsp start_rsp;
		struct bt_new_stream_ind stream_ind;
		bt_audio_error_t error;
		uint8_t buf[BT_SUGGESTED_BUFFER_SIZE];
	} msg;

	/* start streaming! */
	memset(&msg, 0, sizeof(msg));

	msg.start_req.h.type = BT_REQUEST;
	msg.start_req.h.name = BT_START_STREAM;
	msg.start_req.h.length = sizeof(msg.start_req);

	r = send_message(servicefd, &msg.start_req.h);
	if (r < 0) {
		return r;
	}

	r = read_message(servicefd, &msg.start_rsp.h, sizeof(msg));	
	if (r < 0) {
		return r;
	}
	if (msg.start_rsp.h.type == BT_ERROR) {
		fprintf(stderr, "Failed to start streaming from device %s\n", obj);
		return -1;
	}

	r = read_message(servicefd, &msg.stream_ind.h, sizeof(msg));
	if (r < 0) {
		return r;
	}
	if (msg.stream_ind.h.type != BT_INDICATION 
	    || msg.stream_ind.h.name != BT_NEW_STREAM) {
		fprintf(stderr, "Stream indication error on device %s\n", obj);
		return -1;
	}

	pcmsock = bt_audio_service_get_data_fd(servicefd);
	if (pcmsock < 0) {
		fprintf(stderr, "Failed to retrieve pcm socket for device %s\n", obj);
		return pcmsock;
	}
	f = fcntl(pcmsock, F_GETFL);
	if (!(f & O_NONBLOCK)) {
		fcntl(pcmsock, F_SETFL, f | O_NONBLOCK);
	}
#if 0
	priority = 6;
	setsockopt(pcmsock, SOL_SOCKET, SO_PRIORITY, (void*)&priority, sizeof(priority));
	timestamp is used with recvmsg to get a timestamp of when the datagram was received
	one = 1;
	setsockopt(pcmsock, SOL_SOCKET, SO_TIMESTAMP, &one, sizeof(one));
#endif
	printf("Got pcm socket %d!\n", pcmsock);
	return pcmsock;
}

static int stop_stream(int servicefd, char *obj)
{
	int r = 0;
	union {
		bt_audio_msg_header_t rsp;
		struct bt_start_stream_req start_req;
		struct bt_start_stream_rsp start_rsp;
		bt_audio_error_t error;
		uint8_t buf[BT_SUGGESTED_BUFFER_SIZE];
	} msg;

	/* start streaming! */
	memset(&msg, 0, sizeof(msg));

	msg.start_req.h.type = BT_REQUEST;
	msg.start_req.h.name = BT_STOP_STREAM;
	msg.start_req.h.length = sizeof(msg.start_req);

	r = send_message(servicefd, &msg.start_req.h);
	if (r < 0) {
		return r;
	}

	r = read_message(servicefd, &msg.start_rsp.h, sizeof(msg));	
	if (r < 0) {
		return r;
	}
	if (msg.start_rsp.h.type == BT_ERROR) {
		fprintf(stderr, "Failed to start streaming from device %s\n", obj);
		return -1;
	}
	printf("Stopped streaming from device %s\n", obj);
	return 0;
}

/*static const char digital_milliwatt[] = {0x1e,0x0b,0x0b,0x1e,0x9e,0x8b,0x8b,0x9e} ;*/
void run_sco_service(char *devname)
{
	union {
		bt_audio_msg_header_t h;
		bt_audio_error_t error;
		uint8_t buf[BT_SUGGESTED_BUFFER_SIZE];
	} msg;
	uint8_t pcmbuf[SCO_PCM_MTU];
	struct pollfd svcpoll[2];
	int rc = 0;
	int audiostopped = 0;
	int pcmsock = 0;
	int svcsock = globals.audio_service_fd;

	if (print_caps(svcsock, devname)) {
		switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR, "Could not get capabilities for device %s\n", devname);
		return;
	}

	if (open_device(svcsock, devname)) {
		switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR, "Could not open audio service for device %s\n", devname);
		return;
	}

	if ((pcmsock = start_stream(svcsock, devname)) < 0) {
		switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR, "Could not start audio service for device %s\n", devname);
		return;
	}

	switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG, "Audio socket is %d\n", pcmsock);

	svcpoll[0].fd = svcsock;
	svcpoll[0].events = POLLIN | POLLERR;
	svcpoll[1].fd = pcmsock;
	svcpoll[1].events = POLLIN | POLLERR;
	audiostopped = 0;
	for ( ; ; ) {

		svcpoll[0].revents = 0;
		svcpoll[1].revents = 0;

		rc = poll(svcpoll, 2, -1);
		if (rc < 0) {
			if (errno == EINTR) {
				break;
			}
			switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR, "Error polling audio connection: %s\n", strerror(errno));
			break;
		}

		if ((svcpoll[0].revents & POLLERR)) {
			switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG, "POLLERR in service connection\n");
			break;
		}

		if ((svcpoll[1].revents & POLLERR)) {
			audiostopped = 1;
			close(pcmsock);
			pcmsock = -1;
			break;
		}

		if ((svcpoll[0].revents & POLLIN)) {
			rc = read_message(svcsock, &msg.h, sizeof(msg));
			if (rc < 0) {
				switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR, "Failed to read bluez audio service message\n");
				break;
			}
		}

		if ((svcpoll[1].revents & POLLIN)) {
			rc = read(pcmsock, pcmbuf, sizeof(pcmbuf));
			if (rc < 0) {
				switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR, "Error reading from audio connection: %s\n", strerror(errno));
				break;
			}
			if (rc != sizeof(pcmbuf)) {
				switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR, "Short read from audio connection (%d bytes)\n", rc);
			}
			rc = write(pcmsock, pcmbuf, rc);
			if (rc < 0) {
				switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR, "Error writing to audio connection: %s\n", strerror(errno));
				break;
			}
			if (rc != sizeof(pcmbuf)) {
				switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR, "Short write to audio connection (%d bytes)\n", rc);
			}
		}
	}

	stop_stream(svcsock, devname);

}


/* 
   State methods they get called when the state changes to the specific state 
   returning SWITCH_STATUS_SUCCESS tells the core to execute the standard state method next
   so if you fully implement the state you can return SWITCH_STATUS_FALSE to skip it.
*/
static switch_status_t channel_on_init(switch_core_session_t *session)
{
	switch_channel_t *channel;
	ofono_modem_t *modem = NULL;

	modem = switch_core_session_get_private(session);
	switch_assert(modem != NULL);

	channel = switch_core_session_get_channel(session);
	switch_assert(channel != NULL);

	switch_set_flag_locked(modem, TFLAG_IO);

	/* Move channel's state machine to ROUTING. This means the call is trying
	   to get from the initial start where the call because, to the point
	   where a destination has been identified. If the channel is simply
	   left in the initial state, nothing will happen. */
	switch_channel_set_state(channel, CS_ROUTING);
	switch_mutex_lock(globals.mutex);
	globals.calls++;
	switch_mutex_unlock(globals.mutex);

	return SWITCH_STATUS_SUCCESS;
}

static switch_status_t channel_on_routing(switch_core_session_t *session)
{
	switch_channel_t *channel = NULL;
	ofono_modem_t *modem = NULL;

	channel = switch_core_session_get_channel(session);
	switch_assert(channel != NULL);

	modem = switch_core_session_get_private(session);
	switch_assert(modem != NULL);

	switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_DEBUG, "%s CHANNEL ROUTING\n", switch_channel_get_name(channel));

	return SWITCH_STATUS_SUCCESS;
}

static switch_status_t channel_on_execute(switch_core_session_t *session)
{

	switch_channel_t *channel = NULL;
	ofono_modem_t *modem = NULL;

	channel = switch_core_session_get_channel(session);
	switch_assert(channel != NULL);

	modem = switch_core_session_get_private(session);
	switch_assert(modem != NULL);

	switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_DEBUG, "%s CHANNEL EXECUTE\n", switch_channel_get_name(channel));

	return SWITCH_STATUS_SUCCESS;
}

static switch_status_t channel_on_destroy(switch_core_session_t *session)
{
	switch_channel_t *channel = NULL;
	ofono_modem_t *modem = NULL;

	channel = switch_core_session_get_channel(session);
	switch_assert(channel != NULL);

	modem = switch_core_session_get_private(session);

	if (modem) {
		if (switch_core_codec_ready(&modem->read_codec)) {
			switch_core_codec_destroy(&modem->read_codec);
		}

		if (switch_core_codec_ready(&modem->write_codec)) {
			switch_core_codec_destroy(&modem->write_codec);
		}
	}
	return SWITCH_STATUS_SUCCESS;
}


static switch_status_t channel_on_hangup(switch_core_session_t *session)
{
	switch_channel_t *channel = NULL;
	ofono_modem_t *modem = NULL;

	channel = switch_core_session_get_channel(session);
	switch_assert(channel != NULL);

	modem = switch_core_session_get_private(session);
	switch_assert(modem != NULL);

	switch_clear_flag_locked(modem, TFLAG_IO);
	switch_clear_flag_locked(modem, TFLAG_VOICE);


	switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_DEBUG, "%s CHANNEL HANGUP\n", switch_channel_get_name(channel));
	switch_mutex_lock(globals.mutex);
	globals.calls--;
	if (globals.calls < 0) {
		globals.calls = 0;
	}
	switch_mutex_unlock(globals.mutex);

	return SWITCH_STATUS_SUCCESS;
}

static switch_status_t channel_kill_channel(switch_core_session_t *session, int sig)
{
	switch_channel_t *channel = NULL;
	ofono_modem_t *modem = NULL;

	channel = switch_core_session_get_channel(session);
	switch_assert(channel != NULL);

	modem = switch_core_session_get_private(session);
	switch_assert(modem != NULL);

	switch (sig) {
	case SWITCH_SIG_KILL:
		break;
	case SWITCH_SIG_BREAK:
		break;
	default:
		break;
	}

	switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_DEBUG, "CHANNEL KILL %d\n", sig);
	return SWITCH_STATUS_SUCCESS;
}

static switch_status_t channel_on_exchange_media(switch_core_session_t *session)
{
	switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_DEBUG, "CHANNEL LOOPBACK\n");
	return SWITCH_STATUS_SUCCESS;
}

static switch_status_t channel_on_soft_execute(switch_core_session_t *session)
{
	switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_DEBUG, "CHANNEL TRANSMIT\n");
	return SWITCH_STATUS_SUCCESS;
}

static switch_status_t channel_send_dtmf(switch_core_session_t *session, const switch_dtmf_t *dtmf)
{
	ofono_modem_t *modem = switch_core_session_get_private(session);
	switch_assert(modem != NULL);

	return SWITCH_STATUS_SUCCESS;
}

static switch_status_t channel_read_frame(switch_core_session_t *session, switch_frame_t **frame, switch_io_flag_t flags, int stream_id)
{
	switch_channel_t *channel = NULL;
	ofono_modem_t *modem = NULL;
	switch_byte_t *data;

	channel = switch_core_session_get_channel(session);
	switch_assert(channel != NULL);

	modem = switch_core_session_get_private(session);
	switch_assert(modem != NULL);
	modem->read_frame.flags = SFF_NONE;
	*frame = NULL;

	modem->readcnt++;
	while (switch_test_flag(modem, TFLAG_IO)) {

		if (switch_test_flag(modem, TFLAG_BREAK)) {
			switch_clear_flag(modem, TFLAG_BREAK);
			goto cng;
		}

		if (!switch_test_flag(modem, TFLAG_IO)) {
			return SWITCH_STATUS_FALSE;
		}

		if (switch_test_flag(modem, TFLAG_IO) && switch_test_flag(modem, TFLAG_VOICE)) {
			switch_clear_flag_locked(modem, TFLAG_VOICE);
			if (!modem->read_frame.datalen) {
				continue;
			}
			*frame = &modem->read_frame;
			return SWITCH_STATUS_SUCCESS;
		}
		switch_cond_next();
	}

	return SWITCH_STATUS_FALSE;

  cng:
	data = (switch_byte_t *) modem->read_frame.data;
	data[0] = 65;
	data[1] = 0;
	modem->read_frame.datalen = 2;
	modem->read_frame.flags = SFF_CNG;
	*frame = &modem->read_frame;

	return SWITCH_STATUS_SUCCESS;

}

static switch_status_t channel_write_frame(switch_core_session_t *session, switch_frame_t *frame, switch_io_flag_t flags, int stream_id)
{
	switch_channel_t *channel = NULL;
	ofono_modem_t *modem = NULL;

	channel = switch_core_session_get_channel(session);
	switch_assert(channel != NULL);

	modem = switch_core_session_get_private(session);
	switch_assert(modem != NULL);

	if (!switch_test_flag(modem, TFLAG_IO)) {
		return SWITCH_STATUS_FALSE;
	}
#if SWITCH_BYTE_ORDER == __BIG_ENDIAN
	if (switch_test_flag(modem, TFLAG_LINEAR)) {
		switch_swap_linear(frame->data, (int) frame->datalen / 2);
	}
#endif
	modem->writecnt++;
	return SWITCH_STATUS_SUCCESS;
}

static switch_status_t channel_receive_message(switch_core_session_t *session, switch_core_session_message_t *msg)
{
	switch_channel_t *channel;
	ofono_modem_t *modem;

	channel = switch_core_session_get_channel(session);
	switch_assert(channel != NULL);

	modem = switch_core_session_get_private(session);
	switch_assert(modem != NULL);

	switch (msg->message_id) {
	case SWITCH_MESSAGE_INDICATE_ANSWER:
		{
			switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_NOTICE, "Answering modem %s\n", modem->name);
		}
		break;
	default:
		break;
	}

	return SWITCH_STATUS_SUCCESS;
}

static switch_status_t modem_init(ofono_modem_t *modem, switch_core_session_t *session)
{
	switch_status_t status;

	switch_mutex_lock(modem->mutex);

	if (modem->session) {
		switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_CRIT, "Modem %s already has a session!\n", modem->name);
		goto err;
	}


	/* initialize the codec */
	status = switch_core_codec_init(&modem->read_codec, "L16", NULL, 
			MODEM_RATE, MODEM_INTERVAL, 
			1, SWITCH_CODEC_FLAG_ENCODE | SWITCH_CODEC_FLAG_DECODE,
			NULL, switch_core_session_get_private(session));
	if (status != SWITCH_STATUS_SUCCESS) {
		switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_CRIT, "Modem %s failed to initialize its read codec!\n", modem->name);
		goto err;
	}

	status = switch_core_codec_init(&modem->write_codec, "L16", NULL, 
			MODEM_RATE, MODEM_INTERVAL, 
			1, SWITCH_CODEC_FLAG_ENCODE | SWITCH_CODEC_FLAG_DECODE,
			NULL, switch_core_session_get_private(session));
	if (status != SWITCH_STATUS_SUCCESS) {
		switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_CRIT, "Modem %s failed to initialize its write codec!\n", modem->name);
		switch_core_codec_destroy(&modem->read_codec);
		goto err;
	}
	switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG, "Set codec L16 %dHz %dms\n", MODEM_RATE, MODEM_INTERVAL);
	switch_core_session_set_read_codec(session, &modem->read_codec);
	switch_core_session_set_write_codec(session, &modem->write_codec);

	/* setup the read frame */
	modem->read_frame.codec = &modem->read_codec;
	modem->read_frame.data = modem->databuf;
	modem->read_frame.buflen = sizeof(modem->databuf);

	/* associate modem to session and viceversa */
	modem->session = session;
	switch_core_session_set_private(session, modem);

	switch_mutex_unlock(modem->mutex);
	return SWITCH_STATUS_SUCCESS;

err:
	switch_mutex_unlock(modem->mutex);
	return SWITCH_STATUS_GENERR;

}


/* Make sure when you have 2 sessions in the same scope that you pass the appropriate one to the routines
   that allocate memory or you will have 1 channel with memory allocated from another channel's pool!
*/
static switch_call_cause_t channel_outgoing_channel(switch_core_session_t *session, switch_event_t *var_event,
						switch_caller_profile_t *outbound_profile,
						switch_core_session_t **new_session, switch_memory_pool_t **pool, switch_originate_flag_t flags,
						switch_call_cause_t *cancel_cause)
{
	if ((*new_session = switch_core_session_request(handsfree_endpoint_interface, SWITCH_CALL_DIRECTION_OUTBOUND, flags, pool)) != 0) {
		ofono_modem_t *modem;
		switch_channel_t *channel;
		switch_caller_profile_t *caller_profile;

		switch_core_session_add_stream(*new_session, NULL);
		channel = switch_core_session_get_channel(*new_session);
		modem_init(modem, *new_session);

		if (outbound_profile) {
			char name[128];

			snprintf(name, sizeof(name), "handsfree/%s", outbound_profile->destination_number);
			switch_channel_set_name(channel, name);

			caller_profile = switch_caller_profile_clone(*new_session, outbound_profile);
			switch_channel_set_caller_profile(channel, caller_profile);
			modem->caller_profile = caller_profile;
		} else {
			switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(*new_session), SWITCH_LOG_ERROR, "Doh! no caller profile\n");
			switch_core_session_destroy(new_session);
			return SWITCH_CAUSE_DESTINATION_OUT_OF_ORDER;
		}


		switch_set_flag_locked(modem, TFLAG_OUTBOUND);
		switch_channel_set_state(channel, CS_INIT);
		return SWITCH_CAUSE_SUCCESS;
	}

	return SWITCH_CAUSE_DESTINATION_OUT_OF_ORDER;

}

static switch_status_t channel_receive_event(switch_core_session_t *session, switch_event_t *event)
{
	struct ofono_modem_t *modem = switch_core_session_get_private(session);
	char *body = switch_event_get_body(event);

	switch_assert(modem != NULL);

	if (!body) {
		body = "";
	}

	return SWITCH_STATUS_SUCCESS;
}



switch_state_handler_table_t handsfree_state_handlers = {
	/*.on_init */ channel_on_init,
	/*.on_routing */ channel_on_routing,
	/*.on_execute */ channel_on_execute,
	/*.on_hangup */ channel_on_hangup,
	/*.on_exchange_media */ channel_on_exchange_media,
	/*.on_soft_execute */ channel_on_soft_execute,
	/*.on_consume_media */ NULL,
	/*.on_hibernate */ NULL,
	/*.on_reset */ NULL,
	/*.on_park */ NULL,
	/*.on_reporting */ NULL,
	/*.on_destroy */ channel_on_destroy
};

switch_io_routines_t handsfree_io_routines = {
	/*.outgoing_channel */ channel_outgoing_channel,
	/*.read_frame */ channel_read_frame,
	/*.write_frame */ channel_write_frame,
	/*.kill_channel */ channel_kill_channel,
	/*.send_dtmf */ channel_send_dtmf,
	/*.receive_message */ channel_receive_message,
	/*.receive_event */ channel_receive_event
};

static switch_status_t load_config(void)
{
	char *cf = "handsfree.conf";
	int modem_i = 0;
	switch_xml_t cfg, xml, settings, param, modems, mymodem;

	if (!(xml = switch_xml_open_cfg(cf, &cfg, NULL))) {
		switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR, "Open of %s failed\n", cf);
		return SWITCH_STATUS_TERM;
	}

	if ((settings = switch_xml_child(cfg, "settings"))) {
		for (param = switch_xml_child(settings, "param"); param; param = param->next) {
			char *var = (char *) switch_xml_attr_soft(param, "name");
			char *val = (char *) switch_xml_attr_soft(param, "value");
			if (!strcasecmp(var, "dialplan")) {
				strcpy(globals.dialplan, val);
			}
			else if (!strcasecmp(var, "context")) {
				strcpy(globals.context, val);
			}
		}
	}

	if ((modems = switch_xml_child(cfg, "modems"))) {
		for (mymodem = switch_xml_child(modems, "modem"); mymodem; mymodem = mymodem->next) {
			ofono_modem_t *modem = &globals.modems_array[modem_i];
			char *modem_id = (char *)switch_xml_attr_soft(mymodem, "id");

			if (!modem_id) {
				switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_WARNING, "Ignoring modem entry without id attribute\n");
				continue;
			}

			switch_core_hash_insert(globals.modems, modem_id, modem);

			switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG, "Added modem '%s'\n", modem_id);

			switch_mutex_init(&modem->mutex, SWITCH_MUTEX_NESTED, globals.module_pool);
			snprintf(modem->id, sizeof(modem->id), "%s", modem_id);
			snprintf(modem->name, sizeof(modem->name), "%s", modem_id);
			snprintf(modem->dialplan, sizeof(modem->dialplan), "%s", "XML");
			snprintf(modem->context, sizeof(modem->context), "%s", "public");

			for (param = switch_xml_child(settings, "param"); param; param = param->next) {
				char *var = (char *) switch_xml_attr_soft(param, "name");
				char *val = (char *) switch_xml_attr_soft(param, "value");
				if (!strcasecmp(var, "dialplan")) {
					snprintf(modem->dialplan, sizeof(modem->dialplan), "%s", val);
				}
				else if (!strcasecmp(var, "context")) {
					snprintf(modem->context, sizeof(modem->context), "%s", val);
				} 
				else if (!strcasecmp(var, "name")) {
					snprintf(modem->name, sizeof(modem->name), "%s", modem_id);
				}
			}

			modem_i++;
			if (modem_i == switch_arraylen(globals.modems_array)) {
				switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR, "Maximum number of modems reached, "
						"not processing more modem entries in configuration!\n");
				break;
			}
		}
	}

	switch_xml_free(xml);

	return SWITCH_STATUS_SUCCESS;
}

static void execute_script(switch_stream_handle_t *stream, char *script)
{
	char cmdpath[1024];
	char linebuf[2048];
	struct pollfd fdset;
	int rc = 0;
	int tofs[2];
	int fromfs[2];
	int pid;
	int fd;
	char *argv[] = { script, NULL };

	snprintf(cmdpath, sizeof(cmdpath), "%s%s%s", SWITCH_GLOBAL_dirs.script_dir, SWITCH_PATH_SEPARATOR, script);

	if (pipe(tofs)) {
		switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR, "Failed to create pipe to monitor ofono events\n");
		return;
	}

	if (pipe(tofs)) {
		close(tofs[0]);
		close(tofs[1]);
		switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR, "Failed to create pipe to monitor ofono events\n");
		return;
	}

	if (pipe(fromfs)) {
		close(tofs[0]);
		close(tofs[1]);
		close(fromfs[0]);
		close(fromfs[1]);
		switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR, "Failed to create pipe to monitor ofono events\n");
		return;
	}

	switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG, "Launching command %s\n", cmdpath);

	pid = fork();
	if (pid < 0) {
		switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR, "Failed to fork to execute script %s\n", cmdpath);
		close(tofs[0]);
		close(tofs[1]);
		close(fromfs[0]);
		close(fromfs[1]);
		return;
	}

	if (!pid) {
		dup2(fromfs[0], STDIN_FILENO);
		dup2(tofs[1], STDOUT_FILENO);
		for (fd = STDERR_FILENO + 1; fd < (2^65536); fd++) {
			close(fd);
		}
		execv(cmdpath, argv);
		exit(0);
	}

	fdset.fd = tofs[0];
	fdset.events = POLLIN | POLLERR;
	while (globals.running) {
		rc = waitpid(pid, NULL, WNOHANG);
		if (rc < 0) {
			if (errno == EINTR) {
				continue;
			}
			switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR, "Failed to wait for script %s: %s\n", script, strerror(errno));
			break;
		}
		if (rc > 0) {
			switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG, "script %s terminated\n", script);
			break;
		}
		switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG, "script %s running\n", script);
		rc = poll(&fdset, 1, 100);
		if (rc < 0) {
			switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR, "Failed to poll output of script %s: %s\n", script, strerror(errno));
			break;
		}
		if (!rc) {
			continue;
		}
		if (rc > 0) {
			if (POLLIN & fdset.revents) {
				rc = read(tofs[0], linebuf, sizeof(linebuf)-1);
				if (rc < 0) {
					switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR, "Failed to read script output: %s\n", strerror(errno));
					break;
				}
				if (rc == 0) {
					switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR, "No script output?\n");
					break;
				}
				linebuf[rc] = 0;
				stream->write_function(stream, "%s", linebuf);
			} else if (POLLERR & fdset.revents) {
				switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR, "Poll error when waiting for script output\n");
				break;
			} else {
				switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR, "Unknown flags when waiting script output: %d\n", fdset.revents);
				break;
			}
		}
	}
	close(tofs[0]);
	close(tofs[1]);
	close(fromfs[0]);
	close(fromfs[1]);
	if (!globals.running) {
		kill(pid, SIGTERM);
		waitpid(pid, NULL, 0);
	}
}

#define LIST_MODEMS_SCRIPT "list-modems"
#define MONITOR_SCRIPT "monitor-ofono"
SWITCH_STANDARD_API(handsf_function)
{
	char *argv[10];
	char *mydata;
	int argc;

	if (zstr(cmd)) {
		stream->write_function(stream, "-ERR Parameter missing");
		return SWITCH_STATUS_SUCCESS;
	}

	if (!(mydata = strdup(cmd))) {
		return SWITCH_STATUS_FALSE;
	}

	if (!(argc = switch_separate_string(mydata, ' ', argv, (sizeof(argv)/sizeof(argv[0])))) || !argv[0]) {
		goto end;
	}

	if (!strcasecmp(argv[0], "list")) {
		execute_script(stream, LIST_MODEMS_SCRIPT);	
	} else {
		stream->write_function(stream, "-ERR Invalid parameter");
	}

end:
	switch_safe_free(mydata);

	return SWITCH_STATUS_SUCCESS;
}

static void load_modems(void)
{
}

static char *skip_sender(const char *event)
{
	char *str = strchr(event, '}');
	if (!str) {
		return NULL;
	}
	str++;
	if (!*str) {
		return NULL;
	}
	str++;
	if (!*str) {
		return NULL;
	}
	return str;
}

/* searching for pattern ..../hfp/<modem-name>/.... */
static char *get_modem_name_from_line(const char *line, char *modem_name, int modem_name_len)
{
	long len = 0;
	const char *end = NULL;
	const char *str = line;
	if (!line) {
		return NULL;
	}
	while (strlen(str) > 5) {
		if (*str == '/' 
	         && *(str + 1) == 'h' 
		 && *(str + 2) == 'f' 
		 && *(str + 3) == 'p'
		 && *(str + 4) == '/') {
			str += 5;
			end = strchr(str, '/');
			if (!end || end == str) {
				return NULL;
			}
			len = (long)end - (long)str;
			if (len >= modem_name_len) {
				return NULL;
			}
			memcpy(modem_name, str, len);
			modem_name[len] = 0;
			modem_name[modem_name_len-1] = 0;
			return modem_name;
		}
		str++;
	}
	return NULL;
}

static void handle_incoming_call(const char *modem_name)
{
	switch_channel_t *channel = NULL;
	switch_core_session_t *session = NULL;
	ofono_modem_t *modem = NULL;
	char chan_name[128];

	modem = switch_core_hash_find(globals.modems, modem_name);
	if (!modem) {
		switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR, "Incoming call in unknown modem '%s'\n", modem_name);
		return;
	}
	switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_NOTICE, "Incoming call in modem %s\n", modem_name);

	/* notify FreeSWITCH about the incoming call */
	if (!(session = switch_core_session_request(handsfree_endpoint_interface, SWITCH_CALL_DIRECTION_INBOUND, SOF_NONE, NULL))) {
		switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_CRIT, "Session init Error!\n");
		return;
	}

	switch_core_session_add_stream(session, NULL);

	channel = switch_core_session_get_channel(session);
	if (modem_init(modem, session) != SWITCH_STATUS_SUCCESS) {
		switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_CRIT, "Failed to initialize modem!\n");
		switch_core_session_destroy(&session);
		return;
	}
	modem->caller_profile = switch_caller_profile_new(switch_core_session_get_pool(session),
							"HandsFree",
							"XML",
							"Moises",
							"1234",
							NULL,
							"5678",
							"5678",
							"1234",
							(char *)modname,
							"default",
							"1234");
	switch_assert(modem->caller_profile != NULL);


	snprintf(chan_name, sizeof(chan_name), "HandsFree/%s/%s", modem->name, modem->caller_profile->destination_number);
	switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG, "Connect inbound channel %s\n", chan_name);
	switch_channel_set_name(channel, chan_name);
	switch_channel_set_caller_profile(channel, modem->caller_profile);
#if 0
	switch_channel_set_variable(channel, "handsfree_network", "blah");
#endif

	switch_channel_set_state(channel, CS_INIT);

	/* bring the session alive! */
	if (switch_core_session_thread_launch(session) != SWITCH_STATUS_SUCCESS) {
		switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_CRIT, "Error spawning session thread!\n");
		switch_core_session_destroy(&session);
		return;
	}
}

#define VCM_NEW_CALL_EVENT "CallAdded"
#define VCM_HANGUP_CALL_EVENT "CallRemoved"
// Bluez device syntax: /org/bluez/26745/hci0/dev_00_1D_28_4B_9A_A8
// where does 26745 comes from? seems like a PID, where to get it from?
// hci0 is the adapter, this needs to come from configuration
// {VoiceCallManager} [CallAdded] /hfp/00158315A310_001D284B9AA8/voicecall01 { State = incoming, LineIdentification = +16478353016, Multiparty = False }
static void handle_call_manager_event(const char *event)
{
	char *event_str = NULL;
	char modem_name[MODEM_NAME_LEN];
	char *modem_str = NULL;

	event_str = skip_sender(event);
	if (!event_str) {
		return;
	}

	/* handle the actual event for VoiceCallManager */
	if (strstr(event_str, VCM_NEW_CALL_EVENT)) {
		modem_str = get_modem_name_from_line(event, modem_name, sizeof(modem_name));
		if (!modem_str) {
			switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR, "Failed to retrieve modem name from incoming call event %s\n", event);
			return;
		}
		handle_incoming_call(modem_name);
	} else if (strstr(event_str, VCM_HANGUP_CALL_EVENT)) {
		modem_str = get_modem_name_from_line(event, modem_name, sizeof(modem_name));
		if (!modem_str) {
			switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR, "Failed to retrieve modem name from hangup call event %s\n", event);
			return;
		}
		switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_NOTICE, "Hangup call in modem %s\n", modem_name);
	} else {
		switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_WARNING, "Ignored VoiceCallManager event: %s\n", event_str);
	}
}

static void handle_call_event(const char *event)
{
}

static void handle_modem_event(const char *event)
{
}

static void handle_call_vol_event(const char *event)
{
}

static void handle_network_reg_event(const char *event)
{
}

static void process_event(const char *event)
{
	switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG, "%s\n", event);
	if (!strncasecmp(EVENT_SRC_VOICE_CALL_MANAGER, event, sizeof(EVENT_SRC_VOICE_CALL_MANAGER)-1)) {
		handle_call_manager_event(event);
	} else if (!strncasecmp(EVENT_SRC_VOICE_CALL, event, sizeof(EVENT_SRC_VOICE_CALL)-1)) {
		handle_call_event(event);
	} else if (!strncasecmp(EVENT_SRC_MODEM, event, sizeof(EVENT_SRC_MODEM)-1)) {
		handle_modem_event(event);
	} else if (!strncasecmp(EVENT_SRC_CALL_VOL, event, sizeof(EVENT_SRC_CALL_VOL)-1)) {
		handle_call_vol_event(event);
	} else if (!strncasecmp(EVENT_SRC_NETWORK_REG, event, sizeof(EVENT_SRC_NETWORK_REG)-1)) {
		handle_network_reg_event(event);
	} else {
		switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_WARNING, "Ignored unknown event: %s\n", event);
	}
}

SWITCH_MODULE_RUNTIME_FUNCTION(mod_handsfree_runtime)
{
	int tofs[2];
	int fromfs[2];
	int pid;
	int fd;
	char *c;
	char *event;
	char *argv[] = { MONITOR_SCRIPT, NULL };
	char linebuf[2048];
	char cmdpath[1024];
	struct pollfd fdset;
	int rc = 0;

	snprintf(cmdpath, sizeof(cmdpath), "%s%s%s", SWITCH_GLOBAL_dirs.script_dir, SWITCH_PATH_SEPARATOR, MONITOR_SCRIPT);

	if (pipe(tofs)) {
		switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR, "Failed to create pipe to monitor ofono events\n");
		return SWITCH_STATUS_TERM;
	}

	if (pipe(fromfs)) {
		close(tofs[0]);
		close(tofs[1]);
		switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR, "Failed to create pipe to monitor ofono events\n");
		return SWITCH_STATUS_TERM;
	}

	switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG, "Launching command %s\n", cmdpath);

	pid = fork();
	if (pid < 0) {
		close(tofs[0]);
		close(tofs[1]);
		close(fromfs[0]);
		close(fromfs[1]);
		switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR, "Failed to fork to monitor ofono events\n");
		return SWITCH_STATUS_TERM;
	}

	if (!pid) {
		dup2(fromfs[0], STDIN_FILENO);
		dup2(tofs[1], STDOUT_FILENO);
		for (fd = STDERR_FILENO + 1; fd < (2^65536); fd++) {
			close(fd);
		}
		execv(cmdpath, argv);
		exit(0);
	}

	globals.thread_running = 1;
	fdset.fd = tofs[0];
	fdset.events = POLLIN | POLLERR;
	while (globals.running) {
		rc = poll(&fdset, 1, 100);
		if (rc < 0) {
			switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR, "Failed to poll ofono events: %s\n", strerror(errno));
			break;
		}
		if (!rc) {
			continue;
		}
		if (rc > 0) {
			if (POLLIN & fdset.revents) {
				rc = read(tofs[0], linebuf, sizeof(linebuf)-1);
				if (rc < 0) {
					switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR, "Failed to read ofono events: %s\n", strerror(errno));
					break;
				}
				if (rc == 0) {
					switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR, "No ofono events\n");
					break;
				}
				linebuf[rc] = 0;
				event = linebuf;
				do {
					c = strchr(event, '\n');
					if (c) {
						*c = '\0';
					}
					process_event(event);
					if (c) {
						event = c;
						event++;
					} else {
						event = NULL;
					}
				} while (event && *event);
			} else if (POLLERR & fdset.revents) {
				switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR, "Poll error when waiting ofono events\n");
				break;
			} else {
				switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR, "Unknown flags when waiting ofono events: %d\n", fdset.revents);
				break;
			}
		}
	}

	close(tofs[0]);
	close(tofs[1]);
	close(fromfs[0]);
	close(fromfs[1]);

	kill(pid, SIGTERM);
	waitpid(pid, NULL, 0);

	globals.thread_running = 0;
	return SWITCH_STATUS_TERM;
}

SWITCH_MODULE_SHUTDOWN_FUNCTION(mod_handsfree_shutdown)
{
	/* stop threads  */
	globals.running = 0;

	/* Wait monitor thread to exit */
	while (globals.thread_running) {
		switch_yield(100000);
	}

	/* close all audio connections to the devices */
	close_device(globals.audio_service_fd, NULL);

	bt_audio_service_close(globals.audio_service_fd);

	switch_core_hash_destroy(&globals.modems);

	switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG, "Handsfree Done!\n");
	return SWITCH_STATUS_SUCCESS;
}

#define HANDS_FREE_SYNTAX "handsfree list"
SWITCH_MODULE_LOAD_FUNCTION(mod_handsfree_load)
{
	switch_api_interface_t *commands_api_interface;
	
	memset(&globals, 0, sizeof(globals));

	globals.module_pool = pool;

	switch_mutex_init(&globals.mutex, SWITCH_MUTEX_NESTED, globals.module_pool);

	switch_core_hash_init(&globals.modems, globals.module_pool);

	/* read config */
	load_config();

	/* update modems hash with real time information */
	load_modems();

	/* connect to the audio service*/
	switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG, "Openning connection to bluez audio service at %s\n", BT_IPC_SOCKET_NAME);

	globals.audio_service_fd = bt_audio_service_open();
	if (globals.audio_service_fd < 0) {
		switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR, "connection to bluez audio service failed\n");
		return SWITCH_STATUS_FALSE;
	}

	switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_NOTICE, "Connected to the bluez audio service successfully\n");

	*module_interface = switch_loadable_module_create_module_interface(pool, modname);
	handsfree_endpoint_interface = switch_loadable_module_create_interface(*module_interface, SWITCH_ENDPOINT_INTERFACE);
	handsfree_endpoint_interface->interface_name = "handsfree";
	handsfree_endpoint_interface->io_routines = &handsfree_io_routines;
	handsfree_endpoint_interface->state_handler = &handsfree_state_handlers;

	SWITCH_ADD_API(commands_api_interface, "handsfree", "Hands Free Endpoint Commands", handsf_function, HANDS_FREE_SYNTAX);

	switch_console_set_complete("add handsfree list");

	/* indicate that the module should continue to be loaded */
	globals.running = 1;
	return SWITCH_STATUS_SUCCESS;
}



/*
 *
 *  BlueZ - Bluetooth protocol stack for Linux
 *
 *  Copyright (C) 2004-2010  Marcel Holtmann <marcel@holtmann.org>
 *
 *  This library is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU Lesser General Public
 *  License as published by the Free Software Foundation; either
 *  version 2.1 of the License, or (at your option) any later version.
 *
 *  This library is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  Lesser General Public License for more details.
 *
 *  You should have received a copy of the GNU Lesser General Public
 *  License along with this library; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 *
 */

#define ARRAY_SIZE(arr) (sizeof(arr) / sizeof((arr)[0]))

/* This table contains the string representation for messages types */
static const char *strtypes[] = {
	"BT_REQUEST",
	"BT_RESPONSE",
	"BT_INDICATION",
	"BT_ERROR",
};

/* This table contains the string representation for messages names */
static const char *strnames[] = {
	"BT_GET_CAPABILITIES",
	"BT_OPEN",
	"BT_SET_CONFIGURATION",
	"BT_NEW_STREAM",
	"BT_START_STREAM",
	"BT_STOP_STREAM",
	"BT_CLOSE",
	"BT_CONTROL",
	"BT_DELAY_REPORT",
};

int bt_audio_service_open(void)
{
	int sk;
	int err;
	struct sockaddr_un addr = {
		AF_UNIX, BT_IPC_SOCKET_NAME
	};

	sk = socket(PF_LOCAL, SOCK_STREAM, 0);
	if (sk < 0) {
		err = errno;
		fprintf(stderr, "%s: Cannot open socket: %s (%d)\n",
			__FUNCTION__, strerror(err), err);
		errno = err;
		return -1;
	}

	if (connect(sk, (struct sockaddr *) &addr, sizeof(addr)) < 0) {
		err = errno;
		fprintf(stderr, "%s: connect() failed: %s (%d)\n",
			__FUNCTION__, strerror(err), err);
		close(sk);
		errno = err;
		return -1;
	}

	return sk;
}

int bt_audio_service_close(int sk)
{
	return close(sk);
}

int bt_audio_service_get_data_fd(int sk)
{
	char cmsg_b[CMSG_SPACE(sizeof(int))], m;
	int err, ret;
	struct iovec iov = { &m, sizeof(m) };
	struct msghdr msgh;
	struct cmsghdr *cmsg;

	memset(&msgh, 0, sizeof(msgh));
	msgh.msg_iov = &iov;
	msgh.msg_iovlen = 1;
	msgh.msg_control = &cmsg_b;
	msgh.msg_controllen = CMSG_LEN(sizeof(int));

	ret = recvmsg(sk, &msgh, 0);
	if (ret < 0) {
		err = errno;
		fprintf(stderr, "%s: Unable to receive fd: %s (%d)\n",
			__FUNCTION__, strerror(err), err);
		errno = err;
		return -1;
	}

	/* Receive auxiliary data in msgh */
	for (cmsg = CMSG_FIRSTHDR(&msgh); cmsg != NULL;
			cmsg = CMSG_NXTHDR(&msgh, cmsg)) {
		if (cmsg->cmsg_level == SOL_SOCKET
				&& cmsg->cmsg_type == SCM_RIGHTS) {
			memcpy(&ret, CMSG_DATA(cmsg), sizeof(int));
			return ret;
		}
	}

	errno = EINVAL;
	return -1;
}

const char *bt_audio_strtype(uint8_t type)
{
	if (type >= ARRAY_SIZE(strtypes))
		return NULL;

	return strtypes[type];
}

const char *bt_audio_strname(uint8_t name)
{
	if (name >= ARRAY_SIZE(strnames))
		return NULL;

	return strnames[name];
}

/* For Emacs:
 * Local Variables:
 * mode:c
 * indent-tabs-mode:t
 * tab-width:4
 * c-basic-offset:4
 * End:
 * For VIM:
 * vim:set softtabstop=4 shiftwidth=4 tabstop=4:
 */
