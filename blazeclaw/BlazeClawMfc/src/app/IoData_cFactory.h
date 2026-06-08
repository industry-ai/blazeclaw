#pragma once

#include <cstdint>
#include <memory>

#include "IoData_c.h"

class CIoData_cFactory final {
public:
	CIoData_cFactory() = default;
	~CIoData_cFactory() = default;

	CIoData_cFactory(const CIoData_cFactory&) = delete;
	CIoData_cFactory& operator=(const CIoData_cFactory&) = delete;

	CIoData_cFactory(CIoData_cFactory&&) = delete;
	CIoData_cFactory& operator=(CIoData_cFactory&&) = delete;

	// Allocates an IoData_c and (optionally) its raw buffer.
	// The returned shared_ptr uses a custom deleter that releases buffer + object.
	static std::shared_ptr<IoData_c> Create(uint8_t msg_type, uint32_t payload_len, int fd = -1, OpType op = OpType::Read);

	// Convenience: parses payload_len, fd, op, type from app header and creates an IoData_c.
	static std::shared_ptr<IoData_c> CreateFromHeader(const struct AppProtoHeader& header, int fd = -1, OpType op = OpType::Read);

	// Releases IoData_c created by this factory (safe for nullptr).
	static void Destroy(IoData_c* d);

private:
	static void Init(IoData_c& d, uint8_t msg_type, uint32_t payload_len, int fd, OpType op);
};