/* 
 * QR Code generator library (C++)
 * 
 * Copyright (c) Project Nayuki. (MIT License)
 * https://www.nayuki.io/page/qr-code-generator-library
 * 
 * Permission is hereby granted, free of charge, to any person obtaining a copy of
 * this software and associated documentation files (the "Software"), to deal in
 * the Software without restriction, including without limitation the rights to
 * use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
 * the Software, and to permit persons to whom the Software is furnished to do so,
 * subject to the following conditions:
 * - The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 * - The Software is provided "as is", without warranty of any kind, express or
 * implied, including but not limited to the warranties of merchantability,
 * fitness for a particular purpose and noninfringement. In no event shall the
 * authors or copyright holders be liable for any claim, damages or other
 * liability, whether in an action of contract, tort or otherwise, arising from,
 * out of or in connection with the Software or the use or other dealings in the
 * Software.
 */

#pragma once

#include <array>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

namespace qrcodegen {

class QrSegment final {
public:
	class Mode final {
	public:
		static const Mode NUMERIC;
		static const Mode ALPHANUMERIC;
		static const Mode BYTE;
		static const Mode KANJI;
		static const Mode ECI;
	public:
		int getModeBits() const;
		int numCharCountBits(int ver) const;
	private:
		Mode(int mode, int cc0, int cc1, int cc2);
		int modeBits;
		int numBitsCharCount[3];
	};

public:
	static QrSegment makeBytes(const std::vector<uint8_t>& data);
	static QrSegment makeNumeric(const char* digits);
	static QrSegment makeAlphanumeric(const char* text);
	static std::vector<QrSegment> makeSegments(const char* text);
	static QrSegment makeEci(long assignVal);
	static bool isNumeric(const char* text);
	static bool isAlphanumeric(const char* text);
	static int getTotalBits(const std::vector<QrSegment>& segs, int version);

public:
	const Mode& getMode() const;
	int getNumChars() const;
	const std::vector<bool>& getData() const;

private:
	QrSegment(const Mode& md, int numCh, const std::vector<bool>& dt);
	QrSegment(const Mode& md, int numCh, std::vector<bool>&& dt);
	static const char* ALPHANUMERIC_CHARSET;

	const Mode* mode;
	int numChars;
	std::vector<bool> data;
};

class QrCode final {
public:
	enum class Ecc {
		LOW = 0,
		MEDIUM,
		QUARTILE,
		HIGH
	};

public:
	static QrCode encodeText(const char* text, Ecc ecl);
	static QrCode encodeBinary(const std::vector<uint8_t>& data, Ecc ecl);
	static QrCode encodeSegments(const std::vector<QrSegment>& segs, Ecc ecl,
		int minVersion = 1, int maxVersion = 40, int mask = -1, bool boostEcl = true);

public:
	int getVersion() const;
	int getSize() const;
	Ecc getErrorCorrectionLevel() const;
	int getMask() const;
	bool getModule(int x, int y) const;

public:
	static constexpr int MIN_VERSION = 1;
	static constexpr int MAX_VERSION = 40;

private:
	QrCode(int ver, Ecc ecl, const std::vector<uint8_t>& dataCodewords, int msk);

	void drawFunctionPatterns();
	void drawFormatBits(int msk);
	void drawVersion();
	void drawFinderPattern(int x, int y);
	void drawAlignmentPattern(int x, int y);
	void setFunctionModule(int x, int y, bool isDark);
	bool module(int x, int y) const;
	std::vector<uint8_t> addEccAndInterleave(const std::vector<uint8_t>& data) const;
	void drawCodewords(const std::vector<uint8_t>& data);
	void applyMask(int msk);
	long getPenaltyScore() const;
	static int getFormatBits(Ecc ecl);
	std::vector<int> getAlignmentPatternPositions() const;
	static int getNumRawDataModules(int ver);
	static int getNumDataCodewords(int ver, Ecc ecl);
	static std::vector<uint8_t> reedSolomonComputeDivisor(int degree);
	static std::vector<uint8_t> reedSolomonComputeRemainder(const std::vector<uint8_t>& data, const std::vector<uint8_t>& divisor);
	static uint8_t reedSolomonMultiply(uint8_t x, uint8_t y);
	int finderPenaltyCountPatterns(const std::array<int, 7>& runHistory) const;
	int finderPenaltyTerminateAndCount(bool currentRunColor, int currentRunLength, std::array<int, 7>& runHistory) const;
	void finderPenaltyAddHistory(int currentRunLength, std::array<int, 7>& runHistory) const;
	static bool getBit(long x, int i);

	static const int PENALTY_N1;
	static const int PENALTY_N2;
	static const int PENALTY_N3;
	static const int PENALTY_N4;

	static const int8_t ECC_CODEWORDS_PER_BLOCK[4][41];
	static const int8_t NUM_ERROR_CORRECTION_BLOCKS[4][41];

	int version;
	int size;
	Ecc errorCorrectionLevel;
	int mask;
	std::vector<std::vector<bool>> modules;
	std::vector<std::vector<bool>> isFunction;
};

class data_too_long : public std::length_error {
public:
	explicit data_too_long(const std::string& msg);
};

class BitBuffer final : public std::vector<bool> {
public:
	BitBuffer();
	void appendBits(uint32_t val, int len);
};

}
