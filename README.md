# RbxChat
Unofficial chat replacement of roblox's chat, because the official one will require age verification and will block chat access to all the unverified users and to verify it you have to do an AI-based selfie check.
# Usage
1. Download Zerotier from https://www.zerotier.com/download/
2. Install Zerotier
3. Run Zerotier, leave it open
4. Go to https://central.zerotier.com/ (if you already have an account created before november 5, 2025, go to https://my.zerotier.com/login) and register or login
5. Register a network or open the control panel of it if you already have a network.
6. Get the network id on the panel.
7. Open Zerotier's tray icon, right click, Join New Network, copy and paste the network id
8. Refresh the control panel of the network. You should see your device appear.
9. Ask your friend or somebody to do all of those  steps, but  add your network ID instead.
10. Give your  managed  IP to your  friend, get your friend's managed IP
11. Run the chat application, Zerotier client must be running.
12. Enter the IPs. Ask peple to do the same  steps if you need more people. Only change the ports if you know what are you doing.
13. Say all your friends to press the button "synchronize"
14. Press it
15. You will be redirected to the chat window, wait 5 seconds then you can use the chat with your friends.
16. It is topmost and admin-privileged, so it will behave itself like an overlay
# How does it work?
It makes a WebSocket over TLS (wss://) connection between you and your friends, p2p-alike. You may need to adjust your firewall settings to make it work properly. But, it wouldn't let you just "connect to each other",you need to be connected by something like LAN, because most ISPs do not give "your own" IPs, instead, they give "their own". You would also probably need port forwarding.So you use Zerotier to do all the routing, because it makes a LAN-like virtual network.
# License
The project is licensed under MIT license.
