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

#include <blepp/nimble_client_transport.h>
#include <blepp/lescan.h>
#include <blepp/logging.h>

// Nimble headers
extern "C" {
#include "host/ble_hs.h"
#include "host/ble_gap.h"
#include "host/ble_gatt.h"
#include "host/util/util.h"
#include "host/ble_hs_mbuf.h"
#include "nimble/ble.h"
#include "nimble/nimble_port.h"

// Internal API for sending raw ATT PDUs
int ble_att_tx(uint16_t conn_handle, struct os_mbuf *txom);
}

#include <cstring>
#include <errno.h>
#include <sstream>
#include <iomanip>

namespace BLEPP
{

NimbleClientTransport::NimbleClientTransport()
	: initialized_(false)
	, scanning_(false)
	, next_fd_(1000)  // Start with 1000 to avoid conflicts with real FDs
{
	LOG(Info, "NimbleClientTransport: Initializing Nimble transport");

	if (initialize_nimble() == 0) {
		initialized_ = true;
	}
}

NimbleClientTransport::~NimbleClientTransport()
{
	LOG(Info, "NimbleClientTransport: Shutting down");

	if (scanning_) {
		stop_scan();
	}

	// Disconnect all connections
	std::lock_guard<std::mutex> lock(conn_mutex_);
	for (auto& pair : connections_) {
		if (pair.second.connected) {
			ble_gap_terminate(pair.second.conn_handle, BLE_ERR_REM_USER_CONN_TERM);
		}
	}
	connections_.clear();
	handle_to_fd_.clear();

	if (initialized_) {
		shutdown_nimble();
	}
}

bool NimbleClientTransport::is_available() const
{
	return initialized_;
}

// ============================================================================
// Nimble Initialization
// ============================================================================

int NimbleClientTransport::initialize_nimble()
{
	LOG(Info, "NimbleClientTransport: Initializing Nimble BLE stack");

	// Initialize Nimble port
	nimble_port_init();

	// Initialize the host
	ble_hs_cfg.sync_cb = [](void) {
		LOG(Info, "Nimble host synchronized");
	};

	ble_hs_cfg.reset_cb = [](int reason) {
		LOG(Error, "Nimble host reset, reason=" << reason);
	};

	// Set device address type to public
	int rc = ble_hs_util_ensure_addr(0);
	if (rc != 0) {
		LOG(Error, "Failed to ensure address: " << rc);
		return -1;
	}

	LOG(Info, "Nimble BLE stack initialized successfully");
	return 0;
}

void NimbleClientTransport::shutdown_nimble()
{
	LOG(Info, "NimbleClientTransport: Shutting down Nimble");

	// Stop scanning if active
	if (ble_gap_disc_active()) {
		ble_gap_disc_cancel();
	}

	// Release nimble port resources
	// Note: nimble_port_release() releases event queues and other resources
	// but doesn't fully deinitialize the stack (no nimble_port_deinit exists)
	nimble_port_release();
}

// ============================================================================
// Nimble Callbacks
// ============================================================================

int NimbleClientTransport::gap_event_callback(struct ble_gap_event* event, void* arg)
{
	NimbleClientTransport* self = static_cast<NimbleClientTransport*>(arg);
	return self->handle_gap_event(event);
}

int NimbleClientTransport::gatt_event_callback(uint16_t conn_handle, uint16_t attr_handle,
                                              struct ble_gatt_access_ctxt* ctxt, void* arg)
{
	NimbleClientTransport* self = static_cast<NimbleClientTransport*>(arg);

	// Note: This callback is for GATT server events (when acting as a peripheral).
	// For client transport, we don't register any GATT services, so this won't
	// be called. Server-initiated messages (notifications/indications) come through
	// BLE_GAP_EVENT_NOTIFY_RX in the GAP event callback instead.

