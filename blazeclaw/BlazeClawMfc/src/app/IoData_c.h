#pragma once

// IoData structure used by the CClient and CConnection_c paths.

#include <cstdint>
#include <string>

enum class OpType : uint8_t {
	Void = 0,
	Read,
	Write
};

struct alignas(64) IoData_c {
	int			fd = -1;			// Socket file descriptor
	OpType		op = OpType::Read;	// Operation type
	uint8_t		type;				// Message type (from AppProtoHeader)
	char*		buffer = nullptr;	// Data buffer
	uint32_t	payload_len;		// Length of payload in bytes
	std::string	payload;			// Payload data
};