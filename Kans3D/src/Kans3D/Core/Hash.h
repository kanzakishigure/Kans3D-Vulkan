#pragma once

namespace Kans
{
	// hash function interface
	class Hash
	{
	public: 
		static void Init();
		static void Shutdown();

		static  std::string GenerateMD5Hash(const std::string& source);
		static  uint64_t Generate64MD5Hash(const std::string& source);
		
	};

}