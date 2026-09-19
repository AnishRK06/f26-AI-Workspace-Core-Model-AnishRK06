#include "aiws/text_processor.hpp"

#include <algorithm>
#include <utility>

namespace aiws {

namespace {

// ascii only on purpose, isalpha/tolower depend on locale and high-bit bytes
bool is_letter(unsigned char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

bool is_digit(unsigned char c) {
    return c >= '0' && c <= '9';
}

bool is_token_byte(unsigned char c) {
    return is_letter(c) || is_digit(c);
}

char to_lower(unsigned char c) {
    if (c >= 'A' && c <= 'Z') {
        return static_cast<char>(c - 'A' + 'a');
    }
    return static_cast<char>(c);
}

// \r counts as line whitespace so CRLF blank lines look the same as LF ones
bool is_line_space(unsigned char c) {
    return c == ' ' || c == '\t' || c == '\r';
}

}  // namespace

// single pass over the bytes, everything else in this file builds on this
std::vector<TokenInfo> TextProcessor::tokenize(const std::string& text) {
    std::vector<TokenInfo> tokens;
    std::size_t paragraph = 0;

    // true while only spaces/tabs have followed the last newline
    bool after_newline = false;
    // saw a blank line somewhere between the previous token and now
    bool blank_line = false;

    std::size_t i = 0;
    while (i < text.size()) {
        unsigned char c = static_cast<unsigned char>(text[i]);

        if (is_token_byte(c)) {
            // several blank lines in a row still only count as one boundary,
            // and blank lines before the first token dont start a new paragraph
            if (blank_line && !tokens.empty()) {
                ++paragraph;
            }
            blank_line = false;
            after_newline = false;

            TokenInfo token;
            token.begin = i;
            while (i < text.size() &&
                   is_token_byte(static_cast<unsigned char>(text[i]))) {
                token.token += to_lower(static_cast<unsigned char>(text[i]));
                ++i;
            }
            token.end = i;
            token.paragraph = paragraph;
            tokens.push_back(std::move(token));
            continue;
        }

        if (c == '\n') {
            if (after_newline) {
                blank_line = true;
            }
            after_newline = true;
        } else if (!is_line_space(c)) {
            // punctuation on the line means it isnt blank
            after_newline = false;
        }
        ++i;
    }

    return tokens;
}

std::vector<std::string> TextProcessor::terms(const std::string& text) {
    std::vector<TokenInfo> tokens = tokenize(text);
    std::vector<std::string> result;
    result.reserve(tokens.size());
    for (TokenInfo& token : tokens) {
        result.push_back(std::move(token.token));
    }
    return result;
}

// goes through tokenize so documents and queries can never normalize differently
std::string TextProcessor::normalize(const std::string& text) {
    std::vector<std::string> words = terms(text);
    return join(words, 0, words.size());
}

// [begin, end), out of range ends get clamped instead of throwing
std::string TextProcessor::join(const std::vector<TokenInfo>& tokens,
                                std::size_t begin,
                                std::size_t end) {
    end = std::min(end, tokens.size());
    std::string result;
    for (std::size_t i = begin; i < end; ++i) {
        if (i != begin) {
            result += ' ';
        }
        result += tokens[i].token;
    }
    return result;
}

std::string TextProcessor::join(const std::vector<std::string>& tokens,
                                std::size_t begin,
                                std::size_t end) {
    end = std::min(end, tokens.size());
    std::string result;
    for (std::size_t i = begin; i < end; ++i) {
        if (i != begin) {
            result += ' ';
        }
        result += tokens[i];
    }
    return result;
}

}  // namespace aiws