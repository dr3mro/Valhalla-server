#pragma once

#include <jwt-cpp/jwt.h>
#include <jwt-cpp/traits/kazuho-picojson/traits.h>

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>

#include "gatekeeper/types.hpp"
#include "utils/global/requester.hpp"

namespace valhalla_test
{
    using api::v2::Types::ClientLoginData;

    /// ClientLoginData with every field initialised (the production struct
    /// leaves `is_active` / `passwordMatch` uninitialised).
    inline ClientLoginData makeLoginData(const uint64_t clientId = 1, const std::string& group = "users", const std::string& ip = "127.0.0.1")
    {
        ClientLoginData data{};
        data.clientId      = clientId;
        data.group         = group;
        data.username      = "testuser";
        data.ip_address    = ip;
        data.is_active     = true;
        data.passwordMatch = false;
        data.expireTime    = std::chrono::system_clock::now().time_since_epoch() + std::chrono::hours{1};
        return data;
    }

    inline api::v2::Requester makeRequester(const uint64_t id, const std::string& group) { return api::v2::Requester(id, group); }

    /// Issues a JWT the same way TokenManager does (picojson traits, HS256).
    /// Not verified against the production secret unless `secret` matches.
    inline std::string makeToken(const std::string& secret, const std::string& issuer, const std::string& type, const std::string& username, uint64_t clientId,
        const std::string& group, const std::string& ip, const std::string& lastLogoutTime, const std::chrono::minutes validity = std::chrono::minutes{60})
    {
        return jwt::create<jwt::traits::kazuho_picojson>()
            .set_issuer(issuer)
            .set_type(type)
            .set_subject(username)
            .set_id(std::to_string(clientId))
            .set_issued_at(std::chrono::system_clock::now())
            .set_expires_at(std::chrono::system_clock::now() + validity)
            .set_payload_claim("ip_address", jwt::basic_claim<jwt::traits::kazuho_picojson>(ip))
            .set_payload_claim("llodt", jwt::basic_claim<jwt::traits::kazuho_picojson>(lastLogoutTime))
            .set_payload_claim("group", jwt::basic_claim<jwt::traits::kazuho_picojson>(group))
            .sign(jwt::algorithm::hs256{secret});
    }

    /// A token signed with the *wrong* key (for tampering/verification tests).
    inline std::string makeTamperedToken(const std::string& token)
    {
        // Flip a character in the middle of the signature (the third JWT segment,
        // after the last '.'). We deliberately avoid the last character: for a
        // 32-byte HMAC-SHA256 output the signature is 43 base64url chars, and the
        // last char's bottom 2 bits are padding zeros — 'A' and 'B' decode to the
        // same bytes, so flipping them does not actually change the signature.
        std::string       tampered = token;
        const std::size_t dot      = tampered.rfind('.');
        if (dot == std::string::npos || dot + 4 >= tampered.size())
            return tampered;
        char& c = tampered[dot + 4];
        c       = (c == 'A') ? 'Z' : 'A';
        return tampered;
    }
}  // namespace valhalla_test
