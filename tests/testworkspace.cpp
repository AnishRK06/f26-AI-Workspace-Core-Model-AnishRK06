#include <cassert>
#include <cstdio>
#include <fstream>
#include <ios>
#include <iostream>
#include <stdexcept>
#include <string>

#include "Document.hpp"
#include "Message.hpp"
#include "Prompt.hpp"
#include "Workspace.hpp"

// "./" so the path has a separator, title should come out as just the filename
static const char *SAMPLE_PATH = "./m0_sample.txt";
static const char *SAMPLE_NAME = "m0_sample.txt";
static const char *EMPTY_PATH = "./m0_empty.txt";
static const char *MISSING_PATH = "./m0_no_such_file.txt";

// blank line and double space, so a >> based read cannot reproduce it
static const std::string SAMPLE_TEXT = "first line\n\nthird  line\n";

static void write_fixtures() {
    std::ofstream sample(SAMPLE_PATH, std::ios::binary);
    sample << SAMPLE_TEXT;
    sample.close();

    std::ofstream empty(EMPTY_PATH, std::ios::binary);
    empty.close();

    std::remove(MISSING_PATH);
}

static void remove_fixtures() {
    std::remove(SAMPLE_PATH);
    std::remove(EMPTY_PATH);
}

static void document_construction_test() {
    Document d;
    assert(d.title().empty());
    assert(d.sourcePath().empty());
    assert(d.contents().empty());
    assert(d.empty());
    assert(d.characterCount() == 0);

    // two arg ctor leaves sourcePath empty
    Document n("Notes", "abc");
    assert(n.title() == "Notes");
    assert(n.sourcePath().empty());
    assert(n.contents() == "abc");
    assert(!n.empty());
    assert(n.characterCount() == 3);
}

static void document_load_test() {
    Document d;
    assert(d.load(SAMPLE_PATH));
    assert(d.sourcePath() == SAMPLE_PATH);
    assert(d.title() == SAMPLE_NAME);
    assert(d.contents() == SAMPLE_TEXT);
    assert(d.characterCount() == SAMPLE_TEXT.size());
    assert(!d.empty());

    // no separator in the path, title is the whole thing
    Document b;
    assert(b.load(SAMPLE_NAME));
    assert(b.title() == SAMPLE_NAME);

    // a good load overwrites whatever was there
    Document old("Old Title", "old contents");
    assert(old.load(SAMPLE_PATH));
    assert(old.title() == SAMPLE_NAME);
    assert(old.contents() == SAMPLE_TEXT);
}

// boundary: empty file loads fine but leaves the document empty
static void document_empty_file_test() {
    Document d("Old Title", "old contents");
    assert(d.load(EMPTY_PATH));
    assert(d.title() == "m0_empty.txt");
    assert(d.sourcePath() == EMPTY_PATH);
    assert(d.contents().empty());
    assert(d.empty());
    assert(d.characterCount() == 0);
}

// failure case: a bad load must not touch anything
static void document_failed_load_test() {
    Document d;
    assert(d.load(SAMPLE_PATH));
    Document before = d;

    assert(!d.load(MISSING_PATH));
    assert(d.title() == SAMPLE_NAME);
    assert(d.sourcePath() == SAMPLE_PATH);
    assert(d.contents() == SAMPLE_TEXT);
    assert(d == before);

    // same for a document that was never loaded from a file
    Document kept("Kept", "kept contents");
    assert(!kept.load(MISSING_PATH));
    assert(kept.title() == "Kept");
    assert(kept.sourcePath().empty());
    assert(kept.contents() == "kept contents");

    // a directory opens but cannot be read, still has to return false
    Document dir("Kept", "kept contents");
    assert(!dir.load("."));
    assert(dir.contents() == "kept contents");
}

static void document_equality_test() {
    Document a("T", "C");
    Document b("T", "C");
    assert(a == b);
    assert(!(a != b));

    b.setTitle("Different");
    assert(a != b);

    // sourcePath is part of equality, so loaded != constructed
    Document loaded;
    assert(loaded.load(SAMPLE_PATH));
    Document made(SAMPLE_NAME, SAMPLE_TEXT);
    assert(loaded != made);
}

static void prompt_test() {
    Prompt d;
    assert(d.title().empty());
    assert(d.text().empty());
    assert(d.empty());

    Prompt p("Reviewer", "Review this requirement.");
    assert(p.title() == "Reviewer");
    assert(p.text() == "Review this requirement.");
    assert(!p.empty());

    // empty depends on text only
    Prompt titled("Has a title", "");
    assert(titled.empty());

    Prompt same("Reviewer", "Review this requirement.");
    assert(p == same);

    same.setText("changed");
    assert(p != same);

    same.setText("Review this requirement.");
    same.setTitle("changed");
    assert(p != same);
}

