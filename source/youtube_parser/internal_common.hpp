#include <string>
#include <map>
#include <vector>
#include "json11/json11.hpp"

#ifdef _WIN32
#	include <iostream> // <------------
#	include <fstream> // <-------
#	include <sstream> // <-------

	typedef uint8_t u8;
	typedef uint16_t u16;
	typedef uint32_t u32;
	typedef uint64_t u64;
	typedef int8_t s8;
	typedef int16_t s16;
	typedef int32_t s32;
	typedef int64_t s64;

#	define debug(s) std::cerr << (s) << std::endl
#else // if it's a 3ds...
#	include "headers.hpp"
#	define debug(s) Util_log_save("yt-parser", (s));
#endif


using namespace json11;

namespace youtube_parser {
	std::string http_get(const std::string &url, std::map<std::string, std::string> header = {});
	std::string http_post_json(const std::string &url, const std::string &json);
	
	std::string url_decode(std::string input);

	// parse something like 'abc=def&ghi=jkl&lmn=opq'
	std::map<std::string, std::string> parse_parameters(std::string input);

	std::string get_text_from_object(Json json);

	// html can contain unnecessary garbage at the end of the actual json data
	Json to_json(const std::string &html, size_t start);

	Json get_succeeding_json_regexes(const std::string &html, std::vector<const char *> patterns);

	std::string convert_url_to_mobile(std::string url);
	std::string convert_url_to_desktop(std::string url);
}
using namespace youtube_parser;

