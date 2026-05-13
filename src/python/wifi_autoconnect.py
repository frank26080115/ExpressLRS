import subprocess
import time
import urllib.request
import urllib.error
import http.client
import os
import signal
import sys
import tempfile

SSID_PREFIXES = [
    "ExpressLRS RX",
    "ExpressLRS TX",
    "ELRS-RX-",
    "ELRS-TX-",
]

WIFI_PASSWORD = "expresslrs"

TARGET_IP = "10.0.0.1"
TARGET_URL = f"http://{TARGET_IP}/bluepad/health"
CHECK_INTERVAL_SECONDS = 30
SCAN_INTERVAL_SECONDS = 5


running = True


def log(message, leading_blank=False, trailing_blank=False):
    if leading_blank:
        print()
    print(f"[{time.strftime('%H:%M:%S')}]: {message}")
    if trailing_blank:
        print()


def run(cmd):
    return subprocess.run(
        cmd,
        capture_output=True,
        text=True,
        shell=True,
        errors="replace",
    )


def notify(title, message):
    # Windows toast-ish notification via PowerShell.
    ps = f'''
    [Windows.UI.Notifications.ToastNotificationManager, Windows.UI.Notifications, ContentType = WindowsRuntime] > $null
    $template = [Windows.UI.Notifications.ToastTemplateType]::ToastText02
    $xml = [Windows.UI.Notifications.ToastNotificationManager]::GetTemplateContent($template)
    $texts = $xml.GetElementsByTagName("text")
    $texts.Item(0).AppendChild($xml.CreateTextNode("{title}")) > $null
    $texts.Item(1).AppendChild($xml.CreateTextNode("{message}")) > $null
    $toast = [Windows.UI.Notifications.ToastNotification]::new($xml)
    [Windows.UI.Notifications.ToastNotificationManager]::CreateToastNotifier("WiFi Watchdog").Show($toast)
    '''

    try:
        run(f'powershell -NoProfile -ExecutionPolicy Bypass -Command "{ps}"')
    except Exception:
        pass

    log(f"🔔 {title}: {message}", leading_blank=True, trailing_blank=True)


def scan_wifi():
    result = run("netsh wlan show networks")
    ssids = []

    for line in result.stdout.splitlines():
        line = line.strip()
        if line.startswith("SSID ") and ":" in line:
            ssid = line.split(":", 1)[1].strip()
            if ssid:
                ssids.append(ssid)

    return ssids


def find_matching_ssid(ssids):
    for ssid in ssids:
        for prefix in SSID_PREFIXES:
            if ssid.startswith(prefix):
                return ssid
    return None


def ensure_profile(ssid):
    profile_xml = f"""<?xml version="1.0"?>
<WLANProfile xmlns="http://www.microsoft.com/networking/WLAN/profile/v1">
    <name>{ssid}</name>
    <SSIDConfig>
        <SSID>
            <name>{ssid}</name>
        </SSID>
    </SSIDConfig>
    <connectionType>ESS</connectionType>
    <connectionMode>manual</connectionMode>
    <MSM>
        <security>
            <authEncryption>
                <authentication>WPA2PSK</authentication>
                <encryption>AES</encryption>
                <useOneX>false</useOneX>
            </authEncryption>
            <sharedKey>
                <keyType>passPhrase</keyType>
                <protected>false</protected>
                <keyMaterial>{WIFI_PASSWORD}</keyMaterial>
            </sharedKey>
        </security>
    </MSM>
</WLANProfile>
"""

    with tempfile.NamedTemporaryFile(
        mode="w",
        suffix=".xml",
        delete=False,
        encoding="utf-8",
    ) as f:
        f.write(profile_xml)
        temp_path = f.name

    try:
        run(f'netsh wlan add profile filename="{temp_path}" user=current')
    finally:
        try:
            os.remove(temp_path)
        except Exception:
            pass


def connect_wifi(ssid):
    log(f"📡 Connecting to {ssid!r}...")
    ensure_profile(ssid)
    result = run(f'netsh wlan connect name="{ssid}" ssid="{ssid}"')
    return result.returncode == 0


def web_page_accessible():
    try:
        with urllib.request.urlopen(TARGET_URL, timeout=5) as response:
            return 200 <= response.status < 500
    except (urllib.error.URLError, TimeoutError, OSError, http.client.HTTPException):
        return False
    except Exception as ex:
        log(f"⚠️ ERROR exception in 'web_page_accessible': {ex}")
        return False


def handle_ctrl_c(sig, frame):
    global running
    running = False
    log("🛑 CTRL-C received. Exiting cleanly...", leading_blank=True)


signal.signal(signal.SIGINT, handle_ctrl_c)


def main():
    connected_ssid = None
    already_notified = False

    log("🦝 Wi-Fi raccoon is awake. Press CTRL-C to stop.")

    while running:
        if connected_ssid and web_page_accessible():
            log(f"✅ {TARGET_IP}:80 still reachable on {connected_ssid!r}")
            time.sleep(CHECK_INTERVAL_SECONDS)
            continue

        if connected_ssid:
            log(f"⚠️ Lost access to {TARGET_IP}:80. Returning to scan mode.")
            connected_ssid = None
            already_notified = False

        log("🔍 Scanning Wi-Fi networks...")
        ssids = scan_wifi()
        match = find_matching_ssid(ssids)

        if not match:
            log("No matching SSID found.")
            time.sleep(SCAN_INTERVAL_SECONDS)
            continue

        if not connect_wifi(match):
            log(f"Could not connect to {match!r}")
            time.sleep(SCAN_INTERVAL_SECONDS)
            continue

        log("⏳ Waiting briefly for DHCP/IP setup...")
        time.sleep(5)

        if web_page_accessible():
            connected_ssid = match
            if not already_notified:
                notify(
                    "Target Wi-Fi is ready",
                    f"Connected to {match} and {TARGET_IP}:80 is reachable.",
                )
                already_notified = True
        else:
            log(f"Connected to {match!r}, but {TARGET_IP}:80 is not reachable yet.")
            time.sleep(SCAN_INTERVAL_SECONDS)

    log("Bye! 🛜")


if __name__ == "__main__":
    main()
