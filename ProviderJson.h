#pragma once

#include <string>

struct Msg {
    std::string type;
    std::string sam;
    std::string password;
    std::string reason;
};

void SecureClearString(std::string& s);
void SecureClearMsg(Msg& m);

bool ParseJsonLine(const std::string& line, Msg& out);
