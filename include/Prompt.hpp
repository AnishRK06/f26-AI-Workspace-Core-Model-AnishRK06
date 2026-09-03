#ifndef AIWORKSPACE_PROMPT_HPP
#define AIWORKSPACE_PROMPT_HPP

#include <string>

class Prompt {
public:
    Prompt() = default;
    Prompt(std::string title, std::string text);

    Prompt(const Prompt&) = default;
    Prompt(Prompt&&) noexcept = default;
    Prompt& operator=(const Prompt&) = default;
    Prompt& operator=(Prompt&&) noexcept = default;
    ~Prompt() = default;

    bool operator==(const Prompt& other) const;
    bool operator!=(const Prompt& other) const;

    const std::string& title() const noexcept;
    const std::string& text() const noexcept;
    void setTitle(std::string title);
    void setText(std::string text);
    bool empty() const noexcept;

private:
    std::string title_;
    std::string text_;
};

#endif
