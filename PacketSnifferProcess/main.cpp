
#include <algorithm>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>


namespace
{
	const size_t psdPacketSize = 271;
	std::vector<std::string> parameters;

	struct packetData
	{
		std::string destinationAddress;
		std::string sourceAddress;
		uint8_t port;
		uint8_t transactionId;
		std::string dataHex;
		int8_t rssi;
		uint8_t lqi;
		bool fcsOk;
	};
}

static void fillParameters(int argc, char* argv[]);
static struct packetData parsePsd(const std::vector<uint8_t>& packetBinary);

int main(char argc, char* argv[])
{
	if (argc < 2)
	{
		std::cout << "Not enough parameters provided." << std::endl;
	}

	fillParameters(argc, argv);

	std::ifstream inputFile;
	inputFile.open(parameters.at(0), std::ios::binary);
	if (inputFile.good() == false)
	{
		std::cerr << "Input file does not exist. Exiting." << std::endl;
		return -1;
	}

	inputFile.seekg(0, std::ifstream::end);
	size_t packetsExpected = static_cast<size_t>(inputFile.tellg() / psdPacketSize);
	size_t byteError = static_cast<size_t>(inputFile.tellg() % psdPacketSize);
	inputFile.seekg(0);
	std::cout << "Expecting " << packetsExpected << " packets from the file." << std::endl;
	std::cout << "File offset byte count: " << byteError << "." << std::endl;

	std::string outputFileName = parameters.at(0);
	outputFileName.replace(outputFileName.end() - 3, outputFileName.end(), "csv");
	std::ofstream outputFile;
	outputFile.open(outputFileName);

	outputFile << "packetNr,destination,source,port,transactionID,packet,RSSI,LQI,FCS" << std::endl;

	uint32_t packetCount = 1;

	do
	{
		std::vector<uint8_t> packetSnifferPacket(271);

		inputFile.read(reinterpret_cast<char*>(packetSnifferPacket.data()), static_cast<std::streamsize>(packetSnifferPacket.size()));

		if (inputFile.gcount() != psdPacketSize)
		{
			break;
		}

		auto parsedData = parsePsd(packetSnifferPacket);

		std::cout << "\r" << packetCount << " packets parsed.";

		outputFile << packetCount << "," << parsedData.destinationAddress << "," << parsedData.sourceAddress << "," << static_cast<uint32_t>(parsedData.port) << "," 
			<< static_cast<uint32_t>(parsedData.transactionId) << "," << parsedData.dataHex << "," << static_cast<int32_t>(parsedData.rssi) 
			<< "," << static_cast<uint32_t>(parsedData.lqi) << "," << (parsedData.fcsOk ? "OK" : "ERROR") << std::endl;

		packetCount++;
	}
	while (inputFile.eof() == false && inputFile.good() == true);

	inputFile.close();
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

	stringBuffer << "\"";

	for (auto& aByte : buffer)
		stringBuffer << std::hex << std::setw(2) << std::setfill('0') << std::uppercase << static_cast<int>(aByte) << " ";

	auto asString = stringBuffer.str();
	asString.replace(asString.end() - 1, asString.end(), "\"");

	return asString;
}

static struct packetData parsePsd(const std::vector<uint8_t>& packetBinary)
{
	struct packetData packet = {};

	size_t dataLength = packetBinary.at(15);
	packet.destinationAddress = bufferToHex(std::vector<uint8_t>(packetBinary.begin() + 16, packetBinary.begin() + 20));
	packet.sourceAddress = bufferToHex(std::vector<uint8_t>(packetBinary.begin() + 20, packetBinary.begin() + 24));
	packet.port = packetBinary.at(24);
	packet.transactionId = packetBinary.at(26);
	packet.dataHex = "EMPTY";

	size_t applicationDataLength = dataLength - 11;
	if (applicationDataLength > 0 && applicationDataLength <= 50)
	{
		packet.dataHex = bufferToHex(std::vector<uint8_t>(packetBinary.begin() + 27, packetBinary.begin() + (27 + (dataLength - 11))));
	}
	
	// When there are erroneous packets logged.
	if (applicationDataLength > 50)
	{
		packet.fcsOk = false;
		return packet;
	}

	int8_t rawRssi = static_cast<int8_t>(packetBinary.at(27 + applicationDataLength));
	int16_t calculatedRssi = (rawRssi >= 128 ? ((rawRssi - 256) / 2 - 72) : (rawRssi / 2 - 72));
	packet.rssi = (calculatedRssi < -128 ? -128 : calculatedRssi); //std::max(-128, calculatedRssi);
	packet.fcsOk = ((packetBinary.at(27 + applicationDataLength + 1) & 0x80) > 0 ? true : false);
	packet.lqi = packetBinary.at(27 + applicationDataLength + 1) & 0x7F;

	return packet;
}