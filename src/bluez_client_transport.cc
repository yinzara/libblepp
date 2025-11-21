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

#ifdef BLEPP_BLUEZ_SUPPORT

#include <blepp/bluez_client_transport.h>
#include <blepp/logging.h>

#include <bluetooth/bluetooth.h>
#include <bluetooth/hci.h>
#include <bluetooth/hci_lib.h>
#include <bluetooth/l2cap.h>

#include <sys/socket.h>
#include <sys/ioctl.h>
#include <sys/select.h>
#include <unistd.h>
#include <fcntl.h>
#include <cerrno>
#include <cstring>

namespace BLEPP
{

BlueZClientTransport::BlueZClientTransport()
	: hci_dev_id_(-1)
	, hci_fd_(-1)
	, scanning_(false)
{
	ENTER();
}

BlueZClientTransport::~BlueZClientTransport()
{
	ENTER();

	if (scanning_) {
		stop_scan();
	}

	// Close all connections
	for (auto& conn : connections_) {
		if (conn.second.fd >= 0) {
			close(conn.second.fd);
		}
	}
	connections_.clear();

	close_hci_device();
}

bool BlueZClientTransport::is_available() const
{
	// Check if we can open HCI device
	int dev_id = hci_get_route(nullptr);
	if (dev_id < 0) {
		return false;
	}

	int fd = hci_open_dev(dev_id);
	if (fd < 0) {
		return false;
	}

	close(fd);
	return true;
}

int BlueZClientTransport::open_hci_device()
{
	ENTER();

	if (hci_fd_ >= 0) {
		return 0;  // Already open
	}

	hci_dev_id_ = hci_get_route(nullptr);
	if (hci_dev_id_ < 0) {
		LOG(Error, "No Bluetooth adapter found");
		return -1;
	}

	hci_fd_ = hci_open_dev(hci_dev_id_);
	if (hci_fd_ < 0) {
		LOG(Error, "Failed to open HCI device: " << strerror(errno));
		return -1;
	}

	LOG(Debug, "Opened HCI device " << hci_dev_id_ << " (fd=" << hci_fd_ << ")");
	return 0;
}

void BlueZClientTransport::close_hci_device()
{
	ENTER();

	if (hci_fd_ >= 0) {
		hci_close_dev(hci_fd_);
		hci_fd_ = -1;
		hci_dev_id_ = -1;
	}
}

int BlueZClientTransport::start_scan(const ScanParams& params)
{
	ENTER();

	if (scanning_) {
		LOG(Warning, "Already scanning");
		return 0;
	}

	if (open_hci_device() < 0) {
		return -1;
	}

	scan_params_ = params;

	// Set scan parameters
	if (set_scan_parameters(params) < 0) {
		return -1;
	}

	// Enable scanning
	if (set_scan_enable(true, params.filter_duplicates) < 0) {
		return -1;
	}

	scanning_ = true;
	seen_devices_.clear();

	LOG(Info, "BLE scanning started");
	return 0;
}

int BlueZClientTransport::stop_scan()
{
	ENTER();

	if (!scanning_) {
		return 0;
	}

	if (set_scan_enable(false, false) < 0) {
		LOG(Warning, "Failed to disable scanning");
	}

	scanning_ = false;
	LOG(Info, "BLE scanning stopped");
	return 0;
}

int BlueZClientTransport::set_scan_parameters(const ScanParams& params)
{
	ENTER();

	uint8_t scan_type = static_cast<uint8_t>(params.scan_type);
	uint16_t interval = params.interval_ms * 1000 / 625;  // Convert to 0.625ms units
	uint16_t window = params.window_ms * 1000 / 625;
	uint8_t own_type = 0x00;  // Public address
	uint8_t filter = static_cast<uint8_t>(params.filter_policy);

	if (hci_le_set_scan_parameters(hci_fd_, scan_type, htobs(interval),
	                                htobs(window), own_type, filter, 1000) < 0) {
		LOG(Error, "Failed to set scan parameters: " << strerror(errno));
		return -1;
	}

	LOG(Debug, "Set scan parameters: interval=" << params.interval_ms
	          << "ms window=" << params.window_ms << "ms");
	return 0;
}

int BlueZClientTransport::set_scan_enable(bool enable, bool filter_duplicates)
{
	ENTER();

	uint8_t enable_val = enable ? 0x01 : 0x00;
	uint8_t filter_dup = filter_duplicates ? 0x01 : 0x00;

	if (hci_le_set_scan_enable(hci_fd_, enable_val, filter_dup, 1000) < 0) {
		LOG(Error, "Failed to " << (enable ? "enable" : "disable")
		          << " scanning: " << strerror(errno));
		return -1;
	}

	return 0;
}

int BlueZClientTransport::get_advertisements(std::vector<AdvertisementData>& ads, int timeout_ms)
{
	ENTER();

	if (!scanning_) {
		LOG(Error, "Not scanning");
		return -1;
	}

	ads.clear();
	return read_hci_events(ads, timeout_ms);
}

int BlueZClientTransport::read_hci_events(std::vector<AdvertisementData>& ads, int timeout_ms)
{
	ENTER();

	fd_set rfds;
	struct timeval tv;

	FD_ZERO(&rfds);
	FD_SET(hci_fd_, &rfds);

	if (timeout_ms >= 0) {
		tv.tv_sec = timeout_ms / 1000;
		tv.tv_usec = (timeout_ms % 1000) * 1000;
	}

	int ret = select(hci_fd_ + 1, &rfds, nullptr, nullptr,
	                 timeout_ms >= 0 ? &tv : nullptr);

	if (ret < 0) {
		LOG(Error, "select() failed: " << strerror(errno));
		return -1;
	}

	if (ret == 0) {
		// Timeout
		return 0;
	}

	// Read HCI event
	uint8_t buf[HCI_MAX_EVENT_SIZE];
	ssize_t len = read(hci_fd_, buf, sizeof(buf));

	if (len < 0) {
		LOG(Error, "read() failed: " << strerror(errno));
		return -1;
	}

	if (len < HCI_EVENT_HDR_SIZE) {
		return 0;
	}

	// Parse HCI event
	hci_event_hdr* hdr = (hci_event_hdr*)buf;

	if (hdr->evt != EVT_LE_META_EVENT) {
		return 0;
	}

	// Parse LE meta event
	return parse_advertising_report(buf + HCI_EVENT_HDR_SIZE + 1,
	                                len - HCI_EVENT_HDR_SIZE - 1, ads);
}

int BlueZClientTransport::parse_advertising_report(const uint8_t* data, size_t len,
                                                   std::vector<AdvertisementData>& ads)
{
	if (len < 1) return 0;

	uint8_t subevent = data[0];

	if (subevent != EVT_LE_ADVERTISING_REPORT) {
		return 0;
	}

	if (len < 2) return 0;

	uint8_t num_reports = data[1];
	const uint8_t* ptr = data + 2;
	const uint8_t* end = data + len;

	for (uint8_t i = 0; i < num_reports && ptr < end; i++) {
		if (ptr + 10 > end) break;  // Minimum report size

		AdvertisementData ad;
		ad.event_type = ptr[0];
		ad.address_type = ptr[1];

		// Parse address (6 bytes, little-endian)
		char addr_str[18];
		ba2str((bdaddr_t*)(ptr + 2), addr_str);
		ad.address = addr_str;

		uint8_t data_len = ptr[8];
		ptr += 9;

		if (ptr + data_len + 1 > end) break;

		// Copy advertising data
		ad.data.assign(ptr, ptr + data_len);
		ptr += data_len;

		// RSSI
		ad.rssi = (int8_t)(*ptr);
		ptr++;

		// Apply software duplicate filtering if enabled
		if (scan_params_.filter_duplicates) {
			if (seen_devices_.count(ad.address) > 0) {
				continue;  // Skip duplicate
			}
			seen_devices_.insert(ad.address);
		}

		ads.push_back(ad);

		// Call callback if set
		if (on_advertisement) {
			on_advertisement(ad);
		}
	}

	return ads.size();
}

int BlueZClientTransport::connect(const ClientConnectionParams& params)
{
	ENTER();

	// Create L2CAP socket
	int sock = socket(AF_BLUETOOTH, SOCK_SEQPACKET, BTPROTO_L2CAP);
	if (sock < 0) {
		LOG(Error, "Failed to create L2CAP socket: " << strerror(errno));
		return -1;
	}

	// Set up address
	struct sockaddr_l2 addr = {};
	addr.l2_family = AF_BLUETOOTH;
	addr.l2_cid = htobs(4);  // ATT CID
	addr.l2_bdaddr_type = params.peer_address_type;

	// Parse address string to bdaddr_t
	if (str2ba(params.peer_address.c_str(), &addr.l2_bdaddr) < 0) {
		LOG(Error, "Invalid Bluetooth address: " << params.peer_address);
		close(sock);
		return -1;
	}

	// Connect
	if (::connect(sock, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
		LOG(Error, "Failed to connect to " << params.peer_address
		          << ": " << strerror(errno));
		close(sock);
		return -1;
	}

	// Store connection info
	ConnectionInfo info;
	info.fd = sock;
	info.mtu = 23;  // Default ATT MTU
	info.address = params.peer_address;

	connections_[sock] = info;

	LOG(Info, "Connected to " << params.peer_address << " (fd=" << sock << ")");

	// Call callback if set
	if (on_connected) {
		on_connected(sock);
	}

	return sock;
}

int BlueZClientTransport::disconnect(int fd)
{
	ENTER();

	auto it = connections_.find(fd);
	if (it == connections_.end()) {
		LOG(Warning, "Unknown connection fd=" << fd);
		return -1;
	}

	std::string addr = it->second.address;
	close(fd);
	connections_.erase(it);

	LOG(Info, "Disconnected from " << addr << " (fd=" << fd << ")");

	// Call callback if set
	if (on_disconnected) {
		on_disconnected(fd);
	}

	return 0;
}

int BlueZClientTransport::get_fd(int fd) const
{
	// In BlueZ, the connection fd IS the file descriptor for select/poll
	return fd;
}

int BlueZClientTransport::send(int fd, const uint8_t* data, size_t len)
{
	auto it = connections_.find(fd);
	if (it == connections_.end()) {
		LOG(Error, "Invalid connection fd=" << fd);
		return -1;
	}

	ssize_t sent = write(fd, data, len);
	if (sent < 0) {
		LOG(Error, "Failed to send data on fd=" << fd << ": " << strerror(errno));
		return -1;
	}

	return sent;
}

int BlueZClientTransport::receive(int fd, uint8_t* data, size_t max_len)
{
	auto it = connections_.find(fd);
	if (it == connections_.end()) {
		LOG(Error, "Invalid connection fd=" << fd);
		return -1;
	}

	ssize_t received = read(fd, data, max_len);
	if (received < 0) {
		LOG(Error, "Failed to receive data on fd=" << fd << ": " << strerror(errno));
		return -1;
	}

	if (received == 0) {
		// Connection closed
		LOG(Info, "Connection closed by peer (fd=" << fd << ")");
		return 0;
	}

	// Call callback if set
	if (on_data_received) {
		on_data_received(fd, data, received);
	}

	return received;
}

uint16_t BlueZClientTransport::get_mtu(int fd) const
{
	auto it = connections_.find(fd);
	if (it == connections_.end()) {
		return 23;  // Default ATT MTU
	}

	return it->second.mtu;
}

int BlueZClientTransport::set_mtu(int fd, uint16_t mtu)
{
	auto it = connections_.find(fd);
	if (it == connections_.end()) {
		LOG(Error, "Invalid connection fd=" << fd);
		return -1;
	}

	it->second.mtu = mtu;
	LOG(Debug, "Set MTU=" << mtu << " for fd=" << fd);
	return 0;
}

// ============================================================================
// MAC Address Operations
// ============================================================================

std::string BlueZClientTransport::get_mac_address() const
{
	ENTER();

	// Return cached address if available
	if (!mac_address_.empty()) {
		return mac_address_;
	}

	// Read address from HCI device
	bdaddr_t bdaddr;
	int rc = hci_devba(hci_dev_id_, &bdaddr);
	if (rc < 0) {
		LOG(Error, "Failed to read device address");
		return "";
	}

	// Convert bdaddr_t to string (BlueZ format is little-endian)
	char addr_str[18];
	ba2str(&bdaddr, addr_str);
	mac_address_ = addr_str;

	return mac_address_;
}

} // namespace BLEPP

#endif // BLEPP_BLUEZ_SUPPORT
