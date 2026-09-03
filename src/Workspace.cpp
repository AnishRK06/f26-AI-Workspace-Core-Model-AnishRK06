#include "Workspace.hpp"

Workspace::Workspace(std::string name) {
    // TODO: implement according to the M1 specification.
  
}

bool Workspace::operator==(const Workspace& other) const {
    // TODO

}

bool Workspace::operator!=(const Workspace& other) const {
    // TODO

}

const std::string& Workspace::name() const noexcept {
    // TODO

}

void Workspace::setName(std::string name) {
    // TODO

}

void Workspace::addDocument(Document document) {
    // TODO

}

void Workspace::addPrompt(Prompt prompt) {
    // TODO

}

void Workspace::addMessage(Message message) {
    // TODO

}

std::size_t Workspace::documentCount() const noexcept {
    // TODO

}

std::size_t Workspace::promptCount() const noexcept {
    // TODO

}

std::size_t Workspace::messageCount() const noexcept {
    // TODO

}

Document& Workspace::documentAt(std::size_t index) {
    // TODO: replace this placeholder behavior.

}

const Document& Workspace::documentAt(std::size_t index) const {
    // TODO: replace this placeholder behavior.

}

Prompt& Workspace::promptAt(std::size_t index) {
    // TODO: replace this placeholder behavior.

}

const Prompt& Workspace::promptAt(std::size_t index) const {
    // TODO: replace this placeholder behavior.

}

Message& Workspace::messageAt(std::size_t index) {
    // TODO: replace this placeholder behavior.

}

const Message& Workspace::messageAt(std::size_t index) const {
    // TODO: replace this placeholder behavior.

}
