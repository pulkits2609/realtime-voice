#pragma once

#include "third_party/nlohmann/json.hpp"
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <string>

using Json = nlohmann::json;

constexpr int VoiceProtocolVersion = 2;
constexpr std::size_t MaxControlBytes = 65536;

class ProtocolError : public std::runtime_error{
    public:
        std::string code;
        ProtocolError(const std::string& code, const std::string& message):
            std::runtime_error(message), code(code){}
};

inline std::uint32_t Number(const Json& value, const char* key, bool allowZero = false){
    if(!value.contains(key) || !value[key].is_number_integer()){
        throw ProtocolError("invalid_request", std::string(key) + " must be an integer");
    }
    const auto& number = value[key];
    if((number.is_number_unsigned() && number.get<std::uint64_t>() > UINT32_MAX) ||
       (!number.is_number_unsigned() && number.get<std::int64_t>() < 0)){
        throw ProtocolError("invalid_request", std::string(key) + " is out of range");
    }
    const auto result = number.get<std::uint32_t>();
    if(!allowZero && result == 0){
        throw ProtocolError("invalid_request", std::string(key) + " must be positive");
    }
    return result;
}

inline std::string Text(const Json& value, const char* key, std::size_t maximum = 64){
    if(!value.contains(key) || !value[key].is_string()){
        throw ProtocolError("invalid_request", std::string(key) + " must be a string");
    }
    const auto result = value[key].get<std::string>();
    if(result.empty() || result.size() > maximum){
        throw ProtocolError("invalid_request", std::string(key) + " has an invalid length");
    }
    for(unsigned char character : result){
        if(character < 32 || character == 127){
            throw ProtocolError("invalid_request", std::string(key) + " contains a control character");
        }
    }
    return result;
}

inline bool Boolean(const Json& value, const char* key){
    if(!value.contains(key) || !value[key].is_boolean()){
        throw ProtocolError("invalid_request", std::string(key) + " must be boolean");
    }
    return value[key].get<bool>();
}

inline void ValidateRequest(const Json& request){
    if(!request.is_object()){
        throw ProtocolError("invalid_request", "Expected a JSON object");
    }
    if(!request.contains("v") || !request["v"].is_number_integer() ||
       request["v"] != VoiceProtocolVersion){
        throw ProtocolError("incompatible_version", "This server requires protocol version 2");
    }
    Text(request, "id");
    Text(request, "op");
}

inline Json Success(const Json& request, const Json& data = Json::object()){
    return {{"v", VoiceProtocolVersion}, {"id", request.at("id")},
            {"ok", true}, {"data", data}};
}

inline Json Failure(const Json& request, const std::string& code, const std::string& message){
    Json id = nullptr;
    if(request.is_object() && request.contains("id") && request["id"].is_string() &&
       request["id"].get_ref<const std::string&>().size() <= 64){
        id = request["id"];
    }
    return {{"v", VoiceProtocolVersion}, {"id", id}, {"ok", false},
            {"error", {{"code", code}, {"message", message}}}};
}

inline Json Event(const std::string& name, Json data = Json::object()){
    return {{"v", VoiceProtocolVersion}, {"event", name}, {"data", std::move(data)}};
}
