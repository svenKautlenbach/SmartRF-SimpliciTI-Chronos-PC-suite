#pragma once

#include <Windows.h>

#include <atomic>
#include <cstdint>
#include <functional>
#include <thread>
#include <vector>

class SimpliciTi
{
public:
	// COM port handle must be created and set up previously.
	SimpliciTi(HANDLE comHandlem, const std::function<void(std::vector<uint8_t>)>& fileLogCallback);

	// Start must be called before any other operations are called.
	void startAccessPoint();
	void stopAccessPoint();

	~SimpliciTi();

private:

	void writeCommand(const std::vector<uint8_t>& command);
	void readData(bool isCommand, size_t dataLength = 100);

	// Used together in conjuction. Reads the COM port at the background, parses packets
	// ,fills the buffers and logs.
	void parseAndLogPackets();
	std::thread m_parseTask;
	std::atomic<bool> m_stopParsing = false;

	HANDLE m_comHandle;

	size_t m_currentPacketSize = 0;
	std::vector<uint8_t> m_comDataBuffer;
	std::vector<uint8_t> m_comCommandBuffer;

	std::function<void(std::vector<uint8_t>)> m_fileLogCallback;
};