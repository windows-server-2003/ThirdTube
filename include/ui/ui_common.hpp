#pragma once
#include <string>
#include <functional>
#include "3ds.h"
#include "system/draw/draw.hpp"
#include "system/util/string_resource.hpp"
#include "ui/colors.hpp"

#define SMALL_MARGIN 3
#define DEFAULT_FONT_INTERVAL 13
#define MIDDLE_FONT_INTERVAL 18
#define MIDDLE_FONT_SIZE 0.641

namespace UI {
	template<class CallArg> struct FlexibleString {
		enum class Type {
			RAW,
			FUNC_WITH_ARG,
			FUNC
		};
		Type type;
		std::string value;
		std::function<std::string (const CallArg &)> func_with_arg;
		std::function<std::string ()> func;
		const CallArg *arg;
		
		FlexibleString() : type(Type::RAW) {}
		FlexibleString(const char *str) : type(Type::RAW), value(str) {}
		FlexibleString(const std::string &str) : type(Type::RAW), value(str) {}
		FlexibleString(decltype(func_with_arg) func_with_arg, const CallArg &arg) : type(Type::FUNC_WITH_ARG), func_with_arg(func_with_arg), arg(&arg) {}
		FlexibleString(decltype(func) func) : type(Type::FUNC), func(func) {}
		// FlexibleString(FlexibleString &&) = default;
		// FlexibleString(const FlexibleString &&) = default;
		// FlexibleString(FlexibleString &) = default;
		// FlexibleString(const FlexibleString &) = default;
		
		operator std::string () const {
			if (type == Type::RAW) return value;
			if (type == Type::FUNC_WITH_ARG) return func_with_arg(*arg);
			if (type == Type::FUNC) return func();
			return "FlexStr Unknown Type Err";
		}
		FlexibleString &operator = (const FlexibleString &rhs) = default;
	};
}
