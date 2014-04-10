#include "simpliciti.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <ctime>
#include <exception>
#include <iostream>

#include "BM_Driver.h"

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

SimpliciTi::SimpliciTi(const std::string& comPortName, const std::function<void(std::vector<uint8_t>)>& fileLogCallback) : m_fileLogCallback(fileLogCallback)
{
	if (!OpenCOM(0, const_cast<char*>(comPortName.c_str())))
		throw std::exception("Invalid handle supplied to the SimpliciTI parser.");

	// 1000 bytes for the COM buffer.
	m_comDataBuffer.reserve(10000);
	m_comCommandBuffer.reserve(100);
}

void SimpliciTi::startAccessPoint()
{
	FlushCOM(0);

	writeCommand(startSimpliciTiCommand);

	std::this_thread::sleep_for(std::chrono::milliseconds(100));

	m_comCommandBuffer.resize(0);
	readData(true, startSimpliciTiCommandResponse.size());

	if (m_comCommandBuffer != startSimpliciTiCommandResponse)
		throw std::exception("Starting access point failed. Check the device.");

	m_accessPointOn = true;
	m_stopParsing = false;

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
	if (m_accessPointOn == false)
		return;

	m_stopParsing = true;
	m_accessPointOn = false;

	if (m_parseTask.joinable())
		m_parseTask.join();

	std::cout << std::endl << "Stopping..." << std::endl;

	FlushCOM(0);

	// We do no even care of the response anymore...
	writeCommand(stopSimpliciTiCommand);

	std::this_thread::sleep_for(std::chrono::milliseconds(100));

	m_comCommandBuffer.resize(0);
	readData(true, stopSimpliciTiCommandResponse.size());

	if (m_comCommandBuffer != stopSimpliciTiCommandResponse)
		std::cout << "Stopping the access point did not return any message, please check if the led is still blinking..." << std::endl;
}

void SimpliciTi::writeCommand(const std::vector<uint8_t>& command)
{
	if (!WriteCOM(0, command.size(), const_cast<UCHAR*>(command.data())))
	{
		throw std::exception("Failed to send command to the access point.");
	}
}

void SimpliciTi::readData(bool isCommand, size_t dataLength)
{
	std::array<uint8_t, 100> buffer;
	std::vector<uint8_t>& destinationBuffer = (isCommand ? m_comCommandBuffer : m_comDataBuffer);

	// If there is no room for data, then lets read less.
	auto freeBytes = std::min(destinationBuffer.capacity() - destinationBuffer.size(), buffer.size());
	size_t bufferSize = std::min(freeBytes, dataLength);

	size_t readBytes = ReadCOM(0, bufferSize, buffer.data());

	for (size_t i = 0; i < readBytes; i++)
	{
		destinationBuffer.push_back(buffer[i]);
	}

	s_bytesReceived += readBytes;
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

	CloseCOM(0);
}

