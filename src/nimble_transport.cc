/*
 *
 *  blepp - Implementation of the Generic ATTribute Protocol
 *
 *  Copyright (C) 2024
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 *
 */

#include <blepp/blepp_config.h>

#ifdef BLEPP_NIMBLE_SUPPORT

#include <blepp/nimble_transport.h>
#include <blepp/logging.h>

#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <signal.h>
#include <cerrno>
#include <cstring>
#include <stdexcept>
#include <mutex>

// ATBM ioctl base number
#define ATBM_IOCTL                  (121)

// ATBM ioctl command definitions (from ble_host/os/linux/atbm_os_api.c)
#define ATBM_AT_CMD_DIRECT          _IOW(ATBM_IOCTL, 0, unsigned int)
#define ATBM_BLE_SMART              _IOW(ATBM_IOCTL, 1, unsigned int)

// ATBM BLE ioctl commands (from ble_host/nimble_v42/nimble/transport/ioctl/ble_hci_hif.c)
#define ATBM_BLE_COEXIST_START      _IOW(ATBM_IOCTL, 0, unsigned int)
#define ATBM_BLE_COEXIST_STOP       _IOW(ATBM_IOCTL, 1, unsigned int)
#define ATBM_BLE_SET_ADV_DATA       _IOW(ATBM_IOCTL, 2, unsigned int)
#define ATBM_BLE_ADV_RESP_MODE_START _IOW(ATBM_IOCTL, 3, unsigned int)
#define ATBM_BLE_SET_RESP_DATA      _IOW(ATBM_IOCTL, 4, unsigned int)
#define ATBM_BLE_HIF_TXDATA         _IOW(ATBM_IOCTL, 5, unsigned int)

// HCI packet types
#define BLE_HCI_HIF_NONE            0x00
#define BLE_HCI_HIF_CMD             0x01
#define BLE_HCI_HIF_ACL             0x02
#define BLE_HCI_HIF_SCO             0x03
#define BLE_HCI_HIF_EVT             0x04
#define BLE_HCI_HIF_ISO             0x05

// WSM message types
#define HI_MSG_ID_BLE_BASE          0xC00
#define HI_MSG_ID_BLE_EVENT         (HI_MSG_ID_BLE_BASE + 0x01)
#define HI_MSG_ID_BLE_ACK           (HI_MSG_ID_BLE_BASE + 0x02)

// Buffer sizes
#define MAX_SYNC_EVENT_BUFFER_LEN   512
#define HCI_ACL_SHARE_SIZE          1538

