#include <string>
#include <random>
#include <algorithm>

namespace seekserve {

std::string generate_auth_token() {
    static const char charset[] =
        "0123456789abcdefghijklmnopqrstuvwxyz";
    const int token_length = 32;

    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<int> dist(0, sizeof(charset) - 2);

    std::string token;
    token.reserve(token_length);
    for (int i = 0; i < token_length; ++i) {
        token += charset[dist(gen)];
    }
    return token;
}

bool validate_auth_token(const std::string& provided, const std::string& expected) {
    if (provided.size() != expected.size()) return false;
    // Constant-time comparison
    volatile int result = 0;
    for (std::size_t i = 0; i < provided.size(); ++i) {
        result |= (provided[i] ^ expected[i]);
    }
    return result == 0;
}

} // namespace seekserve
