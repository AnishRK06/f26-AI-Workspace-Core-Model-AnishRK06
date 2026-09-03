#include "Message.hpp"

#include <utility>

Message::Message(MessageRole role, std::string text) {
    role_ = role;
    text_ = std::move(text);
}

bool Message::operator==(const Message& other) const {
    return role_ == other.role_ && text_ == other.text_;
}

bool Message::operator!=(const Message& other) const {
    return !(*this == other);
}

MessageRole Message::role() const noexcept {
    return role_;
}

const std::string& Message::text() const noexcept {
    return text_;
}

void Message::setRole(MessageRole role) noexcept {
    role_ = role;
}

void Message::setText(std::string text) {
    text_ = std::move(text);
}

bool Message::empty() const noexcept {
    // text only, every message always has a role
    return text_.empty();
}