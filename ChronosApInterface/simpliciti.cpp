#include "simpliciti.h"

#include <algorithm>
#include <chrono>
#include <exception>
#include <iostream>

#define USB_PACKET_HEADER_LENGTH		0x03
#define USB_PACKET_START_BYTE			0xFF
#define USB_PACKET_START_BYTE_INDEX		0x00
#define USB_PACKET_COMMAND_BYTE_INDEX	0x01
#define USB_PACKET_LENGTH_BYTE_INDEX	0x02

#define SIMPLICITI_PACKET_START_INDEX	0x03

/*
* USB RF Access Point USB Serial protocol:
* First byte - 0xFF
* Second byte - Command (for example BM_START_SIMPLICITI is 0x07)
* Third byte - packet length (minimum length is 3, maximum length is 32)
* ACK packet is sent as original packet received with modified command byte - HW_NO_ERROR (0x06)
*/

#define BM_START_SIMPLICITI 0x07
#define BM_GET_SIMPLICITIDATA 0x08
#define BM_STOP_SIMPLICITI 0x09

#define HW_NO_ERROR 0x06

namespace
{
	size_t s_bytesReceived = 0;
	std::vector<uint8_t> startSimpliciTiCommand = {USB_PACKET_START_BYTE, BM_START_SIMPLICITI, 0x03};
	std::vector<uint8_t> stopSimpliciTiCommand = {USB_PACKET_START_BYTE, BM_STOP_SIMPLICITI, 0x03};
	std::vector<uint8_t> startSimpliciTiCommandResponse = {USB_PACKET_START_BYTE, HW_NO_ERROR, 0x03};
	std::vector<uint8_t> usbPacketStartSequence = {USB_PACKET_START_BYTE, HW_NO_ERROR};
}

SimpliciTi::SimpliciTi(HANDLE comHandle, const std::function<void(std::vector<uint8_t>)>& fileLogCallback) : m_fileLogCallback(fileLogCallback)
{
	if (comHandle == INVALID_HANDLE_VALUE || comHandle == nullptr)
		throw std::exception("Invalid handle supplied to the SimpliciTI parser.");

	m_comHandle = comHandle;

	// 1000 bytes for the COM buffer.
	m_comBuffer.reserve(1000);
}

void SimpliciTi::startAccessPoint()
{
	FlushFileBuffers(m_comHandle);
	writeCommand(startSimpliciTiCommand);
	while (m_comBuffer.size() < startSimpliciTiCommandResponse.size())
		readData(startSimpliciTiCommandResponse.size() - m_comBuffer.size());

	if (!(m_comBuffer == startSimpliciTiCommandResponse))
		throw std::exception("Starting access point failed. Check the device.");

	m_comBuffer.resize(0);

	m_parseTask = std::thread([&]{
									while (!m_stopParsing)
									{
										std::this_thread::sleep_for(std::chrono::milliseconds(1));
										parseAndLogPackets();
										std::cout << "\rBytes received: " << s_bytesReceived;
									}
								  });
}

void SimpliciTi::stopAccessPoint()
{
	m_stopParsing = true;

	// We do no even care of the response anymore...
	writeCommand(stopSimpliciTiCommand);

	FlushFileBuffers(m_comHandle);
}

void SimpliciTi::writeCommand(const std::vector<uint8_t>& command)
{
	DWORD bytesSent = 0;
	DWORD commandLength = command.size();

	while (bytesSent < commandLength)
	{
		DWORD bytesSentSingleWrite = 0;
		auto success = WriteFile(m_comHandle, startSimpliciTiCommand.data() + bytesSent, commandLength - bytesSent, &bytesSentSingleWrite, nullptr);

		// Probably some error, so lets not stay looping.
		if (bytesSentSingleWrite == 0 || !success)
		{
			throw std::exception("Writing to serial was unsuccesful.");
		}

		bytesSent += bytesSentSingleWrite;
	}
}

void SimpliciTi::readData(size_t dataLength)
{
	// If there is no room for data, then lets read less.
	auto freeBytes = m_comBuffer.capacity() - m_comBuffer.size();

	// Unfortunately we cannot set the size of the vector directly, so we read byte by byte.
	DWORD bytesReadTotal = 0;
	while (bytesReadTotal < std::min(freeBytes, dataLength))
	{
		DWORD bytesRead = 0;
		uint8_t aByte = 0;
		auto success = ReadFile(m_comHandle, &aByte, 1, &bytesRead, nullptr);

		// We do not care if we managed to read any data, because the I/O buffers can be empty.
		if (!success)
			throw std::exception("Reading the COM port was not succesful. Check the device manager or the device.");

		// Right no could not get more, so break here, lets not block.
		if (bytesRead == 0)
			break;

		bytesReadTotal += bytesRead;
		m_comBuffer.push_back(aByte);
	}

	s_bytesReceived += bytesReadTotal;
}

// The algorithm here searches for the USB packet header and extracts the length. If there are communication
// errors, for example the packet data was not received completely then this is not checked. And probably
// one packet will be corrupt in the log and one or more packets will be discarded. Should have mutex protection
// here, but since nothing besides m_parseTask uses it, it is safe for now.
void SimpliciTi::parseAndLogPackets()
{
	readData(100);

	// We do not know the new packet length and we must have atleast the complete header.
	if (m_currentPacketSize == 0)
	{
		// Not enough data to find the header though.
		if (m_comBuffer.size() < USB_PACKET_HEADER_LENGTH)
		{
			return;
		}

		// Searching for 0xFF, 0x06.
		auto newPacketBeginning = std::search(m_comBuffer.begin(), m_comBuffer.end(), usbPacketStartSequence.begin(), usbPacketStartSequence.end());

		// Complete packet header not found.
		if (newPacketBeginning + USB_PACKET_LENGTH_BYTE_INDEX >= m_comBuffer.end())
		{
			return;
		}
		
		// If this is somehow still zero, then next time new packet will be searched for anyway.
		m_currentPacketSize = *(newPacketBeginning + USB_PACKET_LENGTH_BYTE_INDEX) - USB_PACKET_HEADER_LENGTH;

		// Erase all the not useful data and the header so later we could just cut the usable data out.
		m_comBuffer.erase(m_comBuffer.begin(), newPacketBeginning + USB_PACKET_HEADER_LENGTH);
	}

	if (m_comBuffer.size() < m_currentPacketSize)
	{
		return;
	}

	// Lets extract the packet data out.
	m_fileLogCallback(std::vector<uint8_t>(m_comBuffer.begin(), m_comBuffer.begin() + m_currentPacketSize));

	m_comBuffer.erase(m_comBuffer.begin(), m_comBuffer.begin() + m_currentPacketSize);

	m_currentPacketSize = 0;
}

SimpliciTi::~SimpliciTi()
{
	stopAccessPoint();

	if (m_parseTask.joinable())
		m_parseTask.join();
}

