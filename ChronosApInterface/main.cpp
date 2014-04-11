
#include <Windows.h>

#include <chrono>
#include <cstdint>
#include <ctime>
#include <exception>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include "simpliciti.h"

namespace
{
	std::vector<std::string> parameters;
	std::ofstream outputFile;
	DWORD baudrate = 115200;

	enum class blobFormat
	{
		number,
		hex,
		ascii,
	};

	blobFormat dataBlobFormat = blobFormat::number;
}

static void fillParameters(int argc, char* argv[]);
static std::string bufferToHex(const std::vector<uint8_t>& buffer);
static void writePacketToFile(const std::vector<uint8_t>& packet);

int main(int argc, char* argv[])
{
	if (argc < 2)
	{
		std::cout << "Not enough parameters provided." << std::endl;
	}

	fillParameters(argc, argv);

	if (parameters.size() > 1)
	{
		auto blobFormatParameter = parameters.at(1);

		if (blobFormatParameter == "hex")
		{
			dataBlobFormat = blobFormat::hex;
		}
		else if (blobFormatParameter == "ascii")
		{
			dataBlobFormat = blobFormat::ascii;
		}
		else
		{
			dataBlobFormat = blobFormat::number;
		}
	}

	auto timeNow = std::time(nullptr);
	auto timeNowTm = std::localtime(&timeNow);

	std::string timeAsString(50, 0);
	auto stringLength = std::strftime(const_cast<char*>(timeAsString.data()), timeAsString.capacity(), "%Y %m %d %H_%M_%S", timeNowTm);
	timeAsString.resize(stringLength);
	auto fileName = timeAsString + std::string(" AP output.txt");

	outputFile.open(fileName, std::ios::trunc);
	if (!outputFile.is_open())
	{
		std::cout << "Could not open the output file. Exiting..." << std::endl;
		return -1;
	}
	
	try
	{
		std::string comName = "\\\\.\\COM" + parameters.at(0);
		SimpliciTi simplicitiParser(comName, writePacketToFile);
		simplicitiParser.startAccessPoint();

		auto timeNow2 = std::time(nullptr);
		auto timeNowTm2 = std::localtime(&timeNow2);
		std::string timeAsString2(50, 0);
		auto stringLength = std::strftime(const_cast<char*>(timeAsString2.data()), timeAsString2.capacity(), "%c", timeNowTm2);
		timeAsString2.resize(stringLength);
		outputFile << "Start @ " << timeAsString2 << std::endl;

		int lastCharFromConsole = 0;
		while (lastCharFromConsole != 'x')
		{
			std::this_thread::sleep_for(std::chrono::milliseconds(100));

			lastCharFromConsole = getc(stdin);
		}
		// While not entered x keep looping...

		simplicitiParser.stopAccessPoint();
	}
	catch (const std::exception& e)
	{
		std::cout << "Exception caught while running SimpliciTI parser." << std::endl;
		std::cout << e.what() << std::endl;
		std::cout << "GetLastError():" << GetLastError() << std::endl;
	}
	catch (...)
	{
		std::cout << "Unknown exception occured. Exiting..." << std::endl;
	}

	outputFile.close();

	return 0;
}

static void fillParameters(int argc, char* argv[])
{
	for (uint32_t i = 1; i < (uint32_t)argc; i++)
		parameters.push_back(std::string(argv[i]));
}

static std::string bufferToHex(const std::vector<uint8_t>& buffer)
{
	std::ostringstream stringBuffer;

	for (auto& aByte : buffer)
		stringBuffer << std::hex << std::setw(2) << std::setfill('0') << std::uppercase << static_cast<int>(aByte) << " ";

	return stringBuffer.str();
}

static std::string bufferToAscii(const std::vector<uint8_t>& buffer)
{
	std::ostringstream stringBuffer;

	for (auto& aByte : buffer)
		stringBuffer << static_cast<char>(aByte) << " ";

	return stringBuffer.str();
}

static std::string bufferToNumber(const std::vector<uint8_t>& buffer)
{
	std::ostringstream stringBuffer;

	for (auto& aByte : buffer)
		stringBuffer << static_cast<uint32_t>(aByte) << " ";

	return stringBuffer.str();
}

static void writePacketToFile(const std::vector<uint8_t>& packet)
{
	time_t timestamp = 0;
	timestamp |= (0x000000FF & packet[1]);
	timestamp |= ((0x000000FF & packet[2]) << 8);
	timestamp |= ((0x000000FF & packet[3]) << 16);
	timestamp |= ((0x000000FF & packet[4]) << 24);

	auto timestampTm = std::localtime(&timestamp);
	std::string timestampAsString(30, 0);
	auto timestampLength = std::strftime(const_cast<char*>(timestampAsString.data()), timestampAsString.capacity(), "%c", timestampTm);
	timestampAsString.resize(timestampLength);

	uint16_t milliseconds = 0;
	milliseconds |= (0x00FF & packet[5]);
	milliseconds |= ((0x00FF & packet[6]) << 8);

	std::string formattedBlob;
	auto packetBlob = std::vector<uint8_t>(packet.begin() + 7, packet.end());
	switch (dataBlobFormat)
	{
	case blobFormat::ascii:
		formattedBlob = bufferToAscii(packetBlob);
		break;
	case blobFormat::hex:
		formattedBlob = bufferToHex(packetBlob);
		break;
	case blobFormat::number:
		formattedBlob = bufferToNumber(packetBlob);
		break;
	default:
			throw std::exception("No BLOB format specified - programming error.");
		break;
	}

	auto timeNow = std::time(nullptr);
	auto timeNowTm = std::localtime(&timeNow);

	std::string timeAsString(30, 0);
	auto stringLength = std::strftime(const_cast<char*>(timeAsString.data()), timeAsString.capacity(), "%H:%M:%S", timeNowTm);
	timeAsString.resize(stringLength);

	outputFile << timeAsString << ", " << packet.size() << " bytes," << " link " << static_cast<uint32_t>(packet[0]) << ", " << timestampAsString
		<< ";" << milliseconds << ", " << formattedBlob << std::endl;
}