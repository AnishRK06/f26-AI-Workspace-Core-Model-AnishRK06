#ifndef AIWORKSPACE_DOCUMENT_HPP
#define AIWORKSPACE_DOCUMENT_HPP

#include <cstddef>
#include <string>

class Document {
public:
    Document() = default;
    Document(std::string title, std::string contents);

    Document(const Document&) = default;
    Document(Document&&) noexcept = default;
    Document& operator=(const Document&) = default;
    Document& operator=(Document&&) noexcept = default;
    ~Document() = default;

    bool operator==(const Document& other) const;
    bool operator!=(const Document& other) const;

    bool load(const std::string& path);

    const std::string& title() const noexcept;
    const std::string& sourcePath() const noexcept;
    const std::string& contents() const noexcept;
    void setTitle(std::string title);

    std::size_t characterCount() const noexcept;
    bool empty() const noexcept;

private:
    std::string title_;
    std::string sourcePath_;
    std::string contents_;
};

#endif
