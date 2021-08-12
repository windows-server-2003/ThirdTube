#pragma once
#include <vector>
#include <string>

enum class NParamFunctionType {
	ROTATE_RIGHT,
	REVERSE,
	PUSH,
	SWAP,
	CIPHER,
	SPLICE,
};
struct NParamCArrayContent {
	enum class Type {
		INTEGER,
		STRING,
		N, // reference to 'n' (the target of the modification)
		SELF, // reference to CArray itself
		FUNCTION,
	};
	Type type;
	int64_t integer;
	std::vector<char> string;
	NParamFunctionType function;
	std::string function_internal_arg; // for NParamFunctionType::CIPHER
	
	std::string to_string() {
		if (type == Type::INTEGER) return "int " + std::to_string(integer);
		if (type == Type::STRING) return "str " + std::string(string.begin(), string.end());
		if (type == Type::N) return "ref to n";
		if (type == Type::SELF) return "ref to c";
		if (type == Type::FUNCTION) return "func type : " + std::to_string((int) function);
		return "unknown";
	}
};
struct yt_nparam_transform_procedure {
	std::vector<NParamCArrayContent> c;
	std::vector<std::pair<int, std::pair<int, int> > > ops; // {func, {arg0, arg1}}
};

yt_nparam_transform_procedure yt_nparam_get_transform_plan(const std::string &js);
std::string yt_modify_nparam(std::string n_param, const yt_nparam_transform_procedure &transform_plan);
