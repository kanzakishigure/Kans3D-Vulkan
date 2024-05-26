#include "kspch.h"
#include "Hash.h"
#include "Kans3D/Core/Base/Base.h"



namespace Kans
{
	namespace HashUtils
	{
		
		inline uint32_t HashFunF(uint32_t x, uint32_t y, uint32_t z)
		{
			return (x & y) | (~x & z);
		}
	
		inline uint32_t HashFunG(uint32_t x, uint32_t y, uint32_t z)
		{
			return (x & z) | (y & ~z);
		}
		inline uint32_t HashFunH(uint32_t x, uint32_t y, uint32_t z)
		{
			return x ^ y ^ z;
		}

		inline uint32_t HashFunI(uint32_t x, uint32_t y, uint32_t z)
		{
			return y ^ (x | ~z);
		}
		inline uint32_t Left_Rotete(uint32_t x,size_t n)
		{
			return (x << n) | (x >> (32 - n));
		}

		
	}


	//specifies the per-round shift amounts
	inline static constexpr  std::array<uint32_t, 64> shiftAmounts = {
			7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22 ,
			5,  9, 14, 20, 5,  9, 14, 20, 5,  9, 14, 20, 5,  9, 14, 20 ,
			4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23 ,
			6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21 };

	inline static  std::vector<uint32_t> sineTable;

	//code reference [https://zh.wikipedia.org/wiki/MD5]
	//Algorithm | Output | Internal | Block   | Length | Word   | Rounds
	//MD5 		| 128bits| 128bits  | 512bits | 64bits | 32bits | 64 
	//NOTE:because MD5 is not a safety hashfunction ,please don't use for encryption
	std::string Hash::GenerateMD5Hash(const std::string& source)
	{
		std::string str = source;
	
		if (sineTable.empty())
		{
			sineTable.resize(64);
			for (size_t i = 0; i < 64; i++)
			{
				sineTable[i] = static_cast<uint32_t>(abs(sin(i + 1)) * pow(2, 32));
			}
		}
		
		//Initialize variables:
		std::vector<uint32_t> state = { 0x67452301,0xEFCDAB89,0x98BADCFE,0x10325476 };
		
		uint32_t sourceBitsize = str.size() * 8;

		char apendHead = 0x80;
		str = str + apendHead;
		
		uint32_t messagebitsize = str.size()*8;
		uint32_t appendsize = messagebitsize%512;

		std::vector<uint8_t>appendmsg;
		size_t count = 0;
		if (appendsize >448)
		{
			count = 64 + (512-appendsize) / 8;
			appendmsg.resize(count);
		}
		else
		{
			count = (512-appendsize) / 8;
			appendmsg.resize(count);
		}
		*(uint64_t*)(&appendmsg[appendmsg.size() - 8]) = sourceBitsize;

		std::string apendStr(appendmsg.begin(), appendmsg.end());

		str = str + apendStr;
		

		size_t chunksize = str.size() / 64;

		size_t chunkIndex = 0;
		while (chunkIndex <chunksize)
		{
			
			std::string chunk = str.substr(64 * chunkIndex, 64);
			std::vector<uint32_t> words(16);
			for (size_t j = 0; j < 16; j++)
			{
				words[j] = *(uint32_t*)(chunk.data() + j * 4);
			}

			uint32_t a = state[0];
			uint32_t b = state[1];
			uint32_t c = state[2];
			uint32_t d = state[3];
			
			uint32_t temp = 0;
			uint32_t f = 0;
			uint32_t g = 0;

			for (int i = 0; i < 64; i++)
			{
				if (i < 16)
				{
					f = HashUtils::HashFunF(b, c, d);
					g = i;
				}
				if (16 <= i && i < 32)
				{
					f = HashUtils::HashFunG(b, c, d);
					g = (5 * i + 1) % 16;
				}
				if (32 <= i && i < 48)
				{
					f = HashUtils::HashFunH(b, c, d);
					g = (3 * i + 5) % 16;
				}
				if (48 <= i && i < 64)
				{
					f = HashUtils::HashFunI(b, c, d);
					g = (7 * i) % 16;
				}
				temp = d;
				d = c;
				c = b;
				b = HashUtils::Left_Rotete((a + f + sineTable[i] + words[g]), shiftAmounts[i]) + b;
				a = temp;
			}
				


			state[0] = state[0] + a;
			state[1] = state[1] + b;
			state[2] = state[2] + c;
			state[3] = state[3] + d;
			
			chunkIndex++;
		}
		
		
#ifdef KS_DEBUG
		std::string digest;
		std::stringstream ss;
		//if we can just stored by 128bit but stored by 256 bit (32 char) for readability
		// in this case if the hash gen in the little endian can't  use for big endian;
		ss << std::hex << state[0] << state[1] << state[2] << state[3];
		digest = ss.str();
#else 
		std::string digest((char*)state.data(), 16);
#endif // KS_DEBUG
		
		
		
		return digest;
	}

