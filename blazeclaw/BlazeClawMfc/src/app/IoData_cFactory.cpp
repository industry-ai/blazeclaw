#include "pch.h"
#include "IoData_cFactory.h"

#include <new>

#include "AppProtoHeader.h"

void CIoData_cFactory::Init(IoData_c& d, uint8_t msg_type, uint32_t payload_len, int fd, OpType op) {
	d.fd = fd;
	d.op = op;
	d.type = msg_type;
	d.payload_len = payload_len;
	d.payload.clear();

	// Allocate raw buffer for payload if requested.
	if (payload_len > 0) {
		d.buffer = new (std::nothrow) char[payload_len];
	}
	else {
		d.buffer = nullptr;
	}
}

std::shared_ptr<IoData_c> CIoData_cFactory::Create(uint8_t msg_type, uint32_t payload_len, int fd, OpType op) {
	IoData_c* d = new (std::nothrow) IoData_c{};
	if (!d) {
		return {};
	}

	Init(*d, msg_type, payload_len, fd, op);

	// If buffer allocation failed, clean up and return empty.
	if (payload_len > 0 && d->buffer == nullptr) {
		Destroy(d);
		return {};
	}

	return std::shared_ptr<IoData_c>(d, &CIoData_cFactory::Destroy);
}

std::shared_ptr<IoData_c> CIoData_cFactory::CreateFromHeader(const AppProtoHeader& header, int fd, OpType op) {
	return Create(header.type, header.payload_len, fd, op);
}

void CIoData_cFactory::Destroy(IoData_c* d) {
	if (!d) {
		return;
	}

	delete[] d->buffer;
	d->buffer = nullptr;

	delete d;
}