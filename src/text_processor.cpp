#include "aiws/text_processor.hpp"

namespace aiws {

std::vector<TokenInfo> TextProcessor::tokenize(const std::string&) {
    // TODO: produce normalized tokens with source and paragraph information.
    return {};
}

std::vector<std::string> TextProcessor::terms(const std::string&) {
    // TODO: return the normalized terms represented by the input text.
    return {};
}

std::string TextProcessor::normalize(const std::string&) {
    // TODO: return the normalized form of the input text.
    return {};
}

std::string TextProcessor::join(const std::vector<TokenInfo>&,
                                std::size_t,
                                std::size_t) {
    // TODO: join the requested token range into normalized text.
    return {};
}

std::string TextProcessor::join(const std::vector<std::string>&,
                                std::size_t,
                                std::size_t) {
    // TODO: join the requested term range into normalized text.
    return {};
}

}  // namespace aiws
