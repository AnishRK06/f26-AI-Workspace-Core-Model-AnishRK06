#include "Prompt.hpp"

#include <utility>

Prompt::Prompt(std::string title, std::string text) {
    title_ = std::move(title);
    text_ = std::move(text);
}

bool Prompt::operator==(const Prompt& other) const {
    return title_ == other.title_ && text_ == other.text_;
}

bool Prompt::operator!=(const Prompt& other) const {
    return !(*this == other);
}

const std::string& Prompt::title() const noexcept {
    return title_;
}

const std::string& Prompt::text() const noexcept {
    return text_;
}

void Prompt::setTitle(std::string title) {
    title_ = std::move(title);
}

void Prompt::setText(std::string text) {
    text_ = std::move(text);
}

bool Prompt::empty() const noexcept {
    // text only, a titled prompt with no text is still empty
    return text_.empty();
}