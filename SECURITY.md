# Security policy

## Supported versions

Until the first tagged release, security fixes apply only to the current
`main` branch. After releases begin, the latest minor release line receives
fixes; older firmware may be asked to upgrade.

## Reporting a vulnerability

Use GitHub's **Security → Report a vulnerability** private advisory flow for
this repository. If private reporting is not enabled, contact a maintainer
through an existing private channel and ask for a secure reporting address.
Do not disclose the issue publicly before a fix or coordinated disclosure date.

Include the affected revision/version and board, impact, prerequisites, a
minimal reproduction, and whether secrets or physical access are involved.
Never attach a real API credential, Wi-Fi password, cardid dump, personal Usage
response, or unredacted serial log. Use synthetic values and describe how the
maintainer can reproduce with their own test account.

Maintainers should acknowledge a complete report within seven days, provide an
initial severity assessment within fourteen days, and coordinate fixes and
credit with the reporter. These are response targets, not guarantees.

## Security boundary

The firmware verifies TLS certificates and hostnames and confines Bearer
credentials to the trusted HTTPS origin. It intentionally does not claim
resistance to physical flash extraction: P0 stores NVS plaintext. The local
captive portal is an open AP over HTTP. Use it only while physically present.
Application logging suppresses Wi-Fi network identifiers, assigned addresses,
and Device Flow URLs/codes. Installation tools or the chip ROM may still print
hardware identifiers while proving that the selected port is the expected
board; do not retain or publish an unredacted installation transcript.

Secure Boot, Flash Encryption, eFuse provisioning, custom OTA, and guaranteed
forensic erasure are not supported. Do not “fix” these by enabling irreversible
settings without a separately reviewed installation and Recovery design.

For a lost or transferred device, unlink locally when possible and revoke Vibe
access at the service. Local unlink alone cannot prove old wear-leveled flash
pages are unrecoverable.
