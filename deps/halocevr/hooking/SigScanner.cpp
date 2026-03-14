#include <vector>
#include <sstream>
#include <iostream>
#include "sigscanner.h"
#include "../Logger.h"

long long SigScanner::UpdateOffset(Offset& offset, bool bErrorOnFail)
{
	std::vector<int> Signature;

	HMODULE CurrentModule = GetModuleHandleA(nullptr);


	if (!CurrentModule)
	{
		if (bErrorOnFail)
		{
			Logger::err << "[SigScanner] [FAIL]:" << offset.DebugName << " can't be located as the default module could not be found!" << std::endl;
		}
		return -1;
	}

	MODULEINFO ModuleInfo;

	BOOL Res = GetModuleInformation(GetCurrentProcess(), CurrentModule, &ModuleInfo, sizeof(ModuleInfo));

	if (!Res)
	{
		if (bErrorOnFail)
		{
			Logger::err << "[SigScanner] [FAIL]:" << offset.DebugName << " can't be validated as GetModuleInformation failed " << std::endl;
		}
		return -1;
	}

	if (offset.Signature.size() == 0)
	{
		offset.Address = (uintptr_t)CurrentModule + offset.Offset;
		Logger::log << "[SigScanner] [WARN]: Skipping signature check for " << offset.DebugName << " and using offset 0x" << std::hex << offset.Offset << std::dec << ", this should only be done during dev builds!" << std::endl;
		return 0;
	}

	std::stringstream SigStream(offset.Signature);
	std::string SigByte;
	while (SigStream >> SigByte)
	{
		if (SigByte == "?" || SigByte == "??")
		{
			Signature.push_back(-1);
		}
		else
		{
			Signature.push_back(strtoul(SigByte.c_str(), NULL, 16));
		}
	}

	uint8_t* bytes = (uint8_t*)ModuleInfo.lpBaseOfDll;

	bool bNeedsUpdating = false;

	// Check if the current offset is fine
	for (size_t i = 0; i < Signature.size(); i++)
	{
		if (Signature[i] != -1 && bytes[offset.Offset + offset.SignatureOffset + i] != Signature[i])
		{
			bNeedsUpdating = true;
			break;
		}
	}

	if (!bNeedsUpdating)
	{
		offset.Address = (uintptr_t)CurrentModule + offset.Offset;
		Logger::log << "[SigScanner] [PASS]: " << offset.DebugName << "(" + offset.Signature + ") - signature found at 0x" << std::hex << offset.Offset << std::dec << std::endl;
		return 0;
	}
	
	if (bErrorOnFail)
	{
		Logger::log << "[SigScanner] [WARN]: " << offset.DebugName << " is outdated, scanning from 0x" << 0 << " to 0x" << std::hex << ModuleInfo.SizeOfImage << std::dec << std::endl;
	}

	// Scan the dll for the signature
	for (DWORD i = 0; i < ModuleInfo.SizeOfImage; i++)
	{
		bool bFound = true;
		for (size_t j = 0; j < Signature.size(); j++)
		{
			if (Signature[j] != -1 && bytes[i + j] != Signature[j])
			{
				bFound = false;
				break;
			}
		}
		if (bFound)
		{
			offset.Offset = i - offset.SignatureOffset;
			offset.Address = (uintptr_t)CurrentModule + offset.Offset;
			Logger::log << "[SigScanner] [PASS]: " << offset.DebugName << "(" + offset.Signature + ") found! New offset is 0x" << std::hex << offset.Offset << std::dec << std::endl;
			return offset.Offset;
		}
	}

	if (bErrorOnFail)
	{
		Logger::err << "Signature for code offset " << offset.DebugName << "(" + offset.Signature + ") could not be found!" << std::endl;
	}
	offset.Offset = -1;
	return -1;
}
