#include "Message.hpp"

Message::Message(MessageRole role, std::string text) {
    // TODO: implement according to the M1 specification.

}

bool Message::operator==(const Message& other) const {
    // TODO

}

bool Message::operator!=(const Message& other) const {
    // TODO

}

MessageRole Message::role() const noexcept {
    // TODO
    
    return MessageRole::User;
}

const std::string& Message::text() const noexcept {
    // TODO
    
}

void Message::setRole(MessageRole role) noexcept {
    // TODO
    
}

void Message::setText(std::string text) {
    // TODO
    
}

bool Message::empty() const noexcept {
    // TODO
    
}
