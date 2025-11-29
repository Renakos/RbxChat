# About
Unofficial chat replacement of Roblox's chat, because the official one will require age verification and will block chat access to all the unverified users, and to verify it, you have to do an AI-based selfie check. No one will want to show their face to a company that is not even related to Roblox but also had data breaches before. The AI is badly trained, and everyone's faces are different depending on ethnic groups, nations, etc., so an adult **MAY** be recognized as a minor. There would not be a need for a 3rd party chat to exist. RbxChat is very lightweight, faster than any other way to communicate besides physically talking. It ensures that your messages are private and only shared by you and your friends/participants. All messages are only sent to each other. The application runs topmost, with elevated privileges, so it acts like an overlay. Windows-only

# Safety notice
While the app works on Roblox and is NOT detected by Hyperion, VAC and FACEIT,(Nov 28, 2025) I can not guarantee safety with other anticheats like eac, vanguard. 

# Project Environment:
1. DirectX11/D2D;
2. Visual Studio 2026;
3. Windows 10/11;
4. Boost.Asio;
5. Boost.Beast;
6. vcpkg;


# Usage
1. Download ZeroTier: https://www.zerotier.com/download
2. Install and start ZeroTier. Let it stay running in the background.
3. Open the control panel:
* https://central.zerotier.com
* if your account was created before November 5, 2025: https://my.zerotier.com/login
4. Sign in or create an account.
5. Create a new network, or open an existing one.
6. Copy the **Network ID** displayed in the network panel.
7. Click the ZeroTier tray icon → **Join New Network** → paste the Network ID.
8. Refresh the network panel, your device should appear
9. Allow the device (push the Synchronize button).
10. Have your friends repeat these same steps, using **your** Network ID.
11. After they join, exchange your **Managed IP** addresses.
12. Start the chat application. ZeroTier must be running.
13. Input the Managed IPs of your friends. NEVER CHANGE PORTS UNLESS YOU KNOW WHAT YOU ARE DOING.
14. All participants press the **synchronize** button.
15. After synchronization, the chat window will be opened. Wait a few seconds and it is ready to work.

# How does it work?
The application establishes a secure WebSocket connection (wss://) between participants. Direct P2P connections usually don’t work because most ISPs don’t provide unique public IP addresses, which makes port forwarding or special routing necessary.
ZeroTier solves this by creating a virtual LAN. All devices in the network can see each other as if they were on the same local network, allowing the chat to work directly without complex networking setup.

# License
The project is licensed under MIT license.
