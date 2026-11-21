# Wi-Fi and networking: status as of 2026-11-21 (build 0284)

## What works
- BCM4375 (`B2LWifi`, `apple80211` bridge for wifid): scan, WPA2, DHCP, DNS, TLS, auto-reconnect (0204-0206); time from the PMIC RTC (0207); wifid no longer drops the link after 60 s (0209).
- Safari loads pages without freezing the phone since 0283: `MAP_JIT` is refused to `com.apple.WebKit*` processes, so JavaScriptCore runs without JIT ([patches/0109-b2l-webkit-no-jit.patch](../../patches/0109-b2l-webkit-no-jit.patch), boot-arg `b2l_wk_jit=1` gives JIT back). Some pages still render badly.
- News loads articles.

## What does not work yet
- **Weather**: WeatherKit answers 401. AMS cannot Mescal-sign its requests because `fairplayd.H2` was disabled in launchd's `disabled.plist` by the clean USERDATA (0250). Build 0284 re-enables it (`b2l_bt_seed.h`, boot-arg `b2l_fp_launchd`: 1 enable, 2 disable again, 0 leave alone); not tested yet. geod also misses its active tile group file (NSCocoaErrorDomain 260).
- **Stocks**: network requests succeed (200), but CloudKit rejects the purchase lookup (CKErrorDomain 2).
- **Apple identity**: since 0283, `/chosen chip-id` (0x8140) and a synthetic `unique-chip-id` are visible to the identity daemons only (akd, adid, appleaccountd, fairplaydeviceidentityd, mobileactivationd; boot-arg `b2l_chip_id`). Anisette provisioning starts at Apple's server but fails at the end (-43004): no Secure Enclave key, no activation record.
- **Video in Safari** (YouTube, Reddit): no hardware video decoder. iOS looks for AppleAVD; the Exynos MFC has no driver.
- **Downloads and the Files app** (blank page): cause not found yet.
