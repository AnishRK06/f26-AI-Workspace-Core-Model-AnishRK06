# M0 Design and Understanding Note

Answer briefly in your own words. This is not intended to be a long report.

1. What responsibility belongs to `Workspace`, and what responsibilities belong to `Document`, `Prompt`, and `Message` instead?
Workspace is responsible for ownership and organization. It holds a name, owns three collections of those values, preserves the order in which items were added, reports counts, and provides bounds-checked access by index. It does not inspect or interpret the contents of the values it stores. Document, Prompt, and Message are value types. Each is responsible only for its own data and the rules attached to it. Document holds a title, source path, and contents, and is responsible for loading itself from a file and reporting its size and emptiness. Prompt holds a title and text and defines emptiness in terms of its text alone. Message pairs a role with text. None of the three knows the others exist, and none knows it might be stored in a collection.

2. Why are the collections inside `Workspace` private? Explain the purpose of the const and non-const `At` overloads.
Private collections let Workspace enforce its own invariants. If the vectors were public, a caller could reorder or clear them and the guarantee that documentCount() matches what's retrievable would no longer hold. It also keeps std::vector an implementation detail that a later milestone could change.

The non-const overload returns Document&, allowing ws.documentAt(0).setTitle("New"). The const overload returns const Document&, read-only. Overload resolution picks based on the workspace's constness, so a const Workspace& can't mutate its contents. Both use vector::at, so a bad index throws std::out_of_range.

3. Explain one meaningful test you added. What behavior does it check, and what implementation error could it catch?
document_failed_load_test checks that a failed load leaves the document unchanged. It loads a real file, then loads a missing path, and asserts the call returned false and that title, source path, and contents are untouched.

It catches the natural mistake of assigning to the members before reading. That version passes every success-path test and only fails when the file is missing — at which point it has already destroyed good data.


4. Describe one implementation decision that you verified, tested, or revised before submitting your work.
My first load checked that the ifstream opened, then read via istreambuf_iterator. Testing a directory path exposed a defect: opening a directory succeeds, but the first read throws std::ios_base::failure, which escaped and terminated the program instead of returning false. I wrapped the read in a try/catch and added a bad() check. I also verified byte fidelity — wc -c text/sample.txt gives 55, and characterCount() agrees.


5. If generative AI was used, disclose it as required by course policy. If no generative AI was used, state that. The disclosure itself is not used as proof of authorship or understanding.
I used the use of AI to understand the assignment and used it to help me understand syntax. 