	uint64_t  Hash::Generate64MD5Hash(const std::string& source)
	{
		std::string str = source;

		if (sineTable.empty())
		{
			sineTable.resize(64);
			for (size_t i = 0; i < 64; i++)
			{
				sineTable[i] = static_cast<uint32_t>(abs(sin(i + 1)) * pow(2, 32));
			}
		}

		//Initialize variables:
		std::vector<uint32_t> state = { 0x67452301,0xEFCDAB89,0x98BADCFE,0x10325476 };

		uint32_t sourceBitsize = str.size() * 8;

		char apendHead = 0x80;
		str = str + apendHead;

		uint32_t messagebitsize = str.size() * 8;
		uint32_t appendsize = messagebitsize % 512;

		std::vector<uint8_t>appendmsg;
		size_t count = 0;
		if (appendsize > 448)
		{
			count = 64 + (512 - appendsize) / 8;
			appendmsg.resize(count);
		}
		else
		{
			count = (512 - appendsize) / 8;
			appendmsg.resize(count);
		}
		*(uint64_t*)(&appendmsg[appendmsg.size() - 8]) = sourceBitsize;

		std::string apendStr(appendmsg.begin(), appendmsg.end());

		str = str + apendStr;


		size_t chunksize = str.size() / 64;

		size_t chunkIndex = 0;
		while (chunkIndex < chunksize)
		{

			std::string chunk = str.substr(64 * chunkIndex, 64);
			std::vector<uint32_t> words(16);
			for (size_t j = 0; j < 16; j++)
			{
				words[j] = *(uint32_t*)(chunk.data() + j * 4);
			}

			uint32_t a = state[0];
			uint32_t b = state[1];
			uint32_t c = state[2];
			uint32_t d = state[3];

			uint32_t temp = 0;
			uint32_t f = 0;
			uint32_t g = 0;

			for (int i = 0; i < 64; i++)
			{
				if (i < 16)
				{
					f = HashUtils::HashFunF(b, c, d);
					g = i;
				}
				if (16 <= i && i < 32)
				{
					f = HashUtils::HashFunG(b, c, d);
					g = (5 * i + 1) % 16;
				}
				if (32 <= i && i < 48)
				{
					f = HashUtils::HashFunH(b, c, d);
					g = (3 * i + 5) % 16;
				}
				if (48 <= i && i < 64)
				{
					f = HashUtils::HashFunI(b, c, d);
					g = (7 * i) % 16;
				}
				temp = d;
				d = c;
				c = b;
				b = HashUtils::Left_Rotete((a + f + sineTable[i] + words[g]), shiftAmounts[i]) + b;
				a = temp;
			}



			state[0] = state[0] + a;
			state[1] = state[1] + b;
			state[2] = state[2] + c;
			state[3] = state[3] + d;

			chunkIndex++;
		}
		uint64_t result = (uint64_t)(*state.data());
		return result;
	}
}