static void message_test() {
    Message d;
    assert(d.role() == MessageRole::User);
    assert(d.text().empty());
    assert(d.empty());

    Message m(MessageRole::System, "You are a reviewer.");
    assert(m.role() == MessageRole::System);
    assert(m.text() == "You are a reviewer.");
    assert(!m.empty());

    // same text, different role, not equal
    Message user(MessageRole::User, "hello");
    Message asst(MessageRole::Assistant, "hello");
    assert(user != asst);

    asst.setRole(MessageRole::User);
    assert(user == asst);

    Message roleonly(MessageRole::Assistant, "");
    assert(roleonly.empty());
}

static void workspace_add_test() {
    Workspace d;
    assert(d.name().empty());
    assert(d.documentCount() == 0);
    assert(d.promptCount() == 0);
    assert(d.messageCount() == 0);

    Workspace w("Demo");
    w.addDocument(Document("First", "one"));
    w.addDocument(Document("Second", "two"));
    w.addPrompt(Prompt("Reviewer", "Review this requirement."));
    w.addMessage(Message(MessageRole::User, "hi"));

    assert(w.documentCount() == 2);
    assert(w.promptCount() == 1);
    assert(w.messageCount() == 1);

    // insertion order, index 0 is the first one added
    assert(w.documentAt(0).title() == "First");
    assert(w.documentAt(1).title() == "Second");
    assert(w.promptAt(0).text() == "Review this requirement.");
    assert(w.messageAt(0).role() == MessageRole::User);
}

// failure case: bad index throws, same as vector::at
static void workspace_bad_index_test() {
    Workspace w("Demo");
    w.addDocument(Document("Only", "x"));

    bool threw = false;
    try {
        w.documentAt(1);
    } catch (const std::out_of_range&) {
        threw = true;
    }
    assert(threw);

    threw = false;
    try {
        Workspace empty;
        empty.documentAt(0);
    } catch (const std::out_of_range&) {
        threw = true;
    }
    assert(threw);

    // collections are independent, no prompts were ever added
    threw = false;
    try {
        w.promptAt(0);
    } catch (const std::out_of_range&) {
        threw = true;
    }
    assert(threw);

    threw = false;
    try {
        w.messageAt(0);
    } catch (const std::out_of_range&) {
        threw = true;
    }
    assert(threw);

    // const overload has to throw too
    threw = false;
    try {
        const Workspace& cw = w;
        cw.documentAt(5);
    } catch (const std::out_of_range&) {
        threw = true;
    }
    assert(threw);
}

// non const At returns a reference into the stored object
static void workspace_mutable_at_test() {
    Workspace w("Demo");
    w.addDocument(Document("Before", "x"));

    w.documentAt(0).setTitle("After");
    assert(w.documentAt(0).title() == "After");

    const Workspace& cw = w;
    assert(cw.documentAt(0).title() == "After");
}

static void workspace_copy_test() {
    Workspace original("Original");
    original.addDocument(Document("Doc", "contents"));
    original.addPrompt(Prompt("P", "text"));

    Workspace copy = original;
    assert(copy == original);

    original.setName("Renamed");
    original.documentAt(0).setTitle("Mutated");
    original.addDocument(Document("Extra", "y"));

    // copy is its own value, none of that reached it
    assert(copy.name() == "Original");
    assert(copy.documentAt(0).title() == "Doc");
    assert(copy.documentCount() == 1);
    assert(copy.promptCount() == 1);
    assert(copy != original);

    // add takes by value, so the caller's object is separate too
    Document mine("Caller", "contents");
    Workspace w("Demo");
    w.addDocument(mine);
    mine.setTitle("Changed After Adding");
    assert(w.documentAt(0).title() == "Caller");
}

static void workspace_equality_test() {
    Workspace a("Demo");
    a.addDocument(Document("One", "1"));
    a.addDocument(Document("Two", "2"));

    Workspace b("Demo");
    b.addDocument(Document("One", "1"));
    b.addDocument(Document("Two", "2"));
    assert(a == b);

    // same documents, different order, not equal
    Workspace rev("Demo");
    rev.addDocument(Document("Two", "2"));
    rev.addDocument(Document("One", "1"));
    assert(a != rev);

    Workspace renamed = a;
    renamed.setName("Different");
    assert(a != renamed);

    // each collection counts on its own
    Workspace extra = a;
    extra.addPrompt(Prompt("P", "text"));
    assert(a != extra);
}

int main() {
    write_fixtures();

    document_construction_test();
    document_load_test();
    document_empty_file_test();
    document_failed_load_test();
    document_equality_test();

    prompt_test();
    message_test();

    workspace_add_test();
    workspace_bad_index_test();
    workspace_mutable_at_test();
    workspace_copy_test();
    workspace_equality_test();

    remove_fixtures();

    std::cout << "M0 tests passed" << std::endl;
    return 0;
}