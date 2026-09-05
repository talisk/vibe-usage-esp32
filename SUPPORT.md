# Support

Use a GitHub issue for reproducible setup, build, UI, or data problems that do
not contain sensitive information. Use the private process in
[`SECURITY.md`](SECURITY.md) for vulnerabilities or any report involving a
credential, personal Usage data, or factory identity.

Before filing an issue:

1. confirm the exact supported board and disconnect other ESP devices;
2. record the source revision or release version and image SHA-256;
3. run the relevant static/build verifier without changing flash contents;
4. capture only sanitized logs and the exact expected versus observed behavior;
5. state which evidence category failed: Build, Host, CI, API, Device,
   Installer, or Power.

Do not post API keys, Authorization headers, Wi-Fi passwords, full NVS/cardid
dumps, private source/project/session names, or raw personal Usage responses.
The project cannot recover vendor accounts or factory Recovery images.

General Vibe, FoloToy, or ZECTRIX product support belongs with the relevant
vendor. This is an independent community firmware project.
