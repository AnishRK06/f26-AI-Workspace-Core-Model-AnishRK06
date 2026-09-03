#include "Document.hpp"

#include <fstream>
#include <ios>
#include <iterator>
#include <utility>

Document::Document(std::string title, std::string contents) {
    // sourcePath_ stays empty, this one did not come from a file
    title_ = std::move(title);
    contents_ = std::move(contents);
}

bool Document::operator==(const Document& other) const {
    return title_ == other.title_ &&
           sourcePath_ == other.sourcePath_ &&
           contents_ == other.contents_;
}

bool Document::operator!=(const Document& other) const {
    return !(*this == other);
}

bool Document::load(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return false;
    }

    // read into a local, only commit if it worked
    std::string data;
    try {
        data.assign(std::istreambuf_iterator<char>(in),
                    std::istreambuf_iterator<char>());
    } catch (const std::ios_base::failure&) {
        // opening a directory succeeds but reading it throws
        return false;
    }
    if (in.bad()) {
        return false;
    }

    // title is the last path component, "text/sample.txt" -> "sample.txt"
    std::string::size_type slash = path.find_last_of("/\\");
    if (slash == std::string::npos) {
        title_ = path;
    } else {
        title_ = path.substr(slash + 1);
    }

    contents_ = std::move(data);
    sourcePath_ = path;
    return true;
}

const std::string& Document::title() const noexcept {
    return title_;
}

const std::string& Document::sourcePath() const noexcept {
    return sourcePath_;
}

const std::string& Document::contents() const noexcept {
    return contents_;
}

void Document::setTitle(std::string title) {
    title_ = std::move(title);
}

std::size_t Document::characterCount() const noexcept {
    return contents_.size();
}

bool Document::empty() const noexcept {
    // emptiness is about contents, not the title
    return contents_.empty();
}