#include <cassert>
#include <iostream>

#include "Prompt.hpp"
#include "Workspace.hpp"

void prompt_smoke_test() {
    Prompt p("Reviewer", "Review this requirement.");
    assert(p.title() == "Reviewer");
    assert(!p.empty());
}

void workspace_smoke_test() {
    Workspace w("Demo");
    w.addPrompt(Prompt("Reviewer", "Review this requirement."));
    assert(w.promptCount() == 1);
}

int main() {
    prompt_smoke_test();
    workspace_smoke_test();

    // TODO: Add your own meaningful M0 tests.
    // See the Testing Requirements section of the specification.

    std::cout << "M0 tests passed\n";
    return 0;
}
