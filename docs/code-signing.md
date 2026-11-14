# Code signing (SignPath Foundation, free): not set up

> **Status (2026-11-14):** the owner chose not to set this up (too much work for now). The release build stays unsigned; the signing steps in `ci.yml` stay switched off, and do nothing, until the settings below are added. The README's "If Windows warns about or blocks the app" says what players do meanwhile. If signing is wanted later, everything below still applies, and the README needs its code signing policy section back (SignPath requires one): see this file's git history.

Unsigned Windows programs are blocked by **Smart App Control** ("An Application Control policy has blocked this file", Setup error code 4551) and warned about by SmartScreen ("Windows protected your PC"). Smart App Control has no list of allowed apps: the only fixes are turning it off on each computer, or a signature from a certificate Windows trusts. Such certificates cost money, except through **SignPath Foundation**, which signs open-source projects for free with its own certificate. Project Prometheus qualifies on the basics: its source is public on GitHub, under the GPL (an OSI-approved licence), and its releases are built by GitHub Actions from that source.

What changes for players once it's on: the installer, the app and the three emulator cores carry a signature, and Windows names **SignPath Foundation** as the publisher (that's the foundation's certificate, as its terms require). The README's [Code signing policy](../README.md#code-signing-policy) is the page SignPath asks every project to publish.

## What's in the repository already

- **`.github/workflows/ci.yml`**, job "Project Prometheus (Windows installer)": when it runs on `main` and the SignPath settings below exist, it sends the staged app files to SignPath, waits for them to come back signed, checks every signature, builds the installer from them, then sends the installer to be signed too. Without the settings, or on any other branch, it builds unsigned exactly as before.
- **`.signpath/artifact-configurations/app-files.xml`** and **`installer.xml`**: which files SignPath signs in each request (pasted into SignPath once, below).

## What the owner does, once

1. **Two-step login** on the GitHub account (Settings, Password and authentication). SignPath Foundation requires it for everyone who can change the repository, and for SignPath itself.
2. **Apply** with the form on [signpath.org](https://signpath.org) for the project `WolfsbaneCo/project-prometheus`. Mention: a free, open-source (GPL-3.0-or-later) player for games the user owns; Windows builds made by GitHub Actions; the code signing policy is in the README. They review each project; approval can take a while, and they decide who qualifies.
3. Once approved, in **SignPath** (app.signpath.io), in the project they set up:
   - **Artifact configurations:** add one with the slug `app-files`, pasting the contents of `.signpath/artifact-configurations/app-files.xml`, and one with the slug `installer`, pasting `installer.xml`.
   - **Signing policy:** note the slug of the release signing policy (the one with SignPath Foundation's certificate; often `release-signing`). Make yourself its approver.
   - **Trusted build system:** link GitHub as SignPath asks (it checks that each signing request comes from this repository's own workflow run).
   - **A CI user with an API token:** create it, give it permission to submit requests on that signing policy, and copy the token.
   - Note the **organization ID** (in the organization's settings).
4. In **GitHub**, the repository's Settings, Secrets and variables, Actions:
   - Secret `SIGNPATH_API_TOKEN`: the CI user's token.
   - Variables `SIGNPATH_ORGANIZATION_ID`, `SIGNPATH_PROJECT_SLUG` and `SIGNPATH_SIGNING_POLICY_SLUG`: the values from step 3.

## Each release

Run CI on `main` as usual. The Windows job stops twice to wait for approval in SignPath (first the app and cores, then the installer), up to an hour each: approve both in app.signpath.io (an email arrives for each). The release then publishes the signed installer. If a request isn't approved in time, the job fails and nothing is published: run it again.

## Known gap

The uninstaller (`unins000.exe`, which Inno Setup writes while installing) isn't signed: Inno Setup signs it only while compiling the installer, with a signing tool on the build machine, which this setup doesn't have. With Smart App Control on, uninstalling from Windows' Settings may be blocked; installing a newer version over the old one, the usual way to update, isn't affected.