	LOG(Warning, "Unexpected GATT server event on client transport");
	return BLE_ATT_ERR_UNLIKELY;
}

int NimbleClientTransport::handle_gap_event(struct ble_gap_event* event)
{
	switch (event->type) {
	case BLE_GAP_EVENT_DISC:
		handle_disc_event(&event->disc);
		break;

	case BLE_GAP_EVENT_CONNECT:
		handle_connect_event(event);
		break;

	case BLE_GAP_EVENT_DISCONNECT:
		handle_disconnect_event(event);
		break;

	case BLE_GAP_EVENT_MTU:
		handle_mtu_event(event);
		break;

	case BLE_GAP_EVENT_NOTIFY_RX:
		handle_notify_rx_event(event);
		break;

	case BLE_GAP_EVENT_DISC_COMPLETE:
		LOG(Info, "Discovery complete");
		scanning_ = false;
		break;

	default:
		LOG(Debug, "Unhandled GAP event: " << (int)event->type);
		break;
	}

	return 0;
}

void NimbleClientTransport::handle_disc_event(const struct ble_gap_disc_desc* disc)
{
	std::lock_guard<std::mutex> lock(scan_mutex_);

	// Convert address to string
	std::string addr_str = addr_to_string(disc->addr.val);

	// Check for duplicates if software filtering is enabled
	if (scan_params_.filter_duplicates) {
		if (seen_devices_.find(addr_str) != seen_devices_.end()) {
			return;  // Duplicate
		}
		seen_devices_.insert(addr_str);
	}

	// Create advertisement data structure
	AdvertisementData ad;
	ad.address = addr_str;
	ad.address_type = disc->addr.type;
	ad.rssi = disc->rssi;
	ad.event_type = disc->event_type;

	// Copy raw advertisement data
	if (disc->length_data > 0 && disc->data != nullptr) {
		ad.data.assign(disc->data, disc->data + disc->length_data);
	}

	scan_results_.push(ad);

	LOG(Debug, "Received advertisement from " << addr_str << " RSSI=" << (int)disc->rssi);

	// Call on_advertisement callback if registered
	if (on_advertisement) {
		on_advertisement(ad);
	}
}

void NimbleClientTransport::handle_connect_event(struct ble_gap_event* event)
{
	std::lock_guard<std::mutex> lock(conn_mutex_);

	uint16_t conn_handle = event->connect.conn_handle;

	if (event->connect.status != 0) {
		LOG(Error, "Connection failed: status=" << event->connect.status);

		// Clean up failed connection attempt
		auto it = handle_to_fd_.find(conn_handle);
		if (it != handle_to_fd_.end()) {
			int fd = it->second;
			connections_.erase(fd);
			handle_to_fd_.erase(conn_handle);
			LOG(Debug, "Cleaned up failed connection fd=" << fd);
		}

		// Call on_disconnected callback if registered (connection failed)
		if (on_disconnected) {
			on_disconnected(-1);  // -1 indicates connection never succeeded
		}

		return;
	}

	// Find the fd for this connection
	auto it = handle_to_fd_.find(conn_handle);
	if (it == handle_to_fd_.end()) {
		LOG(Error, "Connection complete for unknown handle: " << conn_handle);
		return;
	}

	int fd = it->second;
	auto& conn = connections_[fd];
	conn.connected = true;
	conn.conn_handle = conn_handle;

	LOG(Info, "Connected: handle=" << conn_handle << " fd=" << fd);

	// Call on_connected callback if registered
	if (on_connected) {
		on_connected(fd);
	}
}

void NimbleClientTransport::handle_disconnect_event(struct ble_gap_event* event)
{
	std::lock_guard<std::mutex> lock(conn_mutex_);

	uint16_t conn_handle = event->disconnect.conn.conn_handle;

	auto it = handle_to_fd_.find(conn_handle);
	if (it == handle_to_fd_.end()) {
		LOG(Warning, "Disconnect for unknown handle: " << conn_handle);
		return;
	}

	int fd = it->second;

	LOG(Info, "Disconnected: handle=" << conn_handle << " fd=" << fd << " reason=" << event->disconnect.reason);

	// Call on_disconnected callback before cleanup
	if (on_disconnected) {
		on_disconnected(fd);
	}

	// Clean up connection state
	connections_.erase(fd);
	handle_to_fd_.erase(conn_handle);
}

void NimbleClientTransport::handle_mtu_event(struct ble_gap_event* event)
{
	std::lock_guard<std::mutex> lock(conn_mutex_);

	uint16_t conn_handle = event->mtu.conn_handle;
	uint16_t mtu = event->mtu.value;

	auto it = handle_to_fd_.find(conn_handle);
	if (it != handle_to_fd_.end()) {
		int fd = it->second;
		connections_[fd].mtu = mtu;
		LOG(Info, "MTU updated: handle=" << conn_handle << " mtu=" << mtu);
	}
}

void NimbleClientTransport::handle_notify_rx_event(struct ble_gap_event* event)
{
	std::lock_guard<std::mutex> lock(conn_mutex_);

	uint16_t conn_handle = event->notify_rx.conn_handle;
	struct os_mbuf* om = event->notify_rx.om;

	auto it = handle_to_fd_.find(conn_handle);
	if (it == handle_to_fd_.end()) {
		LOG(Warning, "Notification for unknown handle: " << conn_handle);
		return;
	}

	int fd = it->second;
	auto& conn = connections_[fd];

	// Copy data from mbuf to vector
	// Note: This includes the ATT opcode (0x1b for notification, 0x1d for indication)
	uint16_t len = OS_MBUF_PKTLEN(om);
	std::vector<uint8_t> data(len);
	int rc = ble_hs_mbuf_to_flat(om, data.data(), len, NULL);
	if (rc == 0) {
		conn.rx_queue.push(data);
		LOG(Debug, "Received notification/indication: " << len << " bytes");

		// Call on_data_received callback if registered
		if (on_data_received) {
			on_data_received(fd, data.data(), len);
		}
	}
}

// ============================================================================
// ATT PDU Reception Notes
// ============================================================================
//
// Transport Layer Behavior:
// - send() transmits raw ATT PDUs via ble_att_tx() ✓
// - receive() returns notifications/indications from the rx_queue ✓
//
// Architectural Difference from BlueZ:
// BlueZ uses raw L2CAP sockets where all ATT PDUs (requests, responses,
// notifications) are accessible as raw bytes.
//
// Nimble uses an integrated stack where ATT request/response pairs are
// handled internally and matched synchronously. Only server-initiated
// messages (notifications/indications) are exposed via GAP events.
//
// Implications:
// 1. Notifications/indications work perfectly - they're queued in rx_queue
// 2. ATT requests sent via send() will transmit correctly
// 3. ATT responses to those requests won't appear in receive() - they're
//    consumed internally by Nimble's ATT client layer
//
// For applications using libblepp's higher-level APIs (BLEDevice), this
// works correctly since they handle ATT protocol state machines.
//
// For raw ATT PDU bidirectional access, consider using Nimble's native
// GATT client APIs (ble_gattc_*) instead of the transport layer.

// ============================================================================
// Scanning Operations
// ============================================================================

int NimbleClientTransport::start_scan(const ScanParams& params)
{
	if (!initialized_) {
		return -1;
	}

	if (scanning_) {
		LOG(Warning, "Scan already in progress");
		return -1;
	}

	scan_params_ = params;

	// Clear previous results
	{
		std::lock_guard<std::mutex> lock(scan_mutex_);
		while (!scan_results_.empty()) {
			scan_results_.pop();
		}
		seen_devices_.clear();
	}

	// Setup scan parameters
	struct ble_gap_disc_params disc_params;
	memset(&disc_params, 0, sizeof(disc_params));

	disc_params.passive = params.scan_type == ScanParams::ScanType::Passive ? 1 : 0;
	disc_params.filter_duplicates = params.filter_duplicates ? 1 : 0;
	disc_params.filter_policy = BLE_HCI_SCAN_FILT_NO_WL;

	// Convert interval and window from ms to BLE units (0.625ms units)
	disc_params.itvl = (params.interval_ms * 1000) / 625;
	disc_params.window = (params.window_ms * 1000) / 625;

	// Start scan
	int rc = ble_gap_disc(BLE_OWN_ADDR_PUBLIC, BLE_HS_FOREVER, &disc_params,
	                      gap_event_callback, this);

	if (rc != 0) {
		LOG(Error, "Failed to start scan: " << rc);
		return -1;
	}

	scanning_ = true;
	LOG(Info, "Scan started");

	return 0;
}

int NimbleClientTransport::stop_scan()
{
	if (!initialized_) {
		return -1;
	}

	if (!scanning_) {
		return 0;
	}

	int rc = ble_gap_disc_cancel();
	if (rc != 0 && rc != BLE_HS_EALREADY) {
		LOG(Error, "Failed to stop scan: " << rc);
		return -1;
	}

	scanning_ = false;
	LOG(Info, "Scan stopped");

	return 0;
}

int NimbleClientTransport::get_advertisements(std::vector<AdvertisementData>& ads, int timeout_ms)
{
	std::lock_guard<std::mutex> lock(scan_mutex_);

	while (!scan_results_.empty()) {
		ads.push_back(scan_results_.front());
		scan_results_.pop();
	}

	return ads.size();
}

// ============================================================================
// Connection Operations
// ============================================================================

int NimbleClientTransport::connect(const ClientConnectionParams& params)
{
	if (!initialized_) {
		return -1;
	}

	// Allocate a new fd
	int fd = allocate_fd();

	// Parse address
	uint8_t addr[6];
	string_to_addr(params.peer_address, addr);

	// Create connection info
	ConnectionInfo conn_info;
	conn_info.conn_handle = 0;  // Will be set when connection completes
	conn_info.mtu = 23;  // Default ATT MTU
	conn_info.address = params.peer_address;
	conn_info.connected = false;

	{
		std::lock_guard<std::mutex> lock(conn_mutex_);
		connections_[fd] = conn_info;
	}

	// Setup connection parameters
	struct ble_gap_conn_params conn_params;
	memset(&conn_params, 0, sizeof(conn_params));

	// Convert from BLE units (params are already in BLE units)
	conn_params.scan_itvl = 0x0010;  // 10ms
	conn_params.scan_window = 0x0010;  // 10ms
	conn_params.itvl_min = params.min_interval;  // Already in 1.25ms units
	conn_params.itvl_max = params.max_interval;  // Already in 1.25ms units
	conn_params.latency = params.latency;
	conn_params.supervision_timeout = params.timeout;  // Already in 10ms units
	conn_params.min_ce_len = 0;
	conn_params.max_ce_len = 0;

	// Start connection
	ble_addr_t peer_addr;
	peer_addr.type = params.peer_address_type;  // 0=public, 1=random
	memcpy(peer_addr.val, addr, 6);

	int rc = ble_gap_connect(BLE_OWN_ADDR_PUBLIC, &peer_addr, 30000,  // 30 second timeout
	                         &conn_params, gap_event_callback, this);

	if (rc != 0) {
		LOG(Error, "Failed to initiate connection: " << rc);
		release_fd(fd);
		return -1;
	}

	// Store the handle mapping (will be updated when connection completes)
	{
		std::lock_guard<std::mutex> lock(conn_mutex_);
		// Get the connection handle that was just created
		struct ble_gap_conn_desc desc;
		if (ble_gap_conn_find_by_addr(&peer_addr, &desc) == 0) {
			handle_to_fd_[desc.conn_handle] = fd;
			connections_[fd].conn_handle = desc.conn_handle;
		}
	}

	LOG(Info, "Connection initiated to " << params.peer_address << " fd=" << fd);

	return fd;
}

int NimbleClientTransport::disconnect(int fd)
{
	std::lock_guard<std::mutex> lock(conn_mutex_);

	auto it = connections_.find(fd);
	if (it == connections_.end()) {
		return -1;
	}

	uint16_t conn_handle = it->second.conn_handle;

	int rc = ble_gap_terminate(conn_handle, BLE_ERR_REM_USER_CONN_TERM);
	if (rc != 0) {
		LOG(Error, "Failed to disconnect: " << rc);
		return -1;
	}

	LOG(Info, "Disconnect initiated for fd=" << fd);

	return 0;
}

int NimbleClientTransport::get_fd(int fd) const
{
	// For Nimble transport, we don't have a real file descriptor
	// Return the fake fd itself for use with select/poll emulation
	return fd;
}

// ============================================================================
// Data Transfer
// ============================================================================

int NimbleClientTransport::send(int fd, const uint8_t* data, size_t len)
{
	std::lock_guard<std::mutex> lock(conn_mutex_);

	auto it = connections_.find(fd);
	if (it == connections_.end() || !it->second.connected) {
		return -1;
	}

	uint16_t conn_handle = it->second.conn_handle;

	// Allocate mbuf for the ATT PDU
	struct os_mbuf* om = ble_hs_mbuf_from_flat(data, len);
	if (om == NULL) {
		LOG(Error, "Failed to allocate mbuf for send");
		return -1;
	}

	// Send raw ATT PDU through Nimble stack
	int rc = ble_att_tx(conn_handle, om);
	if (rc != 0) {
		LOG(Error, "Failed to send ATT PDU: " << rc);
		// mbuf is freed on error by ble_att_tx
		return -1;
	}

	LOG(Debug, "Sent " << len << " bytes on fd=" << fd);

	return len;
}

int NimbleClientTransport::receive(int fd, uint8_t* data, size_t max_len)
{
	std::lock_guard<std::mutex> lock(conn_mutex_);

	auto it = connections_.find(fd);
	if (it == connections_.end()) {
		return -1;
	}

	auto& conn = it->second;

	if (conn.rx_queue.empty()) {
		return 0;  // No data available
	}

	const std::vector<uint8_t>& pkt = conn.rx_queue.front();
	size_t copy_len = (max_len < pkt.size()) ? max_len : pkt.size();

	memcpy(data, pkt.data(), copy_len);
	conn.rx_queue.pop();

	return copy_len;
}

// ============================================================================
// MTU Operations
// ============================================================================

uint16_t NimbleClientTransport::get_mtu(int fd) const
{
	std::lock_guard<std::mutex> lock(conn_mutex_);

	auto it = connections_.find(fd);
	if (it == connections_.end()) {
		return 0;
	}

	return it->second.mtu;
}

int NimbleClientTransport::set_mtu(int fd, uint16_t mtu)
{
	std::lock_guard<std::mutex> lock(conn_mutex_);

	auto it = connections_.find(fd);
	if (it == connections_.end() || !it->second.connected) {
		return -1;
	}

	uint16_t conn_handle = it->second.conn_handle;

	// Initiate MTU exchange
	int rc = ble_gattc_exchange_mtu(conn_handle, NULL, NULL);
	if (rc != 0) {
		LOG(Error, "Failed to exchange MTU: " << rc);
		return -1;
	}

	// The actual MTU will be updated in handle_mtu_event
	return 0;
}

// ============================================================================
// Helper Functions
// ============================================================================

int NimbleClientTransport::allocate_fd()
{
	return next_fd_++;
}

void NimbleClientTransport::release_fd(int fd)
{
	std::lock_guard<std::mutex> lock(conn_mutex_);
	connections_.erase(fd);
}

std::string NimbleClientTransport::addr_to_string(const uint8_t addr[6])
{
	std::ostringstream oss;
	oss << std::hex << std::setfill('0');
	for (int i = 5; i >= 0; --i) {
		oss << std::setw(2) << (int)addr[i];
		if (i > 0) oss << ":";
	}
	return oss.str();
}

void NimbleClientTransport::string_to_addr(const std::string& str, uint8_t addr[6])
{
	int values[6];
	if (sscanf(str.c_str(), "%x:%x:%x:%x:%x:%x",
	           &values[5], &values[4], &values[3],
	           &values[2], &values[1], &values[0]) == 6) {
		for (int i = 0; i < 6; ++i) {
			addr[i] = (uint8_t)values[i];
		}
	}
}

} // namespace BLEPP

#endif // BLEPP_NIMBLE_SUPPORT
