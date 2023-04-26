#include "kspch.h"
#include "StringUtils.h"

namespace Kans::Utils
{

	std::vector<std::string> SplitString(const std::string& token)
	{
		std::vector<std::string> tokens;
		std::string substring = token;
		size_t fpos = substring.find_first_of("#");
		tokens.emplace_back() = substring.substr(0, fpos+1);
		substring = substring.substr(fpos + 1, substring.length());
		fpos = substring.find_first_of(" ");

		while (fpos != std::string::npos)
		{
			tokens.emplace_back() = substring.substr(0, fpos);
			substring = substring.substr(fpos+1, substring.length());
			fpos = substring.find_first_of(" ");
		}
		tokens.emplace_back() = substring;
		return tokens;
	}

}