#ifndef AIWORKSPACE_WORKSPACE_HPP
#define AIWORKSPACE_WORKSPACE_HPP

#include <cstddef>
#include <string>
#include <vector>

#include "Document.hpp"
#include "Message.hpp"
#include "Prompt.hpp"

class Workspace {
public:
    Workspace() = default;
    explicit Workspace(std::string name);

    Workspace(const Workspace&) = default;
    Workspace(Workspace&&) noexcept = default;
    Workspace& operator=(const Workspace&) = default;
    Workspace& operator=(Workspace&&) noexcept = default;
    ~Workspace() = default;

    bool operator==(const Workspace& other) const;
    bool operator!=(const Workspace& other) const;

    const std::string& name() const noexcept;
    void setName(std::string name);

    void addDocument(Document document);
    void addPrompt(Prompt prompt);
    void addMessage(Message message);

    std::size_t documentCount() const noexcept;
    std::size_t promptCount() const noexcept;
    std::size_t messageCount() const noexcept;

    Document& documentAt(std::size_t index);
    const Document& documentAt(std::size_t index) const;
    Prompt& promptAt(std::size_t index);
    const Prompt& promptAt(std::size_t index) const;
    Message& messageAt(std::size_t index);
    const Message& messageAt(std::size_t index) const;

private:
    std::string name_;
    std::vector<Document> documents_;
    std::vector<Prompt> prompts_;
    std::vector<Message> messages_;
};

#endif
