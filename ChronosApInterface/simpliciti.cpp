#include "simpliciti.h"

#include <algorithm>
#include <chrono>
#include <ctime>
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
* Third byte - packet length (minimum length is 3)
* ACK packet is sent as original packet received with modified command byte - HW_NO_ERROR (0x06)
*/

#define BM_START_SIMPLICITI 0x07
#define BM_GET_SIMPLICITIDATA 0x08
#define BM_STOP_SIMPLICITI 0x09

#define HW_NO_ERROR 0x06

namespace
{
	size_t s_bytesReceived = 0;
	size_t s_packetsReceived = 0;
	std::vector<uint8_t> startSimpliciTiCommand = {USB_PACKET_START_BYTE, BM_START_SIMPLICITI, 0x03};
	std::vector<uint8_t> stopSimpliciTiCommand = {USB_PACKET_START_BYTE, BM_STOP_SIMPLICITI, 0x03};
	std::vector<uint8_t> startSimpliciTiCommandResponse = {USB_PACKET_START_BYTE, HW_NO_ERROR, 0x03};
	auto stopSimpliciTiCommandResponse = startSimpliciTiCommandResponse;
	std::vector<uint8_t> usbPacketStartSequence = {USB_PACKET_START_BYTE, HW_NO_ERROR};
}

SimpliciTi::SimpliciTi(HANDLE comHandle, const std::function<void(std::vector<uint8_t>)>& fileLogCallback) : m_fileLogCallback(fileLogCallback)
{
	if (comHandle == INVALID_HANDLE_VALUE || comHandle == nullptr)
		throw std::exception("Invalid handle supplied to the SimpliciTI parser.");

	m_comHandle = comHandle;

	// 1000 bytes for the COM buffer.
	m_comDataBuffer.reserve(10000);
}

void SimpliciTi::startAccessPoint()
{
	FlushFileBuffers(m_comHandle);

/*	std::vector<uint8_t> bullshit1(4);
	bullshit1.push_back(0xFF);
	bullshit1.push_back(0x55);
	bullshit1.push_back(0x03);

	writeCommand(bullshit1);
	std::this_thread::sleep_for(std::chrono::milliseconds(1000));
	readData(true, 3);

	writeCommand(bullshit1);
	std::this_thread::sleep_for(std::chrono::milliseconds(1000));
	readData(true, 3);

	writeCommand(bullshit1);
	std::this_thread::sleep_for(std::chrono::milliseconds(1000));
	readData(true, 3);*/

/*	std::vector<uint8_t> startCommandWithTime(startSimpliciTiCommand.begin(), startSimpliciTiCommand.end());
	startCommandWithTime[USB_PACKET_LENGTH_BYTE_INDEX] = 7;
	auto timeT = std::time(nullptr);

	// Lets imagine some delay value here.
	timeT += 2;
	startCommandWithTime.push_back(static_cast<uint8_t>(timeT & 0x000000FF));
	startCommandWithTime.push_back(static_cast<uint8_t>((timeT & 0x0000FF00) >> 8));
	startCommandWithTime.push_back(static_cast<uint8_t>((timeT & 0x00FF0000) >> 16));
	startCommandWithTime.push_back(static_cast<uint8_t>((timeT & 0xFF000000) >> 24));
*/
	m_comCommandBuffer.erase(m_comCommandBuffer.begin(), m_comCommandBuffer.end());
	m_comCommandBuffer.reserve(startSimpliciTiCommandResponse.size());
	writeCommand(startSimpliciTiCommand);
	while (m_comCommandBuffer.size() < startSimpliciTiCommand.size())
		readData(true, startSimpliciTiCommand.size() - m_comCommandBuffer.size());

	if (m_comCommandBuffer != startSimpliciTiCommandResponse)
		throw std::exception("Starting access point failed. Check the device.");

	m_comCommandBuffer.resize(0);

	m_parseTask = std::thread([&]{
									while (!m_stopParsing)
									{
										//std::this_thread::sleep_for(std::chrono::milliseconds(1));
										parseAndLogPackets();
										std::cout << "\rPackets received: " << s_packetsReceived << ". In total " << s_bytesReceived << " bytes.";
									}
								  });
}

