# Security Policy

## Supported versions

Silent Riot is pre-1.0. Security fixes go into `main` and ship in the next release; only the
**latest release** and `main` are supported. Older releases do not get backported fixes.

## Reporting a vulnerability

Please **do not open a public issue** for a security problem.

Report it privately through GitHub's private vulnerability reporting: open the repository's
**Security** tab and choose **Report a vulnerability**. Include:

- the plugin version (Settings → About) and your OS, host and plugin format;
- what happens, and the file or steps that trigger it;
- if you can, the crafted file itself (an IR, preset or saved session).

## Scope

In scope: anything a file or host can feed the plugin that makes it misbehave in a way an
attacker could use, for example

- crashes, hangs or memory-safety errors from a crafted **IR file** (WAV), **preset** or
  **saved plugin state** (a DAW project);
- out-of-bounds reads or writes, use-after-free, or unbounded allocation in the loaders;
- anything that lets plugin state reach files or the network it should not.

Out of scope: sound quality, CPU use at extreme settings, and bugs in a host or in JUCE itself
(please report those upstream; tell us too if Silent Riot needs a workaround).

## What to expect

This is a small project maintained by one person. Expect an acknowledgement within a week.
Once a fix is ready it is released and credited in `CHANGELOG.md`, unless you ask not to be
named. Please give us a reasonable chance to ship a fix before disclosing publicly.