namespace BLEPP
{

// Global pointer to NimbleTransport instance for signal handler
static NimbleTransport* g_nimble_transport_instance = nullptr;
static std::mutex g_signal_mutex;

// Signal handler for SIGIO
static void nimble_signal_handler(int sig_num)
{
	if (sig_num == SIGIO) {
		std::lock_guard<std::mutex> lock(g_signal_mutex);
		if (g_nimble_transport_instance) {
			g_nimble_transport_instance->signal_event();
		}
	}
}

NimbleTransport::NimbleTransport(const char* device_path)
	: device_path_(device_path)
	, ioctl_fd_(-1)
	, advertising_(false)
	, next_conn_handle_(1)
	, running_(false)
{
	ENTER();

	// Open Nimble device
	ioctl_fd_ = open(device_path_.c_str(), O_RDWR);
	if (ioctl_fd_ < 0) {
		throw std::runtime_error(std::string("Failed to open ") + device_path_ + ": " + strerror(errno));
	}

	// Set up async I/O
	fcntl(ioctl_fd_, F_SETOWN, getpid());
	unsigned long flags = fcntl(ioctl_fd_, F_GETFL);
	fcntl(ioctl_fd_, F_SETFL, flags | FASYNC);

	// Set close-on-exec flag
	flags = fcntl(ioctl_fd_, F_GETFD, 0);
	fcntl(ioctl_fd_, F_SETFD, flags | FD_CLOEXEC);

	// Register signal handler
	{
		std::lock_guard<std::mutex> lock(g_signal_mutex);
		g_nimble_transport_instance = this;
		signal(SIGIO, nimble_signal_handler);
	}

	// Initialize semaphores
	sem_init(&event_sem_, 0, 0);
	sem_init(&ioctl_sem_, 0, 1);

	// Start Nimble BLE coexistence mode
	uint8_t dummy_data[16] = {0};
	if (ioctl(ioctl_fd_, ATBM_BLE_COEXIST_START, (unsigned long)(&dummy_data)) < 0) {
		close(ioctl_fd_);
		throw std::runtime_error("Failed to start Nimble BLE coexistence mode");
	}

	// Start event loop thread
	running_ = true;
	event_thread_ = std::thread(&NimbleTransport::event_loop_thread, this);

	LOG(Info, "NimbleTransport initialized on " << device_path_);
}

NimbleTransport::~NimbleTransport()
{
	ENTER();
	cleanup();
}

void NimbleTransport::cleanup()
{
	if (running_) {
		// Stop event thread
		running_ = false;
		sem_post(&event_sem_);  // Wake up event thread

		if (event_thread_.joinable()) {
			event_thread_.join();
		}

		// Stop advertising if active
		if (advertising_) {
			stop_advertising();
		}

		// Stop Nimble BLE coexistence mode
		if (ioctl_fd_ >= 0) {
			uint8_t dummy_data[16] = {0};
			ioctl(ioctl_fd_, ATBM_BLE_COEXIST_STOP, (unsigned long)(&dummy_data));
		}

		// Cleanup signal handler
		{
			std::lock_guard<std::mutex> lock(g_signal_mutex);
			if (g_nimble_transport_instance == this) {
				g_nimble_transport_instance = nullptr;
				signal(SIGIO, SIG_DFL);
			}
		}

		// Close device
		if (ioctl_fd_ >= 0) {
			close(ioctl_fd_);
			ioctl_fd_ = -1;
		}

		// Destroy semaphores
		sem_destroy(&event_sem_);
		sem_destroy(&ioctl_sem_);

		LOG(Info, "NimbleTransport cleaned up");
	}
}

void NimbleTransport::signal_event()
{
	// Called from signal handler - post to semaphore to wake event thread
	sem_post(&event_sem_);
}

void NimbleTransport::event_loop_thread()
{
	LOG(Info, "Nimble event loop thread started");

	while (running_) {
		// Wait for SIGIO signal
		sem_wait(&event_sem_);

		if (!running_) {
			break;
		}

		// Read all pending events
		NimbleTransport::status_async event;
		do {
			ssize_t len = read(ioctl_fd_, &event, sizeof(event));

			if (len != sizeof(event)) {
				if (errno != EAGAIN && errno != EWOULDBLOCK) {
					LOG(Error, "Failed to read Nimble event: " << strerror(errno));
				}
				break;
			}

			// Process the event
			process_nimble_event(&event);

		} while (event.list_empty == 0);  // Continue if more events pending
	}

	LOG(Info, "Nimble event loop thread stopped");
}

void NimbleTransport::process_nimble_event(const struct status_async* event)
{
	// Extract WSM header
	const struct wsm_hdr* wsm = reinterpret_cast<const struct wsm_hdr*>(event->event_buffer);
	const uint8_t* hci_data = event->event_buffer + sizeof(struct wsm_hdr);

	LOG(Debug, "Nimble event: type=" << (int)event->type
	           << " driver_mode=" << (int)event->driver_mode
	           << " wsm_id=0x" << std::hex << wsm->id
	           << " wsm_len=" << std::dec << wsm->len);

	if (wsm->id == HI_MSG_ID_BLE_ACK || wsm->id == HI_MSG_ID_BLE_EVENT) {
		// Process HCI packet
		process_hci_event(hci_data, wsm->len);
	} else {
		LOG(Warning, "Unknown Nimble event ID: 0x" << std::hex << wsm->id);
	}
}

void NimbleTransport::process_hci_event(const uint8_t* data, size_t len)
{
	if (len < 2) {
		LOG(Error, "HCI event too short: " << len);
		return;
	}

	uint8_t hci_type = data[0];

	if (hci_type == BLE_HCI_HIF_EVT) {
		// HCI Event packet
		if (len < 3) {
			LOG(Error, "HCI event packet too short");
			return;
		}

		uint8_t event_code = data[1];
		uint8_t param_len = data[2];

		LOG(Debug, "HCI Event: code=0x" << std::hex << (int)event_code
		           << " param_len=" << std::dec << (int)param_len);

		// Parse specific HCI events
		switch (event_code) {
		case 0x03:  // HCI_Connection_Complete
			if (param_len >= 11) {
				uint8_t status = data[3];
				uint16_t conn_handle = (data[5] << 8) | data[4];

				if (status == 0 && on_connected) {
					// Extract peer address (6 bytes in reverse order for BLE)
					ConnectionParams conn_params;
					char addr_str[18];
					snprintf(addr_str, sizeof(addr_str), "%02X:%02X:%02X:%02X:%02X:%02X",
					         data[11], data[10], data[9], data[8], data[7], data[6]);
					conn_params.peer_address = addr_str;
					conn_params.conn_handle = conn_handle;
					on_connected(conn_params);
					LOG(Info, "Connection complete: handle=" << conn_handle);
				} else {
					LOG(Error, "Connection failed: status=" << (int)status);
				}
			}
			break;

		case 0x05:  // HCI_Disconnection_Complete
			if (param_len >= 4) {
				uint8_t status = data[3];
				uint16_t conn_handle = (data[5] << 8) | data[4];
				uint8_t reason = data[6];

				if (status == 0 && on_disconnected) {
					on_disconnected(conn_handle);
					LOG(Info, "Disconnection complete: handle=" << conn_handle << " reason=" << (int)reason);
				}
			}
			break;

		case 0x0E:  // HCI_Command_Complete
			LOG(Debug, "Command complete event");
			break;

		case 0x0F:  // HCI_Command_Status
			if (param_len >= 3) {
				uint8_t status = data[3];
				if (status != 0) {
					LOG(Warning, "Command status error: " << (int)status);
				}
			}
			break;

		default:
			// For other events, pass to on_data_received if registered
			if (on_data_received) {
				// Try to extract connection handle if present
				uint16_t conn_handle = 0xFFFF;  // Invalid handle
				if (param_len >= 2) {
					conn_handle = (data[4] << 8) | data[3];
				}
				on_data_received(conn_handle, data + 3, param_len);
			}
			break;
		}

	} else if (hci_type == BLE_HCI_HIF_ACL) {
		// HCI ACL Data packet
		if (len < 5) {
			LOG(Error, "HCI ACL packet too short");
			return;
		}

		uint16_t handle_flags = (data[2] << 8) | data[1];
		uint16_t conn_handle = handle_flags & 0x0FFF;
		uint16_t data_len = (data[4] << 8) | data[3];

		LOG(Debug, "HCI ACL Data: conn_handle=" << conn_handle
		           << " data_len=" << data_len);

		if (on_data_received && len >= 5 + data_len) {
			on_data_received(conn_handle, data + 5, data_len);
		}
	}
}

int NimbleTransport::start_advertising(const AdvertisingParams& params)
{
	ENTER();

	if (advertising_) {
		LOG(Warning, "Already advertising");
		return 0;
	}

	// Build advertising data in standard BLE format
	uint8_t adv_data[256] = {0};
	uint8_t adv_len = 0;

	// Add flags (mandatory for discoverable advertising)
	if (adv_len + 3 <= sizeof(adv_data)) {
		adv_data[adv_len++] = 2;  // Length
		adv_data[adv_len++] = 0x01;  // Type: Flags
		adv_data[adv_len++] = 0x06;  // BR/EDR Not Supported, LE General Discoverable
	}

	// Add device name if provided
	if (!params.device_name.empty()) {
		size_t name_len = params.device_name.length();
		if (name_len > 0 && adv_len + 2 + name_len <= sizeof(adv_data)) {
			adv_data[adv_len++] = 1 + name_len;  // Length
			adv_data[adv_len++] = 0x09;  // Type: Complete Local Name
			memcpy(&adv_data[adv_len], params.device_name.c_str(), name_len);
			adv_len += name_len;
		}
	}

	// Add service UUIDs if provided
	for (size_t i = 0; i < params.service_uuids.size() && i < 8; ++i) {
		const UUID& uuid = params.service_uuids[i];
		// Add 128-bit UUIDs
		if (adv_len + 2 + 16 <= sizeof(adv_data)) {
			adv_data[adv_len++] = 1 + 16;  // Length
			adv_data[adv_len++] = 0x07;  // Type: Complete List of 128-bit Service UUIDs
			memcpy(&adv_data[adv_len], uuid.value.u128.data, 16);
			adv_len += 16;
		}
	}

	// Add custom advertising data if provided
	if (params.advertising_data_len > 0) {
		size_t remaining = sizeof(adv_data) - adv_len;
		size_t copy_len = (params.advertising_data_len < remaining) ? params.advertising_data_len : remaining;
		memcpy(&adv_data[adv_len], params.advertising_data, copy_len);
		adv_len += copy_len;
	}

	sem_wait(&ioctl_sem_);
	int ret = ioctl(ioctl_fd_, ATBM_BLE_SET_ADV_DATA, (unsigned long)adv_data);
	if (ret < 0) {
		sem_post(&ioctl_sem_);
		LOG(Error, "Failed to set Nimble advertising data: " << strerror(errno));
		return -1;
	}

	// Set scan response data if provided
	if (params.scan_response_data_len > 0) {
		ret = ioctl(ioctl_fd_, ATBM_BLE_SET_RESP_DATA, (unsigned long)params.scan_response_data);
		if (ret < 0) {
			sem_post(&ioctl_sem_);
			LOG(Error, "Failed to set Nimble scan response data: " << strerror(errno));
			return -1;
		}
	}

	// Start advertising
	ret = ioctl(ioctl_fd_, ATBM_BLE_ADV_RESP_MODE_START, 0);
	sem_post(&ioctl_sem_);

	if (ret < 0) {
		LOG(Error, "Failed to start Nimble advertising: " << strerror(errno));
		return -1;
	}

	advertising_ = true;
	LOG(Info, "Nimble advertising started: " << params.device_name);
	return 0;
}

int NimbleTransport::stop_advertising()
{
	ENTER();

	if (!advertising_) {
		return 0;
	}

	// Stop BLE coexistence to stop advertising
	// This sends BLE_MSG_COEXIST_STOP to the ATBM firmware
	sem_wait(&ioctl_sem_);
	int ret = ioctl(ioctl_fd_, ATBM_BLE_COEXIST_STOP, 0);
	sem_post(&ioctl_sem_);

	if (ret < 0) {
		LOG(Error, "Failed to stop Nimble advertising: " << strerror(errno));
		return -1;
	}

	advertising_ = false;
	LOG(Info, "Nimble advertising stopped");
	return 0;
}

bool NimbleTransport::is_advertising() const
{
	return advertising_;
}

int NimbleTransport::accept_connection()
{
	// For Nimble, connections are handled asynchronously via events
	// The event loop thread will call on_connected when a connection occurs
	return 0;
}

int NimbleTransport::disconnect(uint16_t conn_handle)
{
	ENTER();

	std::lock_guard<std::mutex> lock(connections_mutex_);

	auto it = connections_.find(conn_handle);
	if (it == connections_.end()) {
		LOG(Warning, "Connection " << conn_handle << " not found");
		return -1;
	}

	// Send HCI Disconnect command
	uint8_t hci_disconnect[] = {
		0x06, 0x04,  // HCI_Disconnect opcode (0x0406)
		0x03,        // Parameter length
		(uint8_t)(conn_handle & 0xFF),
		(uint8_t)(conn_handle >> 8),
		0x13         // Reason: Remote User Terminated Connection
	};

	int ret = send_hci_command(hci_disconnect, sizeof(hci_disconnect));
	if (ret < 0) {
		LOG(Error, "Failed to send HCI disconnect command");
		return -1;
	}

	// Remove from connections map (actual disconnect event will come later)
	connections_.erase(it);

	LOG(Info, "Disconnecting connection " << conn_handle);
	return 0;
}

int NimbleTransport::send_pdu(uint16_t conn_handle, const uint8_t* data, size_t len)
{
	if (len > HCI_ACL_SHARE_SIZE) {
		LOG(Error, "PDU too large: " << len);
		return -1;
	}

	// Build HCI ACL packet
	uint8_t hci_packet[HCI_ACL_SHARE_SIZE + 10];
	uint16_t* plen = reinterpret_cast<uint16_t*>(hci_packet);

	*plen = len + 5;  // Type byte + HCI ACL header (4 bytes)
	hci_packet[2] = BLE_HCI_HIF_ACL;
	hci_packet[3] = conn_handle & 0xFF;
	hci_packet[4] = (conn_handle >> 8) & 0x0F;  // Plus PB and BC flags
	hci_packet[5] = len & 0xFF;
	hci_packet[6] = (len >> 8) & 0xFF;
	memcpy(&hci_packet[7], data, len);

	sem_wait(&ioctl_sem_);
	int ret = ioctl(ioctl_fd_, ATBM_BLE_HIF_TXDATA, (unsigned long)hci_packet);
	sem_post(&ioctl_sem_);

	if (ret < 0) {
		LOG(Error, "Failed to send HCI ACL data: " << strerror(errno));
		return -1;
	}

	LOG(Debug, "Sent " << len << " bytes on connection " << conn_handle);
	return len;
}

int NimbleTransport::recv_pdu(uint16_t conn_handle, uint8_t* buf, size_t len)
{
	// For Nimble, data is received asynchronously via events
	// This function is not used in the async model
	return -1;
}

int NimbleTransport::send_hci_command(const uint8_t* cmd, size_t len)
{
	if (len > 258) {  // Max HCI command size
		LOG(Error, "HCI command too large: " << len);
		return -1;
	}

	// Build HCI command packet
	uint8_t hci_packet[260];
	uint16_t* plen = reinterpret_cast<uint16_t*>(hci_packet);

	*plen = len + 1;  // Type byte + command data
	hci_packet[2] = BLE_HCI_HIF_CMD;
	memcpy(&hci_packet[3], cmd, len);

	sem_wait(&ioctl_sem_);
	int ret = ioctl(ioctl_fd_, ATBM_BLE_HIF_TXDATA, (unsigned long)hci_packet);
	sem_post(&ioctl_sem_);

	if (ret < 0) {
		LOG(Error, "Failed to send HCI command: " << strerror(errno));
		return -1;
	}

	LOG(Debug, "Sent HCI command: " << len << " bytes");
	return 0;
}

int NimbleTransport::set_mtu(uint16_t conn_handle, uint16_t mtu)
{
	std::lock_guard<std::mutex> lock(connections_mutex_);

	auto it = connections_.find(conn_handle);
	if (it == connections_.end()) {
		return -1;
	}

	it->second.mtu = mtu;
	LOG(Info, "Set MTU for connection " << conn_handle << " to " << mtu);
	return 0;
}

uint16_t NimbleTransport::get_mtu(uint16_t conn_handle) const
{
	std::lock_guard<std::mutex> lock(connections_mutex_);

	auto it = connections_.find(conn_handle);
	if (it == connections_.end()) {
		return 23;  // Default ATT MTU
	}

	return it->second.mtu;
}

int NimbleTransport::process_events()
{
	// Events are processed asynchronously by the event thread
	// This function is for compatibility with the transport interface
	return 0;
}

} // namespace BLEPP

#endif // BLEPP_NIMBLE_SUPPORT
