#include "Network/protocol.hpp"
#include <iostream>

int main(){
    try{
        Json valid = {{"v", 2}, {"id", "one"}, {"op", "hello"}, {"team", 2}};
        ValidateRequest(valid);
        if(Number(valid, "team") != 2 || Success(valid)["id"] != "one") return 1;
        auto rejects = [](const Json& value, const std::string& code){
            try{ ValidateRequest(value); }
            catch(const ProtocolError& error){ return error.code == code; }
            return false;
        };
        if(!rejects(Json::array(), "invalid_request")) return 1;
        valid["v"] = 1;
        if(!rejects(valid, "incompatible_version")) return 1;
        valid["v"] = 2;
        valid["id"] = std::string(65, 'x');
        if(!rejects(valid, "invalid_request")) return 1;
        for(Json number : {Json(-1), Json(true), Json(1.5), Json(UINT64_MAX)}){
            try{ Number(Json{{"team", number}}, "team"); return 1; }
            catch(const ProtocolError&){}
        }
        try{ Boolean(Json{{"muted", 1}}, "muted"); return 1; }
        catch(const ProtocolError&){}
        if(Failure(Json::array(), "bad", "bad")["id"] != nullptr) return 1;
        std::cout << "Protocol validation tests passed\n";
    }
    catch(const std::exception& error){
        std::cerr << error.what() << '\n'; return 1;
    }
}

