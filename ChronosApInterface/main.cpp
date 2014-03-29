
#include <Windows.h>

#include <chrono>
#include <cstdint>
#include <ctime>
#include <exception>
#include <fstream>
#include <functional>
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
}

static void fillParameters(int argc, char* argv[]);
static HANDLE createComHandle(const std::string& portName);
static std::string bufferToHex(const std::vector<uint8_t>& buffer);
static void writePacketToFile(const std::vector<uint8_t>& packet);

int main(int argc, char* argv[])
{
	if (argc < 2)
	{
		std::cout << "Not enough parameters provided." << std::endl;
	}

	fillParameters(argc, argv);

	auto comHandle = createComHandle(parameters.at(0));

	if (comHandle == nullptr)
	{
		std::cout << "COM handle creation was not succesful for: " << parameters.at(0) << ". Exiting..." << std::endl;
		return -1;
	}

	auto timeNow = std::time(nullptr);
	auto timeNowTm = std::localtime(&timeNow);

	std::string timeAsString(30, 0);
	auto stringLength = std::strftime(const_cast<char*>(timeAsString.data()), timeAsString.capacity(), "%H_%M_%S", timeNowTm);
	timeAsString.resize(stringLength);
	auto fileName = timeAsString + std::string(" AP output.txt");

	outputFile.open(fileName, std::ios::trunc);
	if (!outputFile.is_open())
	{
		std::cout << "Could not open the output file. Exiting..." << std::endl;
		return -1;
	}

	outputFile << "Start" << std::endl;
	
	try
	{
		SimpliciTi simplicitiParser(comHandle, writePacketToFile);
		simplicitiParser.startAccessPoint();

		int lastCharFromConsole = 0;
		while (lastCharFromConsole != 'x')
		{
			std::this_thread::sleep_for(std::chrono::milliseconds(100));

			lastCharFromConsole = getc(stdin);

		}
		// While not entered x keep looping...
	}
	catch (const std::exception& e)
	{
		std::cout << "Exception caught while running SimpliciTI parser." << std::endl;
		std::cout << e.what() << std::endl;
	}
	catch (...)
	{
		std::cout << "Unknown exception occured. Exiting..." << std::endl;
	}

	CloseHandle(comHandle);
	outputFile.close();

	return 0;
}

static void fillParameters(int argc, char* argv[])
{
	for (uint32_t i = 1; i < (uint32_t)argc; i++)
		parameters.push_back(std::string(argv[i]));
}

static HANDLE createComHandle(const std::string& portName)
{
	HANDLE comHandle = CreateFile(LPCSTR(portName.c_str()),
										(GENERIC_READ | GENERIC_WRITE), 0, 0,
										OPEN_EXISTING, 0, 0);

	if (comHandle == INVALID_HANDLE_VALUE)
	{
		std::cout << "Could not create COM port handle: " << GetLastError() << std::endl;
		return nullptr;
	}

	DCB dcbSerialParameters = {0};
	dcbSerialParameters.DCBlength = sizeof(dcbSerialParameters);
	if (!GetCommState(comHandle, &dcbSerialParameters))
	{
		std::cout << "Getting COM port state failed: " << GetLastError() << std::endl;
		return nullptr;
	}

	dcbSerialParameters.BaudRate = baudrate;
	dcbSerialParameters.ByteSize = 8;
	dcbSerialParameters.StopBits = ONESTOPBIT;
	dcbSerialParameters.Parity = NOPARITY;

	if (!SetCommState(comHandle, &dcbSerialParameters))
	{
		std::cout << "Setting COM port parameters failed: " << GetLastError() << std::endl;
		return nullptr;
	} 

	COMMTIMEOUTS timeouts = {0};
	timeouts.ReadIntervalTimeout = 50 * 20;
	timeouts.ReadTotalTimeoutConstant = 50 * 20;
	timeouts.ReadTotalTimeoutMultiplier = 10 * 20;
	timeouts.WriteTotalTimeoutConstant = 50 * 20;
	timeouts.WriteTotalTimeoutMultiplier = 10 * 20;

	if (!SetCommTimeouts(comHandle, &timeouts))
	{
		std::cout << "Setting COM port timeouts failed: " << GetLastError() << std::endl;
		return nullptr;
	}

	return comHandle;
}

static std::string bufferToHex(const std::vector<uint8_t>& buffer)
{
	std::ostringstream stringBuffer;

	for (auto& aByte : buffer)
		stringBuffer << (uint32_t)aByte << " ";

	return stringBuffer.str();
}

static void writePacketToFile(const std::vector<uint8_t>& packet)
{
	auto packetAsHex = bufferToHex(packet);

	auto timeNow = std::time(nullptr);
	auto timeNowTm = std::localtime(&timeNow);

	std::string timeAsString(30, 0);
	auto stringLength = std::strftime(const_cast<char*>(timeAsString.data()), timeAsString.capacity(), "%H:%M:%S", timeNowTm);
	timeAsString.resize(stringLength);

	outputFile << timeAsString << ", " << packet.size() << " bytes, " << packetAsHex << std::endl;
}