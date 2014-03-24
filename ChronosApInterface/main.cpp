
#include <Windows.h>

#include <cstdint>
#include <ctime>
#include <fstream>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>


namespace
{
	std::vector<std::string> parameters;
	DWORD baudrate = 115200;
}

static void fillParameters(int argc, char* argv[]);
static HANDLE createCOMHandle(const std::string& portName);
static std::string bufferToHex(const std::vector<uint8_t>& buffer);

int main(int argc, char* argv[])
{
	if (argc < 2)
	{
		std::cout << "Not enough parameters provided." << std::endl;
	}

	fillParameters(argc, argv);

	std::ofstream outputFile("ap output.txt", std::ios::trunc);
	if (!outputFile.is_open())
	{
		std::cout << "Could not open the output file. Exiting..." << std::endl;
		return -1;
	}

	auto comHandle = createCOMHandle(parameters.at(0));

	if (comHandle == nullptr)
	{
		std::cout << "COM handle creation was not succesful. Exiting..." << std::endl;
		return -1;
	}
	
	std::vector<uint8_t> sendBuffer(100);
	std::vector<uint8_t> receiveBuffer(1000);

	DWORD bytesSent;
	DWORD bytesRead;
	sendBuffer[0] = 0xFF; sendBuffer[1] = 0x07; sendBuffer[2] = 0x03;
	WriteFile(comHandle, sendBuffer.data(), 3, &bytesSent, nullptr);
	ReadFile(comHandle, receiveBuffer.data(), 3, &bytesRead, nullptr);

	outputFile << "Tere!" << std::endl;
	outputFile << bufferToHex(std::vector<uint8_t>(receiveBuffer.begin(), receiveBuffer.begin() + 3)) << std::endl;

	outputFile.close();

	return 0;
}

static void fillParameters(int argc, char* argv[])
{
	for (uint32_t i = 1; i < (uint32_t)argc; i++)
		parameters.push_back(std::string(argv[i]));
}

static HANDLE createCOMHandle(const std::string& portName)
{
	auto tere = LPCSTR(portName.c_str());
	HANDLE comHandle = CreateFile(tere,
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