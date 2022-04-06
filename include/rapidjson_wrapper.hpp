#define RAPIDJSON_HAS_STDSTRING 1
#include "rapidjson/document.h"
#include "rapidjson/writer.h"
#include "rapidjson/stringbuffer.h"
#include <vector>
#include <string>

// rapidjson wrapper
class RJson {
private :
	rapidjson::Value * json = NULL;
public :
	RJson () {}
	RJson (const RJson &rhs) = default;
	RJson (rapidjson::Value &json) : json(&json) {}
	RJson & operator = (const RJson &rhs) = default;
	
	static RJson parse(rapidjson::Document &data, const char *s, std::string &error) {
		if (data.Parse(s).HasParseError()) {
			error = "Parsing error " + std::to_string(data.GetParseError()) + " at " + std::to_string(data.GetErrorOffset());
			return RJson();
		} else {
			error = "";
			return RJson(data);
		}
	}
	static RJson parse_inplace(rapidjson::Document &data, char *s, std::string &error) {
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
	
	template<typename T> void set(rapidjson::Document &json_root, const char *key, const T &value) {
		if (!json || !json->IsObject()) return;
		if (has_key(key)) (*this)[key].json->SetString(value, json_root.GetAllocator());
		else {
			rapidjson::Value value_object(value);
			rapidjson::Value key_object;
			key_object.SetString(key, json_root.GetAllocator());
			json->AddMember(key_object, value_object, json_root.GetAllocator());
		}
	}
	void set_str(rapidjson::Document &json_root, const char *key, const char *value) {
		if (!json || !json->IsObject()) return;
		if (has_key(key)) (*this)[key].json->SetString(value, json_root.GetAllocator());
		else {
			rapidjson::Value value_object;
			value_object.SetString(value, json_root.GetAllocator());
			rapidjson::Value key_object;
			key_object.SetString(key, json_root.GetAllocator());
			json->AddMember(key_object, value_object, json_root.GetAllocator());
		}
	}
	
	RJson operator [] (const char *key) const { return json && json->IsObject() && json->HasMember(key) ? (*json)[key] : RJson(); }
	RJson operator [] (const std::string &str) const { return (*this)[str.c_str()]; }
	RJson operator [] (size_t index) const { return json && json->IsArray() ? (*json)[index] : RJson(); }
	
	std::string dump() const {
		if (!json) return "(null)";
		rapidjson::StringBuffer buffer;
		rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
		json->Accept(writer);
		return buffer.GetString();
	}
};
