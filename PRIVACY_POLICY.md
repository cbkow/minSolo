# Privacy Policy

**minSolo** — Last updated: October 2026

## Summary

minSolo does not collect, store, or transmit any personal data. All application data remains on your device.

## Data Collection

minSolo does not collect analytics, telemetry, crash reports, or usage statistics. No data is sent to us or any third party automatically.

## Audio

minSolo mutes and solos other apps' audio. On macOS it does this with Core Audio process taps, which is why macOS asks for **System Audio Recording** permission. The tapped audio is discarded immediately; nothing is recorded, stored, analysed, or transmitted. On Windows it uses the per-app audio sessions of the Windows audio engine and captures nothing.

## Local Data Storage

minSolo stores the following data locally on your device:

- **macOS:** `~/Library/Preferences/com.bialkowski.minsolo.plist`
- **Windows:** `%LOCALAPPDATA%\minSolo\` (a Microsoft Store install keeps it in the app's private storage, which uninstalling removes)

Stored data is limited to application settings, such as which apps were muted. This data never leaves your device.

## Network Activity

minSolo makes no network connections. On Windows, the Microsoft Store installs and updates minSolo; the app itself makes no update requests, and the Store's own handling is covered by Microsoft's privacy statement.

## Third-Party Services

minSolo does not integrate with any advertising, analytics, or tracking services.

## Children's Privacy

minSolo does not knowingly collect any information from anyone, including children under the age of 13.

## Changes to This Policy

If this policy changes, the updated version will be published at the same location with a revised date.

## Contact

If you have questions about this privacy policy, please open an issue at:
https://github.com/cbkow/minSolo/issues