void SimpliciTi::stopAccessPoint()
{
	m_stopParsing = true;

	if (m_parseTask.joinable())
		m_parseTask.join();

	std::cout << "Stopping..." << std::endl;

	FlushFileBuffers(m_comHandle);

	// We do no even care of the response anymore...
	writeCommand(stopSimpliciTiCommand);

	std::this_thread::sleep_for(std::chrono::seconds(1));

	m_comCommandBuffer.erase(m_comCommandBuffer.begin(), m_comCommandBuffer.end());
	m_comCommandBuffer.reserve(stopSimpliciTiCommandResponse.size());
	readData(true, stopSimpliciTiCommandResponse.size());

	if (m_comCommandBuffer != stopSimpliciTiCommandResponse)
		std::cout << "Stopping the access point was not succesful, please restart it manually..." << std::endl;
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

void SimpliciTi::readData(bool isCommand, size_t dataLength)
{
	std::vector<uint8_t>& receiveBuffer = (isCommand ? m_comCommandBuffer : m_comDataBuffer);

	// If there is no room for data, then lets read less.
	auto freeBytes = receiveBuffer.capacity() - receiveBuffer.size();

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

		// Right now could not get more, so break here, lets not block.
		if (bytesRead == 0)
			break;

		bytesReadTotal += bytesRead;
		receiveBuffer.push_back(aByte);
	}

	s_bytesReceived += bytesReadTotal;
}

// The algorithm here searches for the USB packet header and extracts the length. If there are communication
// errors, for example the packet data was not received completely then this is not checked. And probably
// one packet will be corrupt in the log and one or more packets will be discarded. Should have mutex protection
// here, but since nothing besides m_parseTask uses it, it is safe for now.
void SimpliciTi::parseAndLogPackets()
{
	readData(false, 50);

	while (m_comDataBuffer.size() > 0)
	{
		// We do not know the new packet length and we must have atleast the complete header.
		if (m_currentPacketSize == 0)
		{
			// Not enough data to find the header though.
			if (m_comDataBuffer.size() < USB_PACKET_HEADER_LENGTH)
			{
				return;
			}

			// Searching for 0xFF, 0x06.
			auto newPacketBeginning = std::search(m_comDataBuffer.begin(), m_comDataBuffer.end(), usbPacketStartSequence.begin(), usbPacketStartSequence.end());

			// Complete packet header not found.
			if (newPacketBeginning + USB_PACKET_LENGTH_BYTE_INDEX >= m_comDataBuffer.end())
			{
				std::cout << "Packet start not found, discarding " << m_comDataBuffer.size() << " bytes" << std::endl;
				m_comDataBuffer.erase(m_comDataBuffer.begin(), m_comDataBuffer.end());

				return;
			}

			// If this is somehow still zero, then next time new packet will be searched for anyway.
			m_currentPacketSize = *(newPacketBeginning + USB_PACKET_LENGTH_BYTE_INDEX) - USB_PACKET_HEADER_LENGTH;

			if (((newPacketBeginning + USB_PACKET_HEADER_LENGTH) - m_comDataBuffer.begin()) > USB_PACKET_HEADER_LENGTH)
				std::cout << "New packet header found, but discarding more bytes (" << (newPacketBeginning + USB_PACKET_HEADER_LENGTH) - m_comDataBuffer.begin()
				<< ")." << std::endl;

			// Erase all the not useful data and the header so later we could just cut the usable data out.
			m_comDataBuffer.erase(m_comDataBuffer.begin(), newPacketBeginning + USB_PACKET_HEADER_LENGTH);
		}

		if (m_comDataBuffer.size() < m_currentPacketSize)
		{
			return;
		}

		// Lets extract the packet data out.
		m_fileLogCallback(std::vector<uint8_t>(m_comDataBuffer.begin(), m_comDataBuffer.begin() + m_currentPacketSize));
		s_packetsReceived++;

		m_comDataBuffer.erase(m_comDataBuffer.begin(), m_comDataBuffer.begin() + m_currentPacketSize);

		m_currentPacketSize = 0;
	}
}

SimpliciTi::~SimpliciTi()
{
	stopAccessPoint();
}

