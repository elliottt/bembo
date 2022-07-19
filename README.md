# Bembo

Bembo is an implementation of the paper [A Prettier Printer]. It does
its best to provide a functional-like api while still acknowledging that it's
implemented in c++.

The `bembo::Doc` type is the type of documents that have not yet been rendered.
To render them, you may use the `pretty` method to produce a `std::string`, or
the more flexible `render` function that takes an implementation of the `Writer`
interface instead.

[A Prettier Printer]: https://homepages.inf.ed.ac.uk/wadler/papers/prettier/prettier.pdf "A Prettier Printer"
