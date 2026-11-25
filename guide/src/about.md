# About this guide

This guide explains why catenae is shaped the way it is, how to use it, and how to write a
stage for it. It is written for one reader: an experienced software engineer who has not
worked on embedded hardware.

If that is you, some of catenae will look strange at first. It has no heap and no
exceptions. What a stage is configured with, what it remembers and what a call needs are
three separate things. A chain is a type you write, not an object you build at run time.
None of this is taste. Each part answers a constraint that a desktop machine never shows
you, and an embedded engineer meets every day.

## The shape of a chapter

Every chapter has three steps and nothing else:

1. **What you would naturally write.** The code a capable engineer writes first, on a
   machine with plenty of memory and a forgiving scheduler.
2. **What it costs on the target.** The cost is measured, and the number comes with the
   command that reproduces it. A number you cannot reproduce is not in this guide.
3. **What catenae does instead.** Shown as code that compiles.

The order matters. A rule such as "no heap" is easy to state and hard to believe until you
have seen what the alternative costs on a small processor. Once the constraint is visible,
the library's answer to it stops looking strange.

## The chapters to come

Each chapter is written as the code it describes lands, so the list below is what the guide
will hold, not what it holds today. Each one shows a constraint:

- **Where memory lives.** Flash, RAM and the stack, and why there is no heap. It leads to
  configuration, state and workspace.
- **The audio callback.** A deadline every block, in which nothing may fail, wait or
  allocate.
- **What arithmetic costs.** A floating-point unit that has no `double`, and fixed point
  and where its binary point is.
- **Stages and chains.** A mistake that should not survive compilation.
- **Rate and latency.** A stream that changes rate, and a dry path that must line up with
  a wet one.
- **Bursts and buffering.** A callback that must return as many frames as it was given,
  around a stage that works in blocks.
- **Threads and clocks.** Two call sites that cannot share a lock. It leads to junctions
  and mailboxes.
- **Measuring.** A cost that is not measured is not known.
- **Writing a stage.** The contract a stage keeps, and the tests that hold it to it.

## Every listing is compiled

No code in this guide is typed into a page. Every listing is included from an example
that the build compiles and the test battery runs, so a listing that stops compiling, or
stops doing what the page says, fails the build before it reaches you.

This is the first one. It checks that the headers a program was compiled against match the
compiled core it linked: `CATENAE_VERSION` is the number in the headers, and
`catenae::version()` returns the number the compiled core was built with.

```cpp
{{#include ../../examples/print_version.cpp:check}}
```

If the two differ, the program was built against one catenae and linked with another, and
the example reports it.

## What this guide assumes, and where the rest is

The guide assumes you know C++ and what a filter is. It does not teach signal processing.

It also does not list what each header promises. That is the job of
[the reference](api/index.html), on this same site, which documents each header's contract.
If you already work on embedded hardware, the reference is likely the faster way in: this
guide spends its time on constraints you already know.
