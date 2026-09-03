#ifndef AIWORKSPACE_MESSAGE_HPP
#define AIWORKSPACE_MESSAGE_HPP

#include <string>

enum class MessageRole {
    System,
    User,
    Assistant
};

class Message {
public:
    Message() = default;
    Message(MessageRole role, std::string text);

    Message(const Message&) = default;
    Message(Message&&) noexcept = default;
    Message& operator=(const Message&) = default;
    Message& operator=(Message&&) noexcept = default;
    ~Message() = default;

    bool operator==(const Message& other) const;
    bool operator!=(const Message& other) const;

    MessageRole role() const noexcept;
    const std::string& text() const noexcept;
    void setRole(MessageRole role) noexcept;
    void setText(std::string text);
    bool empty() const noexcept;

private:
    MessageRole role_ = MessageRole::User;
    std::string text_;
};

#endif
