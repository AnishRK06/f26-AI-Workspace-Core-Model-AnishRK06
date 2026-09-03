#include "Document.hpp"

Document::Document(std::string title, std::string contents) {
    // TODO: implement according to the M1 specification.

}

bool Document::operator==(const Document& other) const {
    // TODO

}

bool Document::operator!=(const Document& other) const {
    // TODO

}

bool Document::load(const std::string& path) {
    // TODO

}

const std::string& Document::title() const noexcept {
    // TODO

}

const std::string& Document::sourcePath() const noexcept {
    // TODO

}

const std::string& Document::contents() const noexcept {
    // TODO
  
}

void Document::setTitle(std::string title) {
    // TODO
  
}

std::size_t Document::characterCount() const noexcept {
    // TODO
    
}

bool Document::empty() const noexcept {
    // TODO
   
}
