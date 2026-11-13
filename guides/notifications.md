# Notifications on your PS4, phone and computer

Orbit Store can send PS4 notifications and optional notifications through [ntfy](https://ntfy.sh). In the default layout, open **App settings → Notifications**. Other layouts use **Settings → Notifications** in the TV app and the browser.

## Choose your alerts

Choose **On this PS4**, ntfy, or both. You can select download completion or failure, successful or failed package installation, and available Orbit updates. Installation notifications also say whether the downloaded package was deleted.

The service checks for a new release ten minutes after starting, then every six hours. It notifies you once per release; it doesn’t install updates automatically.

## Set up ntfy

1. Open **Set up ntfy** or **Change ntfy settings**.
2. Enter your ntfy server address and topic. Use ntfy.sh or a server you operate.
3. Add a username and password if your server needs them. To use an access token, leave the username empty and enter the token in the password field.
4. Save, subscribe to the same topic in your phone or computer’s ntfy app, then choose **Send test notification**.

The saved password isn’t displayed again. Use a private, hard-to-guess topic or an authenticated server; anyone with access to an unprotected topic can read its notifications. The PS4 needs network access to the ntfy server. These alerts come from the Orbit service running on the PS4; they don’t turn on phone access to Orbit’s controls.

If a test fails, check the server address, topic, authentication and the PS4’s network access. PS4 notifications can stay on independently.

[Settings](settings.md) · [Automatic shutdown](shutdown.md) · [Troubleshooting](troubleshooting.md)
