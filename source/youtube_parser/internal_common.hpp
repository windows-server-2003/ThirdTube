#include <string>
#include <map>
#include <vector>
#include "rapidjson/document.h"
#include "rapidjson/writer.h"
#include "rapidjson/stringbuffer.h"

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
#	include "types.hpp"
#	include "system/util/log.hpp"
#	include "system/util/file.hpp"
#	include "system/util/util.hpp"
#	include "system/util/history.hpp"
#	include "system/util/misc_tasks.hpp"
#	include "system/cpu_limit.hpp"
#	include "network/network_io.hpp"
#	include "definitions.hpp"
#	define debug(s) Util_log_save("yt-parser", (s))
#endif


using namespace rapidjson;

namespace youtube_parser {
	using rapidjson_value_t = GenericValue<UTF8<> >;
	// rapidjson wrapper
	class RJson {
	private :
		rapidjson_value_t * json = NULL;
	public :
		RJson () {}
		RJson (const RJson &rhs) = default;
		RJson (rapidjson_value_t &json) : json(&json) {}
		RJson & operator = (const RJson &rhs) = default;
		
		static RJson parse(Document &data, const char *s, std::string &error) {
			if (data.Parse(s).HasParseError()) {
				error = "Parsing error " + std::to_string(data.GetParseError()) + " at " + std::to_string(data.GetErrorOffset());
				return RJson();
			} else {
				error = "";
				return RJson(data);
			}
		}
		static RJson parse_inplace(Document &data, char *s, std::string &error) {
			if (data.ParseInsitu(s).HasParseError()) {
				error = "Parsing error " + std::to_string(data.GetParseError()) + " at " + std::to_string(data.GetErrorOffset());
				return RJson();
			} else {
				error = "";
				return RJson(data);
			}
		}
		
		bool is_valid() const { return json != NULL; }
		bool has_key(const std::string &str) const { return (*this)[str].is_valid(); }
		
		const char *cstring_value() const { return json && json->IsString() ? json->GetString() : ""; }
		std::string string_value() const { return cstring_value(); }
		int int_value() const { return json && json->IsInt() ? json->GetInt() : 0; }
		bool bool_value() const { return json && json->IsBool() ? json->GetBool() : false; }
		std::vector<RJson> array_items() const {
			if (!json || !json->IsArray()) return {};
			const auto &array = json->GetArray();
			return std::vector<RJson>(array.Begin(), array.End());
		}
		
		void set_str(Document &json_root, const char *key, const char *value) {
			if (!json || !json->IsObject()) return;
			if (has_key(key)) (*this)[key].json->SetString(value, json_root.GetAllocator());
			else {
				Value value_object;
				value_object.SetString(value, json_root.GetAllocator());
				Value key_object;
				key_object.SetString(key, json_root.GetAllocator());
				json->AddMember(key_object, value_object, json_root.GetAllocator());
			}
		}
		
		RJson operator [] (const char *key) const { return json && json->IsObject() && json->HasMember(key) ? (*json)[key] : RJson(); }
		RJson operator [] (const std::string &str) const { return (*this)[str.c_str()]; }
		RJson operator [] (size_t index) const { return json && json->IsArray() ? (*json)[index] : RJson(); }
		
		std::string dump() const {
			if (!json) return "(null)";
			StringBuffer buffer;
			Writer<StringBuffer> writer(buffer);
			json->Accept(writer);
			return buffer.GetString();
		}
	};
	RJson get_error_json(const std::string &error);
	
	// internal state
	extern std::string language_code;
	extern std::string country_code;
	extern std::string innertube_key;
	extern std::string base_js_url;
	extern int sts;
	extern bool quick_mode;
	
#	ifndef _WIN32
	extern NetworkSessionList thread_network_session_list;
	HttpRequest http_get_request(const std::string &url, std::map<std::string, std::string> headers = {});
	HttpRequest http_post_json_request(const std::string &url, const std::string &json, std::map<std::string, std::string> headers = {});
#	endif
	std::string http_get(const std::string &url, std::map<std::string, std::string> header = {});
	std::string http_post_json(const std::string &url, const std::string &json, std::map<std::string, std::string> header = {});
	
	bool starts_with(const std::string &str, const std::string &pattern, size_t offset = 0);
	bool ends_with(const std::string &str, const std::string &pattern);
	
	std::string url_decode(std::string input);
	
	// parse something like 'abc=def&ghi=jkl&lmn=opq'
	std::map<std::string, std::string> parse_parameters(std::string input);

	std::string get_text_from_object(RJson json);

	// str[0] must be '(', '[', '{', or '\''
	// returns the prefix of str until the corresponding parenthesis or quote of str[0]
	std::string remove_garbage(const std::string &str, size_t start);
	// html can contain unnecessary garbage at the end of the actual json data
	RJson to_json(Document &json_root, const std::string &html, size_t start);

	// search for `var_name` = ' or `var_name` = {
	bool fast_extract_initial(Document &json_root, const std::string &html, const std::string &var_name, RJson &res);
	
	RJson get_succeeding_json_regexes(Document &json_root, const std::string &html, std::vector<const char *> patterns);
	
	
	// parses `str` as json and calls `on_success` or `on_fail` based on the result of the parsing
	// the content of `str` will be modified
	template<class Func1, class Func2>
	void parse_json_destructive(char *str, const Func1 &on_success, const Func2 &on_fail) {
		std::string json_err;
		Document json_root;
		RJson data = RJson::parse_inplace(json_root, str, json_err);
		if (json_err != "") on_fail(json_err);
		else on_success(json_root, data); // both `json_root` and `str` is alive at this point
	}
	// calls `access` to fetch json and calls `parse` to parse the json
	// properly handles the lifetime of json objects
	template<class Func1, class Func2, class Func3>
	void access_and_parse_json(const Func1 &access, const Func2 &on_success, const Func3 &on_fail) {
		std::string received_str = access();
		parse_json_destructive(&received_str[0], on_success, on_fail);
	}
	
	std::string convert_url_to_mobile(std::string url);
	std::string convert_url_to_desktop(std::string url);
	
	void fetch_innertube_key_and_player();
	
	
}
using namespace youtube_parser;

