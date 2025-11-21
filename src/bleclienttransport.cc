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

#include <blepp/bleclienttransport.h>
#include <blepp/logging.h>

#ifdef BLEPP_BLUEZ_SUPPORT
#include <blepp/bluez_client_transport.h>
#endif

#ifdef BLEPP_NIMBLE_SUPPORT
#include <blepp/nimble_client_transport.h>
#endif

namespace BLEPP
{

BLEClientTransport* create_client_transport()
{
	ENTER();

	// Prefer BlueZ if available and working, fall back to Nimble
#ifdef BLEPP_BLUEZ_SUPPORT
	{
		BlueZClientTransport* transport = new BlueZClientTransport();
		if (transport->is_available()) {
			LOG(Info, "Using BlueZ client transport");
			return transport;
		}
		delete transport;
		LOG(Warning, "BlueZ transport not available");
	}
#endif

#ifdef BLEPP_NIMBLE_SUPPORT
	{
		NimbleClientTransport* transport = new NimbleClientTransport();
		if (transport->is_available()) {
			LOG(Info, "Using Nimble client transport");
			return transport;
		}
		delete transport;
		LOG(Warning, "Nimble transport not available");
	}
#endif

	LOG(Error, "No BLE client transport available");
	return nullptr;
}

#ifdef BLEPP_BLUEZ_SUPPORT
BLEClientTransport* create_bluez_client_transport()
{
	ENTER();
	LOG(Info, "Creating BlueZ client transport");
	return new BlueZClientTransport();
}
#endif

#ifdef BLEPP_NIMBLE_SUPPORT
BLEClientTransport* create_nimble_client_transport()
{
	ENTER();
	LOG(Info, "Creating Nimble client transport");
	return new NimbleClientTransport();
}
#endif

} // namespace BLEPP
