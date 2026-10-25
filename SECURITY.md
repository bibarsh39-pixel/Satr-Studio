# Security

Applies to Satr Studio 0.9.0. Security checks were run on 24 October 2026;
this document was updated on 25 October 2026.

## Network and privacy

Editing, building and running code work locally. Satr Studio does not upload
source files or collect analytics. The optional code-repair assistant runs a
local model in a separate process; it does not send code to an online service.
No listening server is started. Run does not download files, and opening a file
does not execute it.

The Install tools action is the network entry point owned by the editor. On Windows:

* HTTPS with normal operating-system certificate validation is required.
* Initial package domains are restricted to GitHub, Hugging Face, Microsoft,
  python.org and nodejs.org. CDN redirects use a separate exact-host allowlist.
* Redirect targets are checked **before** sending the next request. HTTP downgrade,
  embedded credentials, nonstandard ports, fragments and unknown hosts are rejected.
  Redirect chains and network waits are bounded; cancellation terminates the helper.
* Downloads must match both the pinned byte count and SHA-256 before extraction or
  execution. Failed partial downloads are discarded. A cached download is rechecked.
* Archive entries cannot escape the staging directory, target absolute paths or NTFS
  alternate streams, or create symlinks. Entry count and expanded size are limited.
* The Microsoft runtime installer additionally requires a valid Microsoft signature.
  Administrator access is requested only for the standard Microsoft installation.

On Ubuntu, installation uses the system package manager with its normal repository
signature verification and administrator prompt. It does not disable repository
authentication, TLS verification, the firewall, antivirus or SmartScreen.

## Execution boundaries

Program paths and arguments are passed as argument arrays / quoted native arguments,
not interpolated into a shell command. Test paths include spaces, Arabic characters
and shell metacharacters. Process ownership allows Stop to terminate the launched
program and its ordinary descendants. Reusing a console still launches a fresh child
process, preventing globals or interpreter state leaking between runs.

**This editor is not a security sandbox for untrusted code.** Programs run with the
user's account permissions and can access files and the network, just as programs
started from a terminal can. Deliberately detached processes and malicious programs
are not confined by these lifecycle controls. Use an isolated VM for untrusted code.

The persistent native cache verifies its digest and exact compiler options, relevant
environment and current preprocessed source before object reuse. Corrupt entries
fall back to compilation. Its disk budget is 256 MiB / 128 hash-named entries;
unrelated files are preserved. The digest detects corruption, not a malicious process
that already has write access to the same user's profile. Linux cache directories are
private to the user. Large Node projects use Node's versioned compile cache and honor
its disable/coverage environment options.

## Evidence

* 29 setup checks: unsafe URLs, redirect flows, downgrade, loop bounds, bad hashes,
  traversal archives, symlink archives, managed-directory boundaries.
* A real download of the 12,609,248-byte official Python 3.14.9 Windows archive passed
  its pinned SHA-256 check; the network test did not install it.
* 17 native-cache / IPC checks: published SHA-256 vectors, Unicode round trip,
  changed source/options/object rejection, corrupted cache rejection, stale and
  malformed console replies, size limits, eviction and preservation of other files.
* Windows real-console tests and Linux real-PTY tests cover input, exit status,
  reused consoles, fresh process state, terminal mode restoration and cleanup.
* Five-language integration and installer results are in VERIFICATION.md.

These checks cover the documented cases. They are not an independent security audit.

Node.js 24.22.1 and Windows Python 3.14.9 are the security releases published on
21 October 2026: [Node release](https://nodejs.org/en/blog/release/v24.22.1),
[Python release](https://www.python.org/downloads/release/python-3149/).
Ubuntu uses its maintained system Python packages. The Windows embedded Python
distribution does not bundle pip or third-party packages. Future upstream security
updates will still require a new verified release or normal Ubuntu system updates.

## Distribution limits

Windows installers are not Authenticode-signed by a publisher certificate. SHA-256
files detect changes but must be obtained through a trusted channel to establish
origin. No security exclusions are needed. Use the published checksums to verify
downloads. Hosting on Google Drive or GitHub does not itself certify software safety.


## Java in 0.9.0

Java uses an explicit JDK executable and argument vectors/escaped UTF-8 argument
files, never a shell command assembled from source text. Annotation processing
is disabled in the simple Run workflow. JDK downloads use the existing HTTPS
host allowlist, exact size, SHA-256 and archive traversal checks. No new network
service, telemetry or JVM daemon is introduced. Cache class names reject absolute
paths, traversal, drive/stream syntax and non-class files. Cache archive integrity
and source dependencies are checked before restoration. Normal Java code still
runs with the user's permissions and can access files and the network